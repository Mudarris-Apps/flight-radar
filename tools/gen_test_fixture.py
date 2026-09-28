#!/usr/bin/env python3
"""gen_test_fixture.py: write tests/fixtures/states_sample.json, a synthetic
OpenSky /states/all response used by tests/test_opensky_parser.cpp.

Every row is invented: icao24 values in the 7c0000 block, made-up callsigns,
positions scattered inside the 100 km box around Sydney Airport
(-33.9461, 151.1772). The output is deterministic (fixed seed), so rerunning
the script reproduces the committed file byte for byte.

Rows 0 and 1 are fixed because the parser test asserts their values.

Usage:
    python3 tools/gen_test_fixture.py
"""
import json
import os
import random

SEED = 20260928
TIME = 1700000000
COUNT = 58
HOME_LAT, HOME_LON = -33.9461, 151.1772
DLAT, DLON = 0.85, 1.0          # a little inside the 100 km box (0.9044, 1.0829)
ORIGIN = "Australia"


def row(icao, callsign, tpos, lon, lat, baro, on_ground, vel, track, vrate,
        geo, squawk, category):
    # OpenSky state vector order:
    # icao24, callsign, origin_country, time_position, last_contact,
    # longitude, latitude, baro_altitude, on_ground, velocity, true_track,
    # vertical_rate, sensors, geo_altitude, squawk, spi, position_source,
    # category
    return [icao, callsign, ORIGIN, tpos, TIME - rnd.randint(0, 10) if tpos is None else tpos,
            lon, lat, baro, on_ground, vel, track, vrate, None, geo, squawk,
            False, 0, category]


rnd = random.Random(SEED)


def main():
    states = [
        # Asserted by the parser test: on ground, null baro/vertical rate/squawk.
        row("7c4e21", "SYN100  ", TIME - 65, 151.1695, -33.9402, None, True,
            6.17, 158.3, None, None, None, 0),
        # Asserted by the parser test: squawk "3217", baro 2438.4.
        row("7c2a01", "SYN201  ", TIME - 20, 150.8846, -34.6119, 2438.4, False,
            57.52, 222.83, -2.6, 2600.5, "3217", 0),
    ]
    used = {"7c4e21", "7c2a01"}
    while len(states) < COUNT:
        icao = "7c%04x" % rnd.randint(0x3000, 0xffff)
        if icao in used:
            continue
        used.add(icao)
        n = len(states)
        callsign = ("SYN%03d" % (n + 200)).ljust(8)
        on_ground = rnd.random() < 0.2
        if on_ground:
            lat = round(HOME_LAT + rnd.uniform(-0.02, 0.02), 4)
            lon = round(HOME_LON + rnd.uniform(-0.02, 0.02), 4)
            baro, geo, vrate = None, None, None
            vel = round(rnd.uniform(0, 15), 2)
        else:
            lat = round(HOME_LAT + rnd.uniform(-DLAT, DLAT), 4)
            lon = round(HOME_LON + rnd.uniform(-DLON, DLON), 4)
            baro = round(rnd.uniform(150, 11500), 2)
            geo = round(baro + rnd.uniform(-60, 120), 2)
            vrate = round(rnd.uniform(-12, 12), 2)
            vel = round(rnd.uniform(40, 260), 2)
        track = round(rnd.uniform(0, 359.99), 2)
        squawk = "%04o" % rnd.randint(0o1000, 0o7777)
        tpos = TIME - rnd.randint(0, 30)
        # Sprinkle nulls the parser must tolerate.
        if rnd.random() < 0.1:
            baro = None
        if rnd.random() < 0.1:
            vrate = None
        if rnd.random() < 0.15:
            squawk = None
        if rnd.random() < 0.05:
            tpos = None
        category = rnd.choice([0, 0, 1, 3, 4, 6])
        states.append(row(icao, callsign, tpos, lon, lat, baro, on_ground, vel,
                          track, vrate, geo, squawk, category))

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                       "tests", "fixtures", "states_sample.json")
    with open(out, "w", encoding="ascii") as f:
        json.dump({"time": TIME, "states": states}, f, separators=(",", ":"))
        f.write("\n")
    print("wrote %d states to %s" % (len(states), os.path.normpath(out)))


if __name__ == "__main__":
    main()
