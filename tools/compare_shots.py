#!/usr/bin/env python3
# [rc4l] Compares two renderer screenshots and reports how far apart they are, so core-vs-compat
# parity is evidence rather than an impression. Exits non-zero when the difference exceeds the
# threshold, making it usable as a gate.
import argparse
import sys

try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit("compare_shots.py needs Pillow: pip3 install Pillow")


def compare(path_a, path_b, tolerance):
    """Return (differing_fraction, mean_abs_error, max_channel_delta)."""
    a = Image.open(path_a).convert("RGB")
    b = Image.open(path_b).convert("RGB")
    if a.size != b.size:
        raise ValueError("size mismatch: %s is %s, %s is %s" % (path_a, a.size, path_b, b.size))

    diff = ImageChops.difference(a, b)
    raw = diff.tobytes()
    total = len(raw) // 3
    if total == 0:
        return 0.0, 0.0, 0

    differing = 0
    error_sum = 0
    worst = 0
    for i in range(0, len(raw), 3):
        r, g, bl = raw[i], raw[i + 1], raw[i + 2]
        peak = r if r > g else g
        if bl > peak:
            peak = bl
        if peak > tolerance:
            differing += 1
        error_sum += r + g + bl
        if peak > worst:
            worst = peak

    return differing / total, error_sum / (total * 3.0), worst


def main():
    ap = argparse.ArgumentParser(description="Compare two renderer screenshots.")
    ap.add_argument("before")
    ap.add_argument("after")
    # [rc4l] Per-channel slack; GL drivers differ in the last bit or two even when rendering agrees.
    ap.add_argument("--tolerance", type=int, default=2,
                    help="per-channel delta a pixel may differ by and still count as matching")
    ap.add_argument("--max-differing", type=float, default=0.0,
                    help="fraction of pixels allowed to differ before failing (0 = any difference fails)")
    args = ap.parse_args()

    fraction, mae, worst = compare(args.before, args.after, args.tolerance)
    print("differing pixels: %.4f%%" % (fraction * 100.0))
    print("mean abs error:   %.3f" % mae)
    print("worst channel:    %d" % worst)

    if fraction > args.max_differing:
        print("FAIL: %.4f%% of pixels differ (allowed %.4f%%)"
              % (fraction * 100.0, args.max_differing * 100.0))
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
