# Stack Research: Dynamic Pixel City

**Researched:** 2026-07-12
**Confidence:** High for repository surfaces; medium-high for the newly released astronomy fields

## Recommendation

Keep the feature outside the C render loop as a Python 3 standard-library controller shipped
inside the copied example. Use the installed `hyprlax ctl` client as the only runtime mutation
surface and generate small transparent PNG overlays with `struct` and `zlib`.

| Concern | Choice | Why |
|---------|--------|-----|
| Runtime | Python 3 standard library | Already common on the target systems; no repository dependency |
| HTTP | `urllib.request` with bounded reads and timeouts | HTTPS support, explicit headers, no package install |
| Time zones | `datetime` + `zoneinfo` | Correct IANA-zone and DST handling from Python 3.9+ |
| Cache | JSON under XDG cache, locked and atomically replaced | Human-inspectable, crash-safe, testable |
| Geolocation | ip-api.com free JSON endpoint | Explicit user requirement; returns lat/lon/IANA timezone |
| Astronomy | Sunrise-Sunset.org v2 daily endpoint | One HTTPS response supplies solar events, azimuth/altitude, moonrise/set, phase, illumination |
| Rendering | Current Hyprlax PNG/tint/opacity/blur/UV controls | Preserves the render loop and exercises public behavior |
| Tests | `unittest` with injected clock/fetch/command runner | Deterministic, network- and compositor-independent |

## Verified Provider Contracts

### ip-api.com

- Endpoint: `http://ip-api.com/json/?fields=status,message,lat,lon,timezone,city,regionName,country,countryCode`
- Free endpoint has no API key, but is HTTP-only and limited to non-commercial use.
- Validate both HTTP status and JSON `status`; errors may arrive as HTTP 200 with `status=fail`.
- The controller must expose manual `--latitude`, `--longitude`, and `--timezone` overrides so
  users can avoid IP lookup or use the example in commercial/HTTPS-constrained contexts.
- Official documentation: <https://ip-api.com/docs/api:json>

### Sunrise-Sunset.org v2

- Endpoint: `https://api.sunrise-sunset.org/v2?lat=...&lng=...&date=YYYY-MM-DD`
- One response includes sunrise, sunset, solar noon, twilight, golden/blue hours, solar
  azimuth/altitude, moonrise, moonset, named moon phase, and illumination percentage.
- Null events and `sun_status` must be handled for polar day/night.
- Visible linked attribution is required; the example README must carry it.
- Official documentation: <https://sunrise-sunset.org/api>

## What Not To Add

- Do not add requests, Pillow, astral, ephem, or a C HTTP dependency.
- Do not fetch from inside Hyprlax's event/render loop.
- Do not use browser JavaScript: ip-api's free endpoint is HTTP-only and unsuitable for an HTTPS page.
- Do not hard-code layer IDs, since IDs are assigned at runtime.

## Compatibility Target

- Python 3.9+ (required for standard-library `zoneinfo`).
- Current `origin/master` `hyprlax ctl list --json` and `ctl modify` contracts.
- All existing Wayland compositor adapters, because the controller only targets Hyprlax IPC.
