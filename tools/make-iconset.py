#!/usr/bin/env python3
# Pad a (possibly non-square) source image onto transparent square canvases and
# emit the standard macOS .iconset PNGs.  build.sh then runs `iconutil` to turn
# the directory into a .icns.  Best-effort: build.sh skips the icon if this
# fails for any reason, so the app still builds without one.
import sys
from PIL import Image

def main(src_path, iconset_dir):
    img = Image.open(src_path)
    # A Windows .ico packs several sizes; decode the largest square frame.
    if getattr(img, "format", None) == "ICO":
        img.size = max(img.ico.sizes())
        img.load()
    src = img.convert("RGBA")
    # Standard iconset entries: (px size, filename).  Each logical size needs a
    # 1x and a 2x ("@2x") variant so Retina displays get a crisp icon.
    entries = []
    for pt in (16, 32, 128, 256, 512):
        entries.append((pt, f"icon_{pt}x{pt}.png"))
        entries.append((pt * 2, f"icon_{pt}x{pt}@2x.png"))

    for px, name in entries:
        # Fit the logo within ~90% of the canvas, centered, rest transparent.
        target = int(px * 0.90)
        scale = min(target / src.width, target / src.height)
        w, h = max(1, round(src.width * scale)), max(1, round(src.height * scale))
        logo = src.resize((w, h), Image.LANCZOS)
        canvas = Image.new("RGBA", (px, px), (0, 0, 0, 0))
        canvas.paste(logo, ((px - w) // 2, (px - h) // 2), logo)
        canvas.save(f"{iconset_dir}/{name}")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
