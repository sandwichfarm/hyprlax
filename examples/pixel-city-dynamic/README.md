# Dynamic Pixel City

Dynamic Pixel City copies the six-layer [Pixel City](../pixel-city/README.md) scene and adds a
locally generated sun, phase-correct moon, and directional foreground shadow. A Python 3
standard-library sidecar maps the current local astronomical day to smooth night, sunrise,
morning, high-noon, late-afternoon, and sunset lighting, then applies only changed values through
`hyprlax ctl modify`.

The original example is unchanged. This directory is self-contained after it is copied.

## Requirements

- Hyprlax built from this repository or installed with `make install-user`
- Python 3.9 or newer with IANA timezone data
- A supported Wayland compositor for live mode
- Network access once per day, unless cached/fallback data or `--dry-run` is used

No Python package or API key is required.

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
for deterministic lighting/command inspection, not a provider forecast. `--status` uses the real
daily cache/providers but does not open the Hyprlax socket.

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

`--demo-day` resolves today's location and astronomy once, anchors its mock clock at local
midnight, and advances through 24 wall-clock hours before exiting. The defaults produce one visual
update per real second for 60 seconds. Each JSON line includes `simulated_at`, `progress`, `phase`,
projected `sun_x`/`sun_y`, and the number of IPC commands applied.

Example full-day geographic tuning pass for Budapest while facing southeast:

```bash
python3 "$SCENE" --demo-day --demo-seconds 60 --demo-step 1 \
  --latitude 47.4979 --longitude 19.0402 --timezone Europe/Budapest \
  --locality Budapest --view-azimuth 135 --hyprlax-bin "$HYPRLAX"
```

Only one controller should own the layers. When using the installed user services, pause the
normal real-time controller around the demo:

```bash
systemctl --user stop hyprlax-pixel-city-dynamic-controller.service
python3 "$SCENE" --demo-day --hyprlax-bin "$HYPRLAX"
systemctl --user start hyprlax-pixel-city-dynamic-controller.service
```

Use `--demo-seconds` to choose a 1..3600-second cycle and `--demo-step` to choose a 0.25..5-second
visual cadence. `--at` selects the astronomical date; manual location arguments work unchanged.
The mode does not repeat provider requests on each frame.

## Daily Requests, Privacy, and Offline Behavior

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

If a service, DNS, or schema fails, the sidecar reuses validated last-good data when it is safe.
Without usable data it falls back to UTC/neutral 06:00 sunrise, 12:00 noon, 18:00 sunset, and a
hidden new moon. Animation continues, and JSON output reports `stale`, source, and bounded errors.

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
  controller and daemon paths must resolve to the same directory.
- **`another instance ... running`**: stop the previous default-socket daemon. Socket suffixes are
  intended for isolated tests, not ordinary wallpaper startup.
- **`path contains whitespace`**: move the copied directory to a whitespace-free path.
- **provider error/stale output**: inspect `--status`; same-day failures deliberately wait until
  tomorrow. Delete the cache only when you explicitly want to discard last-good data and the
  attempt record.
- **no visible sun/moon**: this is correct when the body is below the horizon or the moon events
  are unavailable. Use fixed-time `--dry-run` to inspect command geometry.
- **IPC failure in loop mode**: the sidecar logs the failure and retries on the next interval; it
  never blocks the renderer or restarts the daemon.

## Attribution

- Original Pixel City art: [CraftPix](https://craftpix.net/freebies/)
- Solar and lunar daily data: [Sunrise-Sunset.org](https://sunrise-sunset.org/)
- Approximate IP location: [ip-api](http://ip-api.com/)
