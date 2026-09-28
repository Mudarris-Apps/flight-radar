# Security policy

## Reporting a vulnerability

Please report vulnerabilities privately using GitHub's "Report a
vulnerability" button on the repository's Security tab (a private
security advisory). Do not open a public issue for security problems.

We will acknowledge the report, investigate, and let you know what we
plan to do about it.

## What to know before you deploy

- WiFi and OpenSky credentials are stored on the device in plain text.
  `flight_radar/secrets.h` is compiled into the firmware, so the values
  end up in flash. Anyone with physical USB access to the board can read
  the flash and recover them.
- Because of that, use a dedicated OpenSky account (or API client) for
  the device, and put it on a guest or otherwise isolated WiFi network
  rather than your main one.
- TLS connections to `opensky-network.org` and
  `auth.opensky-network.org` are pinned to the ISRG Root X1 certificate.
  `OPENSKY_TLS_INSECURE` in `flight_radar/src/config.h` disables that
  check for diagnosis only; never ship firmware with it set to `1`.
- There is no over-the-air update mechanism. Firmware is only updated
  over USB.
