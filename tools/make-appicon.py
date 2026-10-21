"""Make the Windows application icon from the master picture.

Usage: python tools/make-appicon.py [master.png] [out.ico]

[rc4l] The master is src/zandronum/wadsrc/static/fua_appicon.png, which the engine also reads at run time
for the Linux window icon and mac_compile.sh turns into the macOS .icns; the .ico it writes is compiled into
the Windows executable by win32/zdoom.rc and the server console's own resources.
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASTER = os.path.join(ROOT, "src", "zandronum", "wadsrc", "static", "fua_appicon.png")
OUT = os.path.join(ROOT, "src", "zandronum", "src", "win32", "zandronum.ico")
# [rc4l] Every size Windows asks an icon for, from the taskbar's 16 to the 256 a large-icon view shows.
SIZES = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]


def main(master=MASTER, out=OUT):
    img = Image.open(master).convert("RGBA")
    img.save(out, format="ICO", sizes=SIZES)
    print(f"wrote {out} from {master}")


if __name__ == "__main__":
    main(*sys.argv[1:3])
