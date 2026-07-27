#!/usr/bin/env python3
# [rc4l] Drives the engine down both render paths to the same viewpoint and captures a screenshot
# from each, so core-vs-compat parity can be measured with compare_shots.py instead of eyeballed.
import argparse
import glob
import json
import os
import shutil
import socket
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = os.path.join(REPO, "build", "ZandroX.app", "Contents", "MacOS", "zandronum")


def send_raw(port, payload, timeout=5.0):
    """Send one NDJSON message over the MCP bridge socket."""
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
        # [rc4l] The bridge applies messages on the next frame; give it one before disconnecting.
        time.sleep(0.5)


def send(port, text, timeout=5.0):
    """Send one console command over the MCP bridge."""
    send_raw(port, {"text": text}, timeout)


def set_paused(port, paused):
    """Freeze or resume the simulation."""
    send_raw(port, {"t": "setpause", "paused": 1 if paused else 0})


def wait_for_port(port, deadline):
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1.0):
                return True
        except OSError:
            time.sleep(0.5)
    return False


def capture(path_name, hwrender, args, shotdir):
    """Launch one engine instance, screenshot the scene, return the PNG path."""
    os.makedirs(shotdir, exist_ok=True)
    for stale in glob.glob(os.path.join(shotdir, "*.png")):
        os.remove(stale)

    env = dict(os.environ)
    env["ZANDRONUM_BRIDGE_PORT"] = str(args.port)

    cmd = [ENGINE, "-iwad", args.iwad, "+map", args.map,
           "-width", str(args.width), "-height", str(args.height),
           "+set", "vid_hwrender", "1" if hwrender else "0",
           "+set", "screenshot_dir", shotdir,
           # [rc4l] Ignore the mouse entirely. Stray physical mouse movement during a capture
           # rotates the view, so the two runs would frame different angles and the comparison
           # would report a difference that has nothing to do with the renderer.
           "+set", "use_mouse", "0",
           "+set", "m_noprescale", "1",
           "+set", "sv_cheats", "1"]
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not wait_for_port(args.port, time.time() + args.timeout):
            raise RuntimeError("%s: bridge never opened on port %d" % (path_name, args.port))

        # [rc4l] Freeze the simulation the moment the bridge is up. Without this the two runs drift:
        # they reach the map at slightly different times, so actors and the player have moved and the
        # comparison measures divergent world state rather than renderer difference.
        set_paused(args.port, True)
        time.sleep(args.settle)
        send(args.port, "screenshot")
        time.sleep(1.5)

        shots = sorted(glob.glob(os.path.join(shotdir, "*.png")), key=os.path.getmtime)
        if not shots:
            raise RuntimeError("%s: no screenshot was written to %s" % (path_name, shotdir))
        return shots[-1]
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()


def main():
    ap = argparse.ArgumentParser(description="Capture the same scene on both render paths.")
    ap.add_argument("--iwad", default="doom2.wad")
    ap.add_argument("--map", default="MAP01")
    ap.add_argument("--width", type=int, default=800)
    ap.add_argument("--height", type=int, default=600)
    ap.add_argument("--port", type=int, default=7791, help="bridge port (avoid the MCP's own)")
    ap.add_argument("--settle", type=float, default=6.0, help="seconds to wait before capturing")
    ap.add_argument("--timeout", type=float, default=60.0, help="seconds to wait for the bridge")
    ap.add_argument("--outdir", default=os.path.join(REPO, "build", "ab"))
    args = ap.parse_args()

    if not os.path.exists(ENGINE):
        sys.exit("engine not found at %s -- run ./build.sh first" % ENGINE)

    os.makedirs(args.outdir, exist_ok=True)
    results = {}
    for name, hw in (("compat", False), ("core", True)):
        shot = capture(name, hw, args, os.path.join(args.outdir, "raw_" + name))
        dest = os.path.join(args.outdir, "%s_%s.png" % (args.map.lower(), name))
        shutil.copyfile(shot, dest)
        results[name] = dest
        print("%-7s -> %s" % (name, dest))

    print("\ncompare with:")
    print("  python3 tools/compare_shots.py %s %s" % (results["compat"], results["core"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
