# Dynamic Pixel City

Dynamic Pixel City copies the six-layer [Pixel City](../pixel-city/README.md) scene and adds a
locally generated sun, phase-correct moon, directional foreground shadow, and weather driven by
[Open-Meteo](https://open-meteo.com/en/docs). A Python 3 standard-library sidecar maps the current
local astronomical day to smooth night, sunrise, morning, high-noon, late-afternoon, and sunset
lighting, then applies cloud, fog, rain, snow, hail, wind, and heat-haze effects. Weather modulates
the astronomical scene instead of replacing it: clear, calm, cool conditions retain the original
lighting while weather layers remain transparent.

Clouds attenuate celestial bodies and shadows, fog reduces distant visibility more than foreground
visibility, and rain, snow, and hail use distinct particles at two depths. Wind moves clouds, fog,
precipitation, and sparse clear-sky debris. High temperatures distort generated copies of distant
layers rather than adding an overlay-only shimmer. Transitions use bounded state changes and
quantized animation signatures to avoid flashes and unnecessary texture reloads.

The original example is unchanged. This directory is self-contained after it is copied.

## Requirements

- Hyprlax built from this repository or installed with `make install-user`
- Python 3.9 or newer with IANA timezone data
- A supported Wayland compositor for live mode
- Network access for automatic location, astronomy, and weather; manual location, valid caches,
  fallback data, `--weather off`, presets, and `--dry-run` skip their applicable provider calls

The default non-commercial example requires no Python package or API key. The hosted no-key
Open-Meteo endpoint is for non-commercial use only; commercial deployments must use a paid
Open-Meteo endpoint or self-host the service. See
[Weather Requests, Privacy, and Offline Behavior](#weather-requests-privacy-and-offline-behavior).

## Try It From the Repository

Build Hyprlax, inspect a deterministic zero-I/O preview, then start the scene:

```bash
make
python3 examples/pixel-city-dynamic/dynamic_scene.py \
  --dry-run --at 2026-07-12T12:00:00+02:00
./hyprlax --config examples/pixel-city-dynamic/parallax.toml
```

In another terminal, apply one real update and then keep it current:

```bash
python3 examples/pixel-city-dynamic/dynamic_scene.py \
  --once --hyprlax-bin "$PWD/hyprlax"
python3 examples/pixel-city-dynamic/dynamic_scene.py \
  --loop --interval 60 --hyprlax-bin "$PWD/hyprlax"
```

Only a daemon loaded from this directory matches the controller's canonical paths. Missing or
duplicate managed layers fail closed; unrelated daemon layers are ignored.

## Install a Personal Copy

The destination path must not contain whitespace because the current IPC protocol is
space-delimited.

```bash
make install-user
install -d "$HOME/.config/hyprlax/pixel-city-dynamic"
cp -a examples/pixel-city-dynamic/. \
  "$HOME/.config/hyprlax/pixel-city-dynamic/"
"$HOME/.local/bin/hyprlax" --config \
  "$HOME/.config/hyprlax/pixel-city-dynamic/parallax.toml"
```

Then, in another terminal:

```bash
python3 "$HOME/.config/hyprlax/pixel-city-dynamic/dynamic_scene.py" \
  --loop --hyprlax-bin "$HOME/.local/bin/hyprlax"
```

Do not run a second default-socket Hyprlax daemon. Stop or disable an existing wallpaper daemon
before replacing it with this copied scene.

## Controller Modes

```bash
SCENE="$HOME/.config/hyprlax/pixel-city-dynamic/dynamic_scene.py"
HYPRLAX="$HOME/.local/bin/hyprlax"

# Deterministic preview: no network, cache, generated-file write, socket, or compositor.
python3 "$SCENE" --dry-run --at 2026-07-12T20:15:00+02:00

# Resolve/cache daily facts and print the scene without touching IPC.
python3 "$SCENE" --status

# Apply once (the default live mode).
python3 "$SCENE" --once --hyprlax-bin "$HYPRLAX"

# Run continuously; accepted intervals are 15..3600 seconds.
python3 "$SCENE" --loop --interval 60 --hyprlax-bin "$HYPRLAX"

# Play today's complete local astronomical day once in 60 real seconds.
python3 "$SCENE" --demo-day --demo-seconds 60 --demo-step 1 \
  --hyprlax-bin "$HYPRLAX"

# Cycle every deterministic weather preset once in 60 real seconds; no network access.
python3 "$SCENE" --demo-weather --demo-seconds 60 --demo-step 1 \
  --hyprlax-bin "$HYPRLAX"

# Disable weather without changing astronomy.
python3 "$SCENE" --once --weather off --hyprlax-bin "$HYPRLAX"

# Preview one condition without provider or cache access.
python3 "$SCENE" --once --weather-preset rain-heavy --hyprlax-bin "$HYPRLAX"

# Bypass IP geolocation. All three location arguments are required together.
python3 "$SCENE" --once --hyprlax-bin "$HYPRLAX" \
  --latitude 47.4979 --longitude 19.0402 --timezone Europe/Budapest \
  --locality Budapest

# Reproduce a live astronomical state at an explicit offset-aware instant.
python3 "$SCENE" --once --hyprlax-bin "$HYPRLAX" \
  --at 2026-07-12T05:15:00+02:00

# Face east instead of centering the view on the solar-noon bearing.
python3 "$SCENE" --once --hyprlax-bin "$HYPRLAX" --view-azimuth 90
```

`--dry-run` intentionally uses neutral 06:00/12:00/18:00 anchors and assumed preview IDs. It is
for deterministic lighting/command inspection, not a provider forecast, and defaults to neutral
weather without network or cache access. `--status` uses the real daily and weather
caches/providers but does not open the Hyprlax socket. Its output includes the normalized weather
sample, derived weather state, source, age, stale state, and bounded provider errors.

### Weather controls

- `--weather auto|off` selects automatic weather or disables all weather work. `auto` is default;
  `off` makes no weather request and preserves astronomy.
- `--weather-refresh SECONDS` sets the automatic refresh bucket. Default is 900 seconds; accepted
  values are 600 through 21600 seconds.
- `--weather-cache PATH` overrides the separate weather cache location.
- `--weather-preset PRESET` bypasses provider and cache access for deterministic tuning.
- `--demo-weather` cycles all presets in one deterministic demo and never uses the network.

Available presets are `clear`, `cloudy`, `fog`, `rain-light`, `rain-heavy`, `snow-light`,
`snow-heavy`, `hail-light`, `hail-heavy`, `wind`, and `heat`.

### Geographic sun projection

During a normal solar day, the controller uses the provider's sunrise, solar-noon, and sunset
timestamps plus their azimuths and the solar-noon altitude. It interpolates altitude with a sine
rise before noon and a cosine descent after noon. Azimuth follows the shortest angular path, so
an interval such as 350 degrees to 10 degrees crosses north at 0 degrees instead of sweeping
through south.

Those solar coordinates feed a 2D side projection in scene space. Horizontal position combines
relative azimuth with the altitude-adjusted 0.34 scene radius; vertical position maps the sine of
altitude from the 0.18 horizon toward the -0.24 zenith limit. All output remains within the
existing `SceneState` bounds.

`--view-azimuth DEGREES` sets the compass direction faced by the scene: 0 is north, 90 east, 180
south, and 270 west. Values from 0 through 360 are accepted. When omitted, the controller uses
the day's solar-noon azimuth, which horizontally centers the sun at solar noon.

If any required timestamp, azimuth, or solar-noon altitude is missing, non-finite, out of range,
or out of chronological order, the controller uses the legacy static trajectory. Visibility,
opacity, inverse UV offsets, caching, IPC ownership, polar night, and midnight sun retain their
existing behavior.

### Full-day tuning mode

`--demo-day` resolves today's location, astronomy, and weather timeline once, anchors its mock clock
at local midnight, and advances through 24 wall-clock hours before exiting. It does not issue a
provider request per frame. The defaults produce one visual update per real second for 60 seconds.
Each JSON line includes `simulated_at`, `progress`, `phase`, projected `sun_x`/`sun_y`, and the
number of IPC commands applied.

Example full-day geographic tuning pass for Budapest while facing southeast:

```bash
python3 "$SCENE" --demo-day --demo-seconds 60 --demo-step 1 \
  --latitude 47.4979 --longitude 19.0402 --timezone Europe/Budapest \
  --locality Budapest --view-azimuth 135 --hyprlax-bin "$HYPRLAX"
```

Only one controller should own the layers. When using the installed user services, pause the
normal real-time controller around either demo. Use a restoration trap so interruption cannot
leave an originally active controller stopped:

```bash
CONTROLLER=hyprlax-pixel-city-dynamic-controller.service
was_active=0
systemctl --user is-active --quiet "$CONTROLLER" && was_active=1
restore_controller() {
  if [ "$was_active" -eq 1 ]; then
    systemctl --user start "$CONTROLLER"
  fi
}
trap restore_controller EXIT INT TERM

systemctl --user stop "$CONTROLLER"
python3 "$SCENE" --demo-day --demo-seconds 60 --demo-step 1 \
  --hyprlax-bin "$HYPRLAX"

trap - EXIT INT TERM
restore_controller
systemctl --user is-active "$CONTROLLER"
```

For the 60-second weather pass, use the same sequence and replace the Python command with:

```bash
python3 "$SCENE" --demo-weather --demo-seconds 60 --demo-step 1 \
  --hyprlax-bin "$HYPRLAX"
```

Use `--demo-seconds` to choose a 1..3600-second cycle and `--demo-step` to choose a 0.25..5-second
visual cadence. `--at` selects the astronomical date; manual location arguments work unchanged.
Neither demo repeats provider requests on each frame. `--demo-weather` makes no provider request at
all.

## Weather Requests, Privacy, and Offline Behavior

Automatic weather uses one bounded request to
`https://api.open-meteo.com/v1/forecast`. It reuses the controller's resolved latitude,
longitude, and IANA timezone, requests two forecast days, and sets Celsius, metres per second, and
millimetres explicitly. Both `current` and `hourly` request exactly these fields:

```text
temperature_2m,apparent_temperature,weather_code,cloud_cover,visibility,
precipitation,rain,showers,snowfall,snow_depth,wind_speed_10m,
wind_direction_10m,wind_gusts_10m
```

The request therefore includes `timezone={iana_timezone}`, `forecast_days=2`,
`temperature_unit=celsius`, `wind_speed_unit=ms`, and `precipitation_unit=mm`. Responses are
limited to 256 KiB, time out after 10 seconds, and are rejected when their schema, units, numeric
ranges, finiteness, array lengths, timestamps, response-coordinate bounds, or timezone are invalid.
Open-Meteo may return its nearby weather-grid coordinate; cache ownership still uses the exact
requested location identity. Hourly local timestamps are interpreted in the resolved IANA
timezone. No API key is sent.

The weather cache is separate from the daily astronomy cache. It defaults to
`$XDG_CACHE_HOME/hyprlax/pixel-city-dynamic/weather-v1.json` (or
`~/.cache/hyprlax/pixel-city-dynamic/weather-v1.json`). Each location and UTC refresh bucket gets
one process-shared, pre-network attempt reservation, including failed and concurrent starts.
Writes are private and atomic. Tiny provider changes are quantized before generated assets are
updated.

Fallback order is fresh weather, then matching last-good weather younger than three hours, then
neutral clear/calm/cool weather. Data for different coordinates or timezone are never reused.
Neutral weather hides overlays, restores original base-image paths, disables heat distortion, and
leaves astronomy, moon, shadow, IPC ownership, and service behavior unchanged.

The free hosted API needs no key, but [Open-Meteo's terms](https://open-meteo.com/en/terms) limit
it to non-commercial use and publish ceilings below 600 calls per minute, 5,000 per hour, 10,000
per day, and 300,000 per month. It has no uptime guarantee. Commercial use requires the paid
`customer-api.open-meteo.com` service with an API key or a self-hosted Open-Meteo server. API data
remain under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/); attribution is required in
all modes.

Privacy boundary: each live forecast request sends the resolved coordinates and requesting public
IP address to Open-Meteo. The provider says troubleshooting logs may contain sensitive geographic
coordinates, are not shared with third parties, and are deleted after 90 days. Review
[Open-Meteo's privacy terms](https://open-meteo.com/en/terms#privacy) before enabling automatic
weather. Use `--weather off` or a preset when those disclosures are unsuitable.

## Layer and Performance Model

The scene has 14 canonical core layers, back to front:

1. `1.png`
2. sun
3. moon
4. `weather-cloud`
5. `2.png`
6. `3.png`
7. `weather-fog-back`
8. `4.png`
9. `weather-precip-back`
10. `5.png`
11. shadow
12. `6.png`
13. `weather-fog-front`
14. `weather-precip-front`, also used for clear-sky wind debris

Weather animation does not send an IPC command for every frame. The controller generates
deterministic looping palette GIFs at 576x324 with at most eight frames, while the Hyprlax renderer
advances frames internally. Each effect has A/B asset paths: the inactive file is written
atomically, decoded and validated, then selected only when the quantized weather signature changes.
Unchanged weather causes no path churn. Clear conditions select committed transparent weather
assets and restore original base PNG paths.

Heat haze uses managed A/B GIF copies of distant layers 1 through 3 with real row-wise animated
distortion. Original artwork is never edited. Cloud, fog, and precipitation each have bounded
opacity, tint, particle geometry, and motion; two depth planes make fog and particle density read
as depth instead of a full-scene alpha change. Provider access, file writes, and allocations stay
outside the Hyprlax render loop.

## Daily Astronomy Requests, Privacy, and Offline Behavior

The cache defaults to
`$XDG_CACHE_HOME/hyprlax/pixel-city-dynamic/daily-v1.json` (or
`~/.cache/hyprlax/pixel-city-dynamic/daily-v1.json`). Writes are process-locked, private, and
atomic. Each attempt is reserved before network I/O, so concurrent starts and failed requests do
not create retry storms.

For each resolved local calendar date, the absolute ceilings are:

1. At most one [ip-api](http://ip-api.com/) geolocation attempt when no manual location is given.
2. At most one combined [Sunrise-Sunset.org v2](https://sunrise-sunset.org/api) astronomy attempt
   for sunrise, sunset, twilight, solar noon/position, moonrise, moonset, phase, and illumination.

A failed attempt is not retried until the next local date. Manual latitude/longitude/timezone
skips ip-api completely. Changing manual location after astronomy was already attempted that day
does not bypass the ceiling; the controller uses a neutral fallback until the next date rather
than applying astronomy cached for the wrong location.

Privacy and service limitations:

- The ip-api free endpoint sees the requesting public IP, is HTTP-only, requires no key, and is
  restricted to non-commercial use. Use the manual location arguments if that is unsuitable.
- Sunrise-Sunset.org receives latitude, longitude, date, and timezone over HTTPS. Its free API
  requires no key. Use is subject to its [terms](https://sunrise-sunset.org/terms).
- Responses are schema/range/date/timezone validated, limited to 256 KiB, and use a 10-second
  timeout. Remote content is never executed.

If an astronomy service, DNS lookup, or schema fails, the sidecar reuses validated last-good daily
data when it is safe. Without usable data it falls back to
neutral 06:00 sunrise, 12:00 noon, 18:00 sunset, and a hidden new moon. Animation continues, and
JSON output reports `stale`, source,
and bounded errors. Weather failures follow the independent 900-second/three-hour rules above and
never stop astronomy.

## systemd User Services

The committed units expect the personal-copy paths above and `make install-user`. First import the
active graphical-session environment into the user manager:

```bash
systemctl --user import-environment \
  WAYLAND_DISPLAY XDG_RUNTIME_DIR HYPRLAND_INSTANCE_SIGNATURE
install -Dm644 \
  "$HOME/.config/hyprlax/pixel-city-dynamic/hyprlax-pixel-city-dynamic.service" \
  "$HOME/.config/systemd/user/hyprlax-pixel-city-dynamic.service"
install -Dm644 \
  "$HOME/.config/hyprlax/pixel-city-dynamic/hyprlax-pixel-city-dynamic-controller.service" \
  "$HOME/.config/systemd/user/hyprlax-pixel-city-dynamic-controller.service"
systemctl --user daemon-reload
systemctl --user enable --now \
  hyprlax-pixel-city-dynamic.service \
  hyprlax-pixel-city-dynamic-controller.service
```

Inspect or stop both services with:

```bash
systemctl --user status \
  hyprlax-pixel-city-dynamic.service \
  hyprlax-pixel-city-dynamic-controller.service
journalctl --user -u hyprlax-pixel-city-dynamic-controller.service -f
systemctl --user disable --now \
  hyprlax-pixel-city-dynamic-controller.service \
  hyprlax-pixel-city-dynamic.service
```

## Troubleshooting

- **`managed layers missing from daemon`**: start Hyprlax with this exact copied `parallax.toml`;
  controller and daemon paths must resolve to the same directory. Weather mode expects exactly 14
  canonical core layers; missing or duplicate managed weather layers fail closed rather than
  modifying an unrelated layer.
- **`another instance ... running`**: stop the previous default-socket daemon. Socket suffixes are
  intended for isolated tests, not ordinary wallpaper startup.
- **`path contains whitespace`**: move the copied directory to a whitespace-free path.
- **stale weather/provider error**: inspect `--status` for weather source, age, stale state, and
  bounded error. A failed attempt is not retried in the same UTC refresh bucket. Matching last-good
  weather is usable for three hours; older data becomes neutral weather. Delete the weather cache
  only when intentionally discarding both last-good data and its attempt record. Daily astronomy
  failures still wait until the next local date.
- **no visible sun/moon**: this is correct when the body is below the horizon or the moon events
  are unavailable. Use fixed-time `--dry-run` to inspect command geometry.
- **weather effect invisible**: clear weather deliberately uses transparent overlays. Inspect
  `--status`, then run `--weather-preset rain-heavy`, `--weather-preset fog`, or
  `--weather-preset heat`. Heat is intentionally suppressed by heavy cloud, fog, or precipitation.
- **hail never appears with live weather**: Open-Meteo only forecasts WMO hail codes 96 and 99 in
  supported regions, including Central Europe. Hail is never inferred from heavy rain. Use
  `--weather-preset hail-light` or `--weather-preset hail-heavy` for deterministic tuning elsewhere.
- **IPC failure in loop mode**: the sidecar logs the failure and retries on the next interval; it
  never blocks the renderer or restarts the daemon.

## Attribution

- Original Pixel City art: [CraftPix](https://craftpix.net/freebies/)
- Solar and lunar daily data: [Sunrise-Sunset.org](https://sunrise-sunset.org/)
- Approximate IP location: [ip-api](http://ip-api.com/)
- [Weather data by Open-Meteo.com](https://open-meteo.com/) is licensed under
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Dynamic Pixel City transforms the
  provider's numeric forecast data into interpolated, normalized visual-effect states and
  generated animations; Open-Meteo does not endorse this project.
