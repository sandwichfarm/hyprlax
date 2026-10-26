#!/usr/bin/env python3
"""Reproduce #106 on an isolated, headless Hyprland (no desktop changes).

Requires Hyprland, hyprctl, Wayfire, grim, and a working Mesa render node.
Example: scripts/test_scaled_output.py --render-node /dev/dri/renderD129
Use --binary /path/to/baseline/hyprlax --scale 2 --single-layer to reproduce
against an older binary. Screenshots and daemon logs are kept in --artifacts
(or a new temporary directory). No Python packages are required.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import tempfile
import time
import zlib


WIDTH, HEIGHT = 2880, 1800
OUTPUT = "HEADLESS-1"


def write_fixture(path, overlay=False):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(
            ">I", zlib.crc32(kind + data) & 0xFFFFFFFF
        )

    rows = []
    for y in range(HEIGHT):
        if overlay:
            row = b"\0\0\0\0" * WIDTH
            if 1400 <= y < 1520:
                row = (b"\0\0\0\0" * 2000 + b"\xff\xff\xff\xff" * 120
                       + b"\0\0\0\0" * (WIDTH - 2120))
        else:
            colors = [b"\xff\x50\x28\xff", b"\x1e\xa0\xf0\xff"]
            row = b"".join(colors[(x // 120 + y // 120) % 2] * 120
                           for x in range(0, WIDTH, 120))
        rows.append(b"\0" + row)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")
    )


def tile_spacing(path):
    data = path.read_bytes()
    header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not header:
        raise RuntimeError(f"Unexpected screenshot format: {path}")
    width, height = map(int, header.groups())
    if (width, height) != (WIDTH, HEIGHT):
        raise RuntimeError(f"Unexpected screenshot size: {width}x{height}")
    pixels = data[header.end():]
    spacings = []
    for vertical in (False, True):
        points = []
        for position in range(height if vertical else width):
            x, y = (100, position) if vertical else (position, 100)
            offset = (y * width + x) * 3
            points.append(pixels[offset] > pixels[offset + 2])
        edges = [i for i in range(1, len(points)) if points[i] != points[i - 1]]
        if len(edges) < 4:
            raise RuntimeError(f"Checkerboard missing from screenshot: {path}")
        spacings.append(statistics.median(b - a for a, b in zip(edges, edges[1:])))
    return spacings


def stop(process):
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path,
                        default=Path(__file__).resolve().parents[1] / "hyprlax")
    parser.add_argument("--render-node", required=True,
                        help="Mesa DRM render node for the headless parent (no KMS)")
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--scale", action="append", type=float)
    parser.add_argument("--single-layer", action="store_true")
    args = parser.parse_args()
    for tool in ("Hyprland", "hyprctl", "wayfire", "grim"):
        if not shutil.which(tool):
            parser.error(f"Required tool not found: {tool}")
    binary = args.binary.resolve()
    if not os.access(binary, os.X_OK):
        parser.error(f"Binary is not executable: {binary}")
    artifacts = args.artifacts or Path(tempfile.mkdtemp(prefix="hyprlax-scale-"))
    artifacts = artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    print(f"Artifacts: {artifacts}", flush=True)
    write_fixture(artifacts / "checker.png")
    write_fixture(artifacts / "overlay.png", overlay=True)
    (artifacts / "wayfire.ini").write_text("[core]\nplugins = ipc\nxwayland = false\n")
    (artifacts / "hyprland.conf").write_text(
        f"monitor = {OUTPUT},{WIDTH}x{HEIGHT}@60,0x0,2\n"
        "misc:disable_hyprland_logo = true\nmisc:disable_splash_rendering = true\n"
        "animations:enabled = false\nxwayland:enabled = false\n"
    )
    base_config = (f'[[global.layers]]\npath = "{artifacts}/checker.png"\n'
                   'fit = "cover"\ncontent_scale = 0.5\nscale = 0.5\n')
    parent = compositor = daemon = None
    logs = []
    # Keep the runtime path short enough for Hyprland's Unix socket paths.
    with tempfile.TemporaryDirectory(prefix="h") as runtime:
        env = os.environ.copy()
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "HYPRLAND_INSTANCE_SIGNATURE"):
            env.pop(key, None)
        env["XDG_RUNTIME_DIR"] = runtime
        mesa_vendor = Path("/usr/share/glvnd/egl_vendor.d/50_mesa.json")
        if mesa_vendor.exists():
            env["__EGL_VENDOR_LIBRARY_FILENAMES"] = str(mesa_vendor)

        def run(command, **kwargs):
            return subprocess.check_output(command, env=env, timeout=10, **kwargs)

        def hyprctl(*arguments):
            return run(["hyprctl", *arguments], text=True)

        def launch(command, logfile, extra_env=None):
            log = (artifacts / logfile).open("w")
            logs.append(log)
            return subprocess.Popen(command, env=extra_env or env, stdout=log, stderr=log)

        def wait_for(predicate, process):
            for _ in range(100):
                if process.poll() is not None:
                    raise RuntimeError("Process exited during startup; see artifact logs")
                if predicate():
                    return
                time.sleep(0.1)
            raise RuntimeError("Startup timed out; see artifact logs")

        def capture(name):
            path = artifacts / f"{name}.ppm"
            run(["grim", "-t", "ppm", "-o", OUTPUT, str(path)])
            return tile_spacing(path)

        def check_switch(name, workspace):
            before = capture(name + "-before")
            hyprctl("dispatch", "workspace", str(workspace))
            time.sleep(1.5)
            after = capture(name + "-after")
            print(f"{name}: checker spacing {before} -> {after}", flush=True)
            if any(abs(a - b) > 1 for a, b in zip(before, after)):
                raise RuntimeError(f"Wallpaper changed scale during workspace switch: {name}")
            if any(abs(size - 120) > 1 for size in after):
                raise RuntimeError(f"Wallpaper no longer matches the output pixel size: {name}")
            run([str(binary), "ctl", "status"])

        try:
            parent_env = env | {
                "WLR_BACKENDS": "headless", "WLR_HEADLESS_OUTPUTS": "1",
                "WLR_RENDERER": "gles2", "WLR_RENDER_DRM_DEVICE": args.render_node,
            }
            parent = launch(["wayfire", "-c", str(artifacts / "wayfire.ini")],
                            "wayfire.log", parent_env)
            wait_for(lambda: bool(list(Path(runtime).glob("wayland-[0-9]"))), parent)
            env.update(WAYLAND_DISPLAY=list(Path(runtime).glob("wayland-[0-9]"))[0].name,
                       AQ_DRM_DEVICES="/dev/null", AQ_NO_KMS_REQUIREMENT="1",
                       LIBSEAT_BACKEND="noop", HYPRLAND_NO_RT="1", HYPRLAND_NO_SD_NOTIFY="1")
            compositor = launch(["Hyprland", "-c", str(artifacts / "hyprland.conf")],
                                "hyprland.log")
            wait_for(lambda: bool(list(Path(runtime).glob("hypr/*/.socket.sock"))), compositor)
            instance = json.loads(hyprctl("instances", "-j"))[0]
            env.update(HYPRLAND_INSTANCE_SIGNATURE=instance["instance"],
                       WAYLAND_DISPLAY=instance["wl_socket"], HYPRLAX_SOCKET_SUFFIX="t")
            (artifacts / "version.json").write_text(hyprctl("version", "-j"))
            hyprctl("output", "create", "headless")
            for layers in ([1] if args.single_layer else [1, 2]):
                config = base_config
                if layers == 2:
                    config += (f'\n[[global.layers]]\npath = "{artifacts}/overlay.png"\n'
                               'fit = "cover"\nopacity = 0.7\nshift_multiplier = 1.5\n')
                config_path = artifacts / f"layers-{layers}.toml"
                config_path.write_text(config)
                for scale in args.scale or [1, 2, 1.5]:
                    name = f"layers-{layers}-scale-{scale:g}"
                    hyprctl("keyword", "monitor", f"{OUTPUT},{WIDTH}x{HEIGHT}@60,0x0,{scale}")
                    hyprctl("dispatch", "workspace", "1")
                    time.sleep(0.3)
                    daemon = launch([str(binary), "--debug", "--config", str(config_path)],
                                    name + ".log")
                    wait_for(lambda: bool(list(Path(runtime).glob("hyprlax-*.sock"))), daemon)
                    time.sleep(0.5)
                    check_switch(name, 2)
                    if scale == 1.5:
                        for index, new_scale in enumerate([2, 1.5, 1]):
                            hyprctl("keyword", "monitor",
                                    f"{OUTPUT},{WIDTH}x{HEIGHT}@60,0x0,{new_scale}")
                            # Screencopy requests drive frames on an idle headless output.
                            # Wait for the compositor/client scale exchange to settle.
                            stable_frames = 0
                            for attempt in range(20):
                                time.sleep(0.25)
                                try:
                                    spacing = capture(f"{name}-configure-{new_scale}")
                                except RuntimeError:
                                    stable_frames = 0
                                    continue
                                stable_frames = (stable_frames + 1 if all(
                                    abs(size - 120) <= 1 for size in spacing) else 0)
                                if stable_frames >= 3:
                                    break
                            else:
                                raise RuntimeError(f"Scale transition did not settle: {new_scale}")
                            check_switch(f"{name}-transition-{new_scale}", index + 3)
                    stop(daemon)
                    daemon = None
            print("PASS: scale remains stable across workspace switches and scale transitions")
        finally:
            stop(daemon)
            stop(compositor)
            stop(parent)
            for log in logs:
                log.close()


if __name__ == "__main__":
    main()
