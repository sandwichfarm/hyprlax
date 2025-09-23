#!/usr/bin/env python3

import os
import sys
import json
import math
import time
import shutil
import argparse
import subprocess
from datetime import datetime, timedelta, timezone
from urllib import request, parse, error as urlerror

try:
    from PIL import Image, ImageDraw, ImageFilter, ImageChops
except Exception as e:
    print("Pillow (PIL) is required: pip install pillow", file=sys.stderr)
    raise


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ASSETS_DIR = os.path.join(SCRIPT_DIR, 'assets')
CACHE_DIR = os.path.join(ASSETS_DIR, 'cache')
TMP_DIR = os.path.join(SCRIPT_DIR, 'tmp')
SECRETS_FILE = os.path.join(SCRIPT_DIR, '.secrets.env')
ASTRO_CACHE_JSON = os.path.join(CACHE_DIR, 'astro.json')


def ensure_dirs():
    for d in (ASSETS_DIR, CACHE_DIR, TMP_DIR):
        os.makedirs(d, exist_ok=True)


def parse_env_file(path):
    data = {}
    if not os.path.exists(path):
        return data
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if '=' in line:
                k, v = line.split('=', 1)
                data[k.strip()] = v.strip()
    return data


def run_ctl(args, capture=True):
    cmd = ['hyprlax', 'ctl'] + args
    try:
        if capture:
            return subprocess.run(cmd, capture_output=True, text=True)
        else:
            return subprocess.run(cmd)
    except FileNotFoundError:
        raise RuntimeError("hyprlax binary not found in PATH")


def get_status_json():
    res = run_ctl(['status', '--json'])
    if res.returncode != 0:
        return None
    try:
        return json.loads(res.stdout)
    except Exception:
        return None


def get_mon_geometry():
    # Return (width, height) for the primary or largest monitor
    sj = get_status_json()
    if sj and 'monitors' in sj and sj['monitors']:
        # choose largest by area
        best = None
        best_area = -1
        for m in sj['monitors']:
            w, h = m.get('width', 0), m.get('height', 0)
            area = w * h
            if area > best_area:
                best_area = area
                best = (w, h)
        if best and best[0] > 0 and best[1] > 0:
            return best
    # Fallback
    return (3840, 2160)


def atomic_write(img, path):
    # Save atomically using a temp .png then rename
    tmp = path + '.tmp.png'
    img.save(tmp, format='PNG')
    os.replace(tmp, path)


def parse_at_time(at_str, tz_str):
    if not at_str:
        return None
    # Try ISO 8601 first
    try:
        dt = datetime.fromisoformat(at_str)
        if dt.tzinfo is None:
            tz_dt = now_tz(tz_str)
            dt = dt.replace(tzinfo=tz_dt.tzinfo)
        return dt
    except Exception:
        pass
    # Try HH:MM or HH:MM:SS for today in local tz
    try:
        import re
        m = re.match(r"^(\d{1,2}):(\d{2})(?::(\d{2}))?$", at_str)
        if m:
            hh = int(m.group(1)); mm = int(m.group(2)); ss = int(m.group(3) or 0)
            base = now_tz(tz_str)
            return base.replace(hour=hh, minute=mm, second=ss, microsecond=0)
    except Exception:
        pass
    return None


def generate_sun_svg(path: str, diameter: int = 400):
    # Minimal radial gradient-like SVG; purely illustrative for users
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{diameter}" height="{diameter}" viewBox="0 0 {diameter} {diameter}">
  <defs>
    <radialGradient id="g" cx="50%" cy="50%" r="50%">
      <stop offset="0%" stop-color="#fff59e" stop-opacity="1"/>
      <stop offset="60%" stop-color="#ffd54f" stop-opacity="0.95"/>
      <stop offset="100%" stop-color="#ffb300" stop-opacity="0.0"/>
    </radialGradient>
  </defs>
  <circle cx="{diameter//2}" cy="{diameter//2}" r="{diameter//2}" fill="url(#g)"/>
  <circle cx="{diameter//2}" cy="{diameter//2}" r="{int(diameter*0.35)}" fill="#ffeb3b"/>
 </svg>'''
    with open(path, 'w') as f:
        f.write(svg)


def generate_moon_svgs(dirpath: str, buckets: int = 30, diameter: int = 300):
    os.makedirs(dirpath, exist_ok=True)
    for i in range(buckets):
        p = i / (buckets - 1) if buckets > 1 else 0.0
        # phase: 0=new, 0.5=full
        # Use simple two-circle approach with offset mask to suggest phase
        r = diameter / 2
        cx = cy = r
        # offset from center for shadow circle
        # new moon near p=0: full cover; full moon p=0.5: overlapped but not visible shadow
        angle = (p * 2.0 * math.pi)  # map 0..1 to 0..2pi
        # offset magnitude from -r..r; cosine-like curve for waxing/waning
        dx = r * math.cos(angle)
        svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{diameter}" height="{diameter}" viewBox="0 0 {diameter} {diameter}">
  <defs>
    <mask id="m"><rect width="100%" height="100%" fill="white"/>
      <circle cx="{cx+dx:.2f}" cy="{cy:.2f}" r="{r:.2f}" fill="black"/>
    </mask>
  </defs>
  <circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}" fill="#f5f5f5"/>
  <circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}" fill="#f5f5f5" mask="url(#m)"/>
 </svg>'''
        out = os.path.join(dirpath, f'moon_{i:02d}.svg')
        with open(out, 'w') as f:
            f.write(svg)


def ensure_sun_sprite(diameter: int) -> str:
    # Render a soft-glow sun sprite using Pillow only (no external deps)
    path = os.path.join(CACHE_DIR, f'sun_{diameter}.png')
    if os.path.exists(path):
        return path
    img = Image.new('RGBA', (diameter, diameter), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # glow layers
    for frac, alpha in [(1.0, 40), (0.85, 60), (0.7, 80)]:
        r = int(diameter * frac / 2)
        bbox = [diameter//2 - r, diameter//2 - r, diameter//2 + r, diameter//2 + r]
        draw.ellipse(bbox, fill=(255, 179, 0, alpha))
    # core
    r = int(diameter * 0.32)
    bbox = [diameter//2 - r, diameter//2 - r, diameter//2 + r, diameter//2 + r]
    draw.ellipse(bbox, fill=(255, 235, 59, 230))
    img = img.filter(ImageFilter.GaussianBlur(radius=diameter * 0.04))
    img.save(path)
    # Ensure SVG exists for demo
    svg_path = os.path.join(ASSETS_DIR, 'sun.svg')
    if not os.path.exists(svg_path):
        try:
            generate_sun_svg(svg_path, max(200, diameter))
        except Exception:
            pass
    return path


def ensure_moon_sprite(diameter: int, phase01: float) -> str:
    # Cache by bucket
    buckets = 30
    bi = int(round(phase01 * (buckets - 1)))
    path = os.path.join(CACHE_DIR, f'moon_{bi:02d}_{diameter}.png')
    if os.path.exists(path):
        return path
    # Render crescent/gibbous via two circles
    img = Image.new('RGBA', (diameter, diameter), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    r = diameter / 2
    cx = cy = r
    # Base disc
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(245, 245, 245, 255))
    # Shadow disc offset controls phase
    angle = phase01 * 2.0 * math.pi
    dx = r * math.cos(angle)
    shadow = Image.new('L', (diameter, diameter), 0)
    sd = ImageDraw.Draw(shadow)
    sd.ellipse([cx + dx - r, cy - r, cx + dx + r, cy + r], fill=255)
    # Apply mask via compositing to create crescent effect
    img.putalpha(ImageChops.subtract(img.split()[3], shadow))
    # Slight blur for softer edge
    img = img.filter(ImageFilter.GaussianBlur(radius=max(1, int(diameter * 0.01))))
    img.save(path)
    # Ensure SVGs exist for demo
    moon_svg_dir = os.path.join(ASSETS_DIR, 'moon_svgs')
    if not os.path.exists(moon_svg_dir) or not os.listdir(moon_svg_dir):
        try:
            generate_moon_svgs(moon_svg_dir, buckets=buckets, diameter=max(200, diameter))
        except Exception:
            pass
    return path


def load_secrets():
    env = {}
    env.update(parse_env_file(SECRETS_FILE))
    # allow environment override
    for k in ('OPENWEATHER_API_KEY', 'LAT', 'LON', 'TZ'):
        if os.environ.get(k):
            env[k] = os.environ.get(k)
    return env


def now_tz(tz_str):
    if tz_str:
        try:
            import zoneinfo  # Python 3.9+
            tz = zoneinfo.ZoneInfo(tz_str)
            return datetime.now(tz)
        except Exception:
            pass
    return datetime.now().astimezone()


def fetch_openweather(env: dict, verbose=False):
    key = env.get('OPENWEATHER_API_KEY')
    lat = env.get('LAT')
    lon = env.get('LON')
    if not key or not lat or not lon:
        return None
    base = 'https://api.openweathermap.org/data/3.0/onecall'
    params = {
        'lat': lat,
        'lon': lon,
        'exclude': 'minutely,hourly,alerts',
        'appid': key,
        'units': 'metric',
    }
    url = f"{base}?{parse.urlencode(params)}"
    try:
        if verbose:
            print(f"[HTTP] GET {url}")
        with request.urlopen(url, timeout=10) as resp:
            if resp.status != 200:
                return None
            data = json.loads(resp.read().decode('utf-8'))
            return data
    except urlerror.URLError:
        return None
    except Exception:
        return None


def load_astro_cache():
    if not os.path.exists(ASTRO_CACHE_JSON):
        return None
    try:
        with open(ASTRO_CACHE_JSON, 'r') as f:
            return json.load(f)
    except Exception:
        return None


def save_astro_cache(data: dict):
    try:
        with open(ASTRO_CACHE_JSON, 'w') as f:
            json.dump(data, f)
    except Exception:
        pass


def get_astro_data(env: dict, verbose=False):
    # Cache policy: 6h TTL; refresh at local midnight
    now = now_tz(env.get('TZ'))
    cache = load_astro_cache()
    def fresh(cache):
        if not cache:
            return False
        ts = cache.get('_ts')
        if not ts:
            return False
        t = datetime.fromtimestamp(ts, tz=now.tzinfo)
        # midnight boundary
        midnight = now.replace(hour=0, minute=0, second=0, microsecond=0)
        if t < midnight and now >= midnight:
            return False
        return (now - t) < timedelta(hours=6)

    if not fresh(cache):
        data = fetch_openweather(env, verbose=verbose)
        if data:
            data['_ts'] = int(now.timestamp())
            save_astro_cache(data)
            cache = data
        elif not cache:
            # synthesize fallback
            sr = now.replace(hour=6, minute=30, second=0, microsecond=0)
            ss = now.replace(hour=18, minute=30, second=0, microsecond=0)
            cache = {
                '_ts': int(now.timestamp()),
                'fallback': True,
                'daily': [{
                    'sunrise': int(sr.timestamp()),
                    'sunset': int(ss.timestamp()),
                    'moon_phase': None,
                }]
            }
    return cache


def extract_times(now: datetime, data: dict):
    # Returns dict with sunrise, sunset (today), next_sunrise (tomorrow), moon_phase float or None
    tz = now.tzinfo
    daily = data.get('daily', [])
    if not daily:
        return None
    # Find today's entry; OpenWeather returns multiple days, first is today
    today = daily[0]
    sunrise = datetime.fromtimestamp(today['sunrise'], tz=timezone.utc).astimezone(tz)
    sunset = datetime.fromtimestamp(today['sunset'], tz=timezone.utc).astimezone(tz)
    moon_phase = today.get('moon_phase')  # 0 new .. 1 new
    # Get next day sunrise as night end
    if len(daily) > 1:
        tomorrow = daily[1]
        next_sunrise = datetime.fromtimestamp(tomorrow['sunrise'], tz=timezone.utc).astimezone(tz)
    else:
        # approximate +1 day
        next_sunrise = sunrise + timedelta(days=1)
    return {
        'sunrise': sunrise,
        'sunset': sunset,
        'next_sunrise': next_sunrise,
        'moon_phase': moon_phase,
    }


def clamp(x, a, b):
    return a if x < a else b if x > b else x


def smoothstep(x: float):
    x = clamp(x, 0.0, 1.0)
    return x * x * (3 - 2 * x)


def compute_arch_xy(t: float, W: int, H: int, arc_height: float, margins=(80, 80)):
    # Linear x across screen with margins; y is sinusoidal arch peaking at mid
    left, right = margins[0], margins[1]
    x = left + (W - left - right) * clamp(t, 0.0, 1.0)
    y_base = H * 0.62
    y = y_base - arc_height * math.sin(math.pi * clamp(t, 0.0, 1.0))
    return int(round(x)), int(round(y))


def classify_phase(now: datetime, sunrise: datetime, sunset: datetime, twilight_minutes: int):
    # Determine phase: 'night' 'dawn' 'day' 'dusk'
    dawn_start = sunrise - timedelta(minutes=twilight_minutes)
    dawn_end = sunrise + timedelta(minutes=twilight_minutes)
    dusk_start = sunset - timedelta(minutes=twilight_minutes)
    dusk_end = sunset + timedelta(minutes=twilight_minutes)

    if now < dawn_start or now >= dusk_end:
        return 'night'
    if dawn_start <= now < dawn_end:
        return 'dawn'
    if dusk_start <= now < dusk_end:
        return 'dusk'
    return 'day'


def tint_spec(hexrgb: str, strength: float):
    s = clamp(strength, 0.0, 1.0)
    return f"{hexrgb}:{s:.2f}"


def compute_tints(phase: str, minutes_to_edge: float, moon_alt_frac: float, twilight_minutes: int):
    # Return sky_tint_spec, bld_tint_spec
    if phase == 'day':
        return 'none', 'none'
    if phase in ('dawn', 'dusk'):
        # warm tint ramp over the configured twilight window
        denom = max(1.0, float(twilight_minutes))
        k = smoothstep(1.0 - clamp(abs(minutes_to_edge), 0.0, denom)/denom)
        return tint_spec('#ffb566', 0.35 * k), tint_spec('#ffd39e', 0.25 * k)
    # night
    base_sky = 0.12
    base_bld = 0.18
    # Moon adds coolness proportional to altitude fraction (0..1)
    bld = clamp(base_bld + 0.10 * moon_alt_frac, 0.0, 0.35)
    return tint_spec('#7aa5ff', base_sky), tint_spec('#6b87c8', bld)


def discover_layers(example_dir: str, overlays=('sun_overlay.png', 'moon_overlay.png'), sky_regex=None, bld_regex=None, verbose=False):
    # Returns dict: { 'sun_id', 'moon_id', 'sky_ids':[], 'bld_ids':[] }
    out = {'sun_id': None, 'moon_id': None, 'sky_ids': [], 'bld_ids': []}
    res = run_ctl(['list', '--json'])
    if res.returncode != 0:
        return out
    try:
        layers = json.loads(res.stdout)
    except Exception:
        return out
    import re
    sky_re = re.compile(sky_regex or r"/(1|2|3|4)\.png$")
    bld_re = re.compile(bld_regex or r"/(5|6|7|8|9|10)\.png$")
    base_abs = os.path.abspath(SCRIPT_DIR)
    for L in layers:
        path = L.get('path') or ''
        lid = L.get('id')
        if not path or lid is None:
            continue
        if any(x in path for x in overlays):
            if overlays[0] in path:
                out['sun_id'] = lid
            if overlays[1] in path:
                out['moon_id'] = lid
            continue
        # Match only files within this example directory
        if (example_dir in path) or (base_abs in path):
            if sky_re.search(path):
                out['sky_ids'].append(lid)
            elif bld_re.search(path):
                out['bld_ids'].append(lid)
    if verbose:
        print(f"[layers] sun_id={out['sun_id']} moon_id={out['moon_id']} sky={out['sky_ids']} bld={out['bld_ids']}")
    return out


def add_overlay_layer(path: str, z: int, verbose=False):
    args = ['add', path, f'z={z}', 'opacity=1.0', 'shift_multiplier=0.0', 'fit=cover']
    res = run_ctl(args)
    if res.returncode != 0:
        if verbose:
            print(f"[ipc] add failed: {res.stderr.strip()}")
        return None
    # parse id
    lid = None
    if 'Layer added with ID:' in res.stdout:
        try:
            lid = int(res.stdout.split('ID:')[1].strip())
        except Exception:
            lid = None
    if verbose:
        print(f"[ipc] added layer {lid} -> {path}")
    return lid


def modify_layer(lid: int, prop: str, value: str, verbose=False):
    res = run_ctl(['modify', str(lid), prop, value])
    ok = res.returncode == 0
    if verbose and not ok:
        print(f"[ipc] modify {lid} {prop}={value} failed: {res.stderr.strip()}")
    return ok


def main():
    ensure_dirs()
    parser = argparse.ArgumentParser(description='Dynamic Sun/Moon controller for pixel-city-advanced')
    parser.add_argument('--interval', type=int, default=120, help='Base polling interval seconds')
    parser.add_argument('--once', action='store_true', help='Run once and exit')
    parser.add_argument('--sun-size', type=int, default=260, help='Sun diameter in px')
    parser.add_argument('--moon-size', type=int, default=180, help='Moon diameter in px')
    parser.add_argument('--arc-height-day', type=float, default=580.0, help='Day arch height in px')
    parser.add_argument('--arc-height-night', type=float, default=500.0, help='Night arch height in px')
    parser.add_argument('--twilight-minutes', type=int, default=45, help='Dawn/dusk window in minutes')
    parser.add_argument('--sun-z', type=int, default=5, help='Z-index for sun overlay')
    parser.add_argument('--moon-z', type=int, default=5, help='Z-index for moon overlay')
    parser.add_argument('--dry-run', action='store_true', help='Do not call hyprlax; compute and log only')
    parser.add_argument('--at', type=str, default=None, help='Simulate a specific time (ISO 8601 or HH:MM[:SS])')
    parser.add_argument('--demo', type=str, choices=['dawn','dusk','day','night'], default=None, help='Demo mode: loop a window quickly')
    parser.add_argument('--demo-seconds', type=int, default=120, help='Seconds to complete a full window in demo mode')
    parser.add_argument('--verbose', '-v', action='store_true')
    parser.add_argument('--sky-regex', type=str, default=None, help='Regex to select sky layers from list --json')
    parser.add_argument('--bld-regex', type=str, default=None, help='Regex to select building layers from list --json')
    args = parser.parse_args()

    env = load_secrets()
    now = now_tz(env.get('TZ'))
    if args.verbose:
        print(f"[time] now={now.isoformat()}")

    # Generate demo SVGs if missing; cache sprites as needed later
    sun_svg = os.path.join(ASSETS_DIR, 'sun.svg')
    if not os.path.exists(sun_svg):
        try:
            generate_sun_svg(sun_svg, max(200, args.sun_size))
        except Exception:
            pass
    moon_svg_dir = os.path.join(ASSETS_DIR, 'moon_svgs')
    if not os.path.exists(moon_svg_dir) or not os.listdir(moon_svg_dir):
        try:
            generate_moon_svgs(moon_svg_dir, diameter=max(200, args.moon_size))
        except Exception:
            pass

    astro = get_astro_data(env, verbose=args.verbose)
    times = extract_times(now, astro) if astro else None
    if not times:
        print("Failed to obtain sunrise/sunset; aborting", file=sys.stderr)
        sys.exit(1)

    W, H = get_mon_geometry()
    if args.verbose:
        print(f"[mon] geometry: {W}x{H}")

    # State for gating
    last = {
        'sun_xy': None,
        'moon_xy': None,
        'sky_tint': None,
        'bld_tint': None,
    }

    # One-time layer discovery/creation
    overlays = ('sun_overlay.png', 'moon_overlay.png')
    overlay_paths = { 'sun': os.path.join(TMP_DIR, overlays[0]), 'moon': os.path.join(TMP_DIR, overlays[1]) }

    def ensure_layers():
        if args.dry_run:
            return {'sun_id': 0, 'moon_id': 0, 'sky_ids': [], 'bld_ids': []}
        L = discover_layers('examples/pixel-city-advanced', overlays=overlays, sky_regex=args.sky_regex, bld_regex=args.bld_regex, verbose=args.verbose)
        if L['sun_id'] is None:
            L['sun_id'] = add_overlay_layer(overlay_paths['sun'], args.sun_z, verbose=args.verbose)
        if L['moon_id'] is None:
            L['moon_id'] = add_overlay_layer(overlay_paths['moon'], args.moon_z, verbose=args.verbose)
        return L

    # Ensure placeholder overlays exist so initial add succeeds
    for k in ('sun', 'moon'):
        p = overlay_paths[k]
        if not os.path.exists(p):
            placeholder = Image.new('RGBA', (W, H), (0, 0, 0, 0))
            atomic_write(placeholder, p)
    layers = ensure_layers()

    # Time providers
    fixed_now = None
    if args.at:
        fixed_now = parse_at_time(args.at, env.get('TZ'))
        if not fixed_now:
            print(f"Invalid --at value: {args.at}", file=sys.stderr)
            sys.exit(2)
        if args.verbose:
            print(f"[sim] using fixed time: {fixed_now.isoformat()}")

    get_demo_now = None
    if args.demo:
        sr, ss, nsr = times['sunrise'], times['sunset'], times['next_sunrise']
        tw = timedelta(minutes=args.twilight_minutes)
        if args.demo == 'dawn':
            win_start, win_end = sr - tw, sr + tw
        elif args.demo == 'dusk':
            win_start, win_end = ss - tw, ss + tw
        elif args.demo == 'day':
            win_start, win_end = sr, ss
        else:  # night
            win_start, win_end = ss, nsr
        window_len = max(1.0, (win_end - win_start).total_seconds())
        demo_total = max(1.0, float(args.demo_seconds))
        t0 = time.monotonic()
        if args.verbose:
            print(f"[demo] window {args.demo}: {win_start} → {win_end} (len={window_len:.1f}s) over {demo_total:.1f}s real time")

        def _demo_now():
            elapsed = time.monotonic() - t0
            frac = (elapsed / demo_total) % 1.0
            return win_start + timedelta(seconds=frac * window_len)

        get_demo_now = _demo_now

    def compute_once(now_dt):
        sr = times['sunrise']
        ss = times['sunset']
        nsr = times['next_sunrise']
        phase = classify_phase(now_dt, sr, ss, args.twilight_minutes)
        # t_day and t_night
        if sr <= now_dt <= ss:
            t_day = (now_dt - sr) / (ss - sr)
            t_night = None
        else:
            # night across sunset->next_sunrise
            if now_dt >= ss:
                duration = nsr - ss
                t_night = (now_dt - ss) / duration
            else:
                # before sunrise (after previous day sunset): approximate t near end of night
                duration = sr - (ss - timedelta(days=1))
                t_night = (now_dt - (ss - timedelta(days=1))) / duration
            t_day = None

        # Moon phase 0..1 (0 new, 0.5 full). Hide near new (~<0.05 or >0.95)
        moon_phase = times.get('moon_phase')
        moon_visible = (moon_phase is not None) and (0.05 <= moon_phase <= 0.95)
        sun_visible = (t_day is not None)

        # compute arch positions
        sun_xy = None
        moon_xy = None
        if sun_visible:
            x, y = compute_arch_xy(float(t_day), W, H, args.arc_height_day)
            sun_xy = (x, y)
        else:
            if t_night is not None:
                x, y = compute_arch_xy(float(t_night), W, H, args.arc_height_night)
                moon_xy = (x, y)
        # moon altitude fraction approx by sin(pi * t_night)
        moon_alt_frac = 0.0
        if moon_xy is not None and t_night is not None:
            moon_alt_frac = max(0.0, math.sin(math.pi * float(t_night)))

        # minutes to nearest edge among {sunrise, sunset, next_sunrise}
        edges = [sr, ss, nsr]
        minutes_to_edge = min(abs((now_dt - e).total_seconds())/60.0 for e in edges)

        sky_tint, bld_tint = compute_tints(phase, minutes_to_edge, moon_alt_frac, args.twilight_minutes)

        return {
            'phase': phase,
            'sun_visible': sun_visible,
            'moon_visible': moon_visible and (not sun_visible),  # never both visible
            'sun_xy': sun_xy,
            'moon_xy': moon_xy,
            'sky_tint': sky_tint,
            'bld_tint': bld_tint,
        }

    def draw_overlay(kind, xy, size, phase01):
        # compose full-frame transparent PNG with sprite centered at xy
        img = Image.new('RGBA', (W, H), (0,0,0,0))
        if kind == 'sun':
            sprite_path = ensure_sun_sprite(size)
        else:
            sprite_path = ensure_moon_sprite(size, phase01 if phase01 is not None else 0.5)
        spr = Image.open(sprite_path).convert('RGBA')
        sx, sy = xy
        px = int(round(sx - spr.width / 2))
        py = int(round(sy - spr.height / 2))
        img.alpha_composite(spr, (px, py))
        atomic_write(img, overlay_paths[kind])

    def apply_tints(sky_spec: str, bld_spec: str):
        if args.dry_run:
            return True
        ok = True
        for lid in layers['sky_ids']:
            ok &= modify_layer(lid, 'tint', sky_spec, verbose=args.verbose)
        for lid in layers['bld_ids']:
            ok &= modify_layer(lid, 'tint', bld_spec, verbose=args.verbose)
        return ok

    def refresh_overlay(kind: str, lid: int):
        if args.dry_run:
            return True
        return modify_layer(lid, 'path', overlay_paths[kind], verbose=args.verbose)

    # Main loop (or single run)
    iteration = 0
    pos_eps = 3 if args.demo else 10
    try:
        while True:
            if get_demo_now:
                now = get_demo_now()
            elif fixed_now is not None:
                now = fixed_now
            else:
                now = now_tz(env.get('TZ'))
            state = compute_once(now)
            if args.verbose:
                print(f"[state] {now.isoformat()} phase={state['phase']} sun={state['sun_xy']} moon={state['moon_xy']} sky_tint={state['sky_tint']} bld_tint={state['bld_tint']}")

            # Draw + IPC with gating
            # Tint gating
            tint_changed = (state['sky_tint'] != last['sky_tint']) or (state['bld_tint'] != last['bld_tint'])

            # Sun
            if state['sun_visible'] and state['sun_xy']:
                pos_changed = (last['sun_xy'] is None) or (max(abs(state['sun_xy'][0] - (last['sun_xy'][0] if last['sun_xy'] else 0)),
                                                            abs(state['sun_xy'][1] - (last['sun_xy'][1] if last['sun_xy'] else 0))) > pos_eps)
                if pos_changed:
                    draw_overlay('sun', state['sun_xy'], args.sun_size, None)
                    if not args.dry_run and layers['sun_id']:
                        refresh_overlay('sun', layers['sun_id'])
                # Show/hide
                if not args.dry_run and layers['moon_id']:
                    modify_layer(layers['moon_id'], 'visible', 'false', verbose=args.verbose)
                if not args.dry_run and layers['sun_id']:
                    modify_layer(layers['sun_id'], 'visible', 'true', verbose=args.verbose)
            else:
                # hide sun
                if not args.dry_run and layers['sun_id']:
                    modify_layer(layers['sun_id'], 'visible', 'false', verbose=args.verbose)

            # Moon
            if state['moon_visible'] and state['moon_xy']:
                pos_changed = (last['moon_xy'] is None) or (max(abs(state['moon_xy'][0] - (last['moon_xy'][0] if last['moon_xy'] else 0)),
                                                            abs(state['moon_xy'][1] - (last['moon_xy'][1] if last['moon_xy'] else 0))) > pos_eps)
                if pos_changed:
                    phase01 = times.get('moon_phase') if times else 0.5
                    draw_overlay('moon', state['moon_xy'], args.moon_size, phase01)
                    if not args.dry_run and layers['moon_id']:
                        refresh_overlay('moon', layers['moon_id'])
                # visibility
                if not args.dry_run and layers['sun_id']:
                    modify_layer(layers['sun_id'], 'visible', 'false', verbose=args.verbose)
                if not args.dry_run and layers['moon_id']:
                    modify_layer(layers['moon_id'], 'visible', 'true', verbose=args.verbose)
            else:
                if not args.dry_run and layers['moon_id']:
                    modify_layer(layers['moon_id'], 'visible', 'false', verbose=args.verbose)

            # Tints
            if tint_changed:
                apply_tints(state['sky_tint'], state['bld_tint'])

            # Update gating state
            last['sun_xy'] = state['sun_xy']
            last['moon_xy'] = state['moon_xy']
            last['sky_tint'] = state['sky_tint']
            last['bld_tint'] = state['bld_tint']

            if args.once or (fixed_now is not None and not args.demo):
                break

            # Cadence control: demo fixed tick; otherwise dynamic
            if get_demo_now:
                sleep_s = 0.5
            else:
                phase = state['phase']
                if phase in ('dawn', 'dusk'):
                    sleep_s = min(60, max(30, args.interval // 2))
                elif phase == 'day':
                    sleep_s = max(60, args.interval)
                else:  # night
                    sleep_s = max(120, args.interval + 180)
            if args.verbose:
                print(f"[sleep] {sleep_s}s")
            time.sleep(sleep_s)
            iteration += 1

    except KeyboardInterrupt:
        print("\nStopping dynamic sky...")
        # Keep layers by default; could add a flag to remove


if __name__ == '__main__':
    main()
