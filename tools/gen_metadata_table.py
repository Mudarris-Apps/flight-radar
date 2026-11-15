#!/usr/bin/env python3
"""gen_metadata_table.py: regenerate flight_radar/src/metadata_table.cpp from the
OpenSky aircraft metadata database: every aircraft worldwide that has a
registration, packed into 14 bytes a row, plus an airline-designator -> name
table (from the operatoricao/operator columns) so the card can name the airline
of any flight from its callsign.

Usage:
    python3 tools/gen_metadata_table.py <csv path or URL>

The OpenSky CSV export is comma-delimited. Older exports quote fields with a
single quote ('), newer ones with a double quote; we detect which from the
header line and parse with csv.reader, which handles both, including doubled
quote escapes.

The output is OpenSky-derived data under OpenSky's terms of use: generate it
locally and never commit it (CI only accepts the empty stub).
"""
import collections
import csv
import io
import itertools
import os
import sys
import urllib.request

REG_WIDTH = 7       # 7 bytes in the packed row, NUL padded
TYPE_WIDTH = 4      # char[5]
MODEL_WIDTH = 23    # char[24]
OP_WIDTH = 23       # char[24]
ROW_BYTES = 14
LINE_BYTES = 64     # bytes per string-literal line in the output


def clean_ascii(value, width):
    """Strip whitespace, replace non-ASCII with '?', truncate to width."""
    value = ''.join(ch if ord(ch) < 128 else '?' for ch in value.strip())
    return value[:width]


def c_str(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def open_csv_source(path_or_url):
    if path_or_url.startswith('http://') or path_or_url.startswith('https://'):
        req = urllib.request.Request(path_or_url, headers={'User-Agent': 'flight-radar-gen/1.0'})
        resp = urllib.request.urlopen(req, timeout=120)
        return io.TextIOWrapper(resp, encoding='utf-8', errors='replace', newline='')
    return open(path_or_url, newline='', encoding='utf-8', errors='replace')


def bytes_literal(data):
    """Adjacent C string literals, 3-digit octal escapes (never ambiguous)."""
    lines = []
    for i in range(0, len(data), LINE_BYTES):
        chunk = data[i:i + LINE_BYTES]
        lines.append('  "' + ''.join('\\%03o' % b for b in chunk) + '"')
    return '\n'.join(lines) if lines else '  ""'


def main():
    if len(sys.argv) != 2:
        print("usage: gen_metadata_table.py <csv path or URL>", file=sys.stderr)
        return 2

    rows = {}                                    # icao24 -> (reg, (type, model), op)
    airline_names = collections.defaultdict(collections.Counter)   # ICAO code -> Counter(name)

    f = open_csv_source(sys.argv[1])
    try:
        first = f.readline()
        quote = '"' if first.lstrip().startswith('"') else "'"
        reader = csv.reader(itertools.chain([first], f), quotechar=quote)
        header = next(reader)
        idx = {name.strip().strip("'\"").lower(): i for i, name in enumerate(header)}
        needed = ['icao24', 'registration', 'model', 'typecode', 'operator', 'operatoricao']
        for col in needed:
            if col not in idx:
                print(f"error: missing expected column '{col}' in CSV header: {header}", file=sys.stderr)
                return 1
        i_icao, i_reg, i_model = idx['icao24'], idx['registration'], idx['model']
        i_type, i_op, i_opicao = idx['typecode'], idx['operator'], idx['operatoricao']
        width = max(i_icao, i_reg, i_model, i_type, i_op, i_opicao)

        for row in reader:
            if len(row) <= width:
                continue
            op = clean_ascii(row[i_op], OP_WIDTH)
            code = row[i_opicao].strip().upper()
            if op and len(code) == 3 and code.isalpha():
                airline_names[code][op] += 1
            try:
                icao_val = int(row[i_icao].strip(), 16)
            except ValueError:
                continue
            if not (0 < icao_val <= 0xFFFFFF) or icao_val in rows:
                continue
            reg = clean_ascii(row[i_reg], REG_WIDTH)
            if not reg:
                continue  # no registration: nothing useful to show on the card
            pair = (clean_ascii(row[i_type], TYPE_WIDTH), clean_ascii(row[i_model], MODEL_WIDTH))
            rows[icao_val] = (reg, pair, op)
    finally:
        f.close()

    pairs = [('', '')] + sorted({p for _, p, _ in rows.values()} - {('', '')})
    ops = [''] + sorted({o for _, _, o in rows.values()} - {''})
    if len(pairs) > 0xFFFF or len(ops) > 0xFFFF:
        print(f"error: too many types ({len(pairs)}) or operators ({len(ops)}) for 16-bit indexes", file=sys.stderr)
        return 1
    pair_idx = {p: i for i, p in enumerate(pairs)}
    op_idx = {o: i for i, o in enumerate(ops)}

    packed = bytearray()
    for icao_val in sorted(rows):
        reg, pair, op = rows[icao_val]
        t, o = pair_idx[pair], op_idx[op]
        packed += icao_val.to_bytes(3, 'big')
        packed += reg.encode('ascii').ljust(REG_WIDTH, b'\0')
        packed += bytes((t & 0xFF, t >> 8, o & 0xFF, o >> 8))
    assert len(packed) == len(rows) * ROW_BYTES

    airlines = sorted((code, names.most_common(1)[0][0]) for code, names in airline_names.items())

    out_path = os.path.normpath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', 'flight_radar', 'src', 'metadata_table.cpp'))
    with open(out_path, 'w', encoding='ascii') as out:
        out.write('// metadata_table.cpp: GENERATED by tools/gen_metadata_table.py from the OpenSky\n'
                  '// aircraft metadata database. Do not edit by hand; rerun the generator instead.\n'
                  '// OpenSky-derived data under OpenSky\'s terms of use: keep it local, never\n'
                  '// commit it (CI only accepts the empty stub).\n'
                  '#include "metadata_table.h"\n\n')
        out.write(f'const size_t METADATA_COUNT = {len(rows)};\n')
        # +1 so the literal's trailing NUL fits.
        out.write(f'const uint8_t METADATA_ROWS[{len(packed) + 1}] =\n{bytes_literal(packed)};\n\n')
        out.write(f'const size_t METADATA_TYPE_COUNT = {len(pairs)};\n')
        out.write('const char METADATA_TYPES[][5] = {\n' + ',\n'.join('  ' + c_str(t) for t, _ in pairs) + '\n};\n')
        out.write('const char METADATA_MODELS[][24] = {\n' + ',\n'.join('  ' + c_str(m) for _, m in pairs) + '\n};\n\n')
        out.write(f'const size_t METADATA_OP_COUNT = {len(ops)};\n')
        out.write('const char METADATA_OPS[][24] = {\n' + ',\n'.join('  ' + c_str(o) for o in ops) + '\n};\n\n')
        out.write(f'const size_t AIRLINE_COUNT = {len(airlines)};\n')
        out.write('const char AIRLINE_CODES[][4] = {\n' + ',\n'.join('  ' + c_str(c) for c, _ in airlines) + '\n};\n')
        out.write('const char AIRLINE_NAMES[][24] = {\n' + ',\n'.join('  ' + c_str(n) for _, n in airlines) + '\n};\n')

    print(f"wrote {len(rows)} aircraft ({len(packed) / 1e6:.1f} MB packed), {len(pairs)} type/model pairs, "
          f"{len(ops)} operators, {len(airlines)} airlines to {out_path}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
