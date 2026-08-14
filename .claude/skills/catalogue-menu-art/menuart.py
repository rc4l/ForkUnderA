# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

"""Pull the menu art out of a set of game archives, at a size and a byte budget a UI can afford.

Given the files an experience loads, in load order, this finds the picture that identifies it, and
writes a small PNG. The point is to ship the identity of a pack without shipping the pack: the art
is a kilobyte, the archives it came from are hundreds of megabytes.

Run --help for usage. See SKILL.md for when to run it and what to do with the output.
"""

import argparse
import io
import json
import os
import re
import struct
import sys
import zipfile

from PIL import Image, ImageFile

# Mods ship pictures the engine reads and a strict decoder will not: a truncated PNG with no IEND
# still draws in game, so refusing it here would silently cost the art of a mod that works.
ImageFile.LOAD_TRUNCATED_IMAGES = True

# What identifies a pack, best first. The logo is a logo: made to be read small, on a dark
# background, which is exactly the job here. A title screen is a whole illustration and survives the
# shrink far less well, so it is the fallback rather than the choice.
#
# [rc4l] TITLE is last because it is a plain word rather than a reserved lump name, so it is the one
# most likely to be something other than a logo. It earns its place because packs use it: Legacy of
# Darkness keeps its logo in Graphics/Misc/TITLE.png, declares no main menu for the MENUDEF route to
# read, and so came out with no art at all.
LUMPS = ("M_DOOM", "TITLEPIC")

# Archive pixels are 1.2 times as tall as they are wide. Correcting is what makes the output look
# like what a player sees rather than a squashed copy of it.
PIXEL_ASPECT = 1.2

# Fewer colours is the first thing to spend, because it costs less than resolution: a logo is flat
# colour and posterises gracefully, where a smaller logo just goes illegible.
COLOUR_STEPS = (256, 128, 64, 32, 16, 8, 4)

# [rc4l] What the art will be drawn ON, for blending its antialiased edge into.
#
# Only the part-transparent pixels use it, so it needs to be close rather than exact: an edge blended
# into a colour a level or two off the real surface is not something an eye can find. Change it if
# the surface changes, and measure the composite rather than reading one layer's colour, because a
# panel is usually several layers deep.
BACKGROUND = (7, 8, 11)

# [rc4l] The last resort, when every colour depth is still over the ceiling. A wide logo can have
# twice the pixels of an ordinary one, and no amount of posterising will save it.
SIZE_SHRINK = 0.9
SIZE_STEPS = 12

# [rc4l] The stock palette, for decoding a picture whose archive brought none of its own.
#
# Most add-ons ship no palette because the base game already supplies one, and the load order a
# caller passes usually starts with an add-on rather than the base game. Without this the tool
# simply fails on the commonest input there is, which it did.
#
# Taken from a freely redistributable base game, whose palette is the standard one by design.
# Anything shipping its own still wins: this is only reached when nothing else answered.
DEFAULT_PALETTE_HEX = (
	"0000001f170b170f074b4b4bffffff1b1b1b1313130b0b0b0707072f371f232b0f171f070f17004f3b2b4733233f"
	"2b1bffb7b7f7ababf3a3a3eb9797e78f8fdf8787db7b7bd37373cb6b6bc76363bf5b5bbb5757b34f4faf4747a73f"
	"3fa33b3b9b3333972f2f8f2b2b8b2323831f1f7f1b1b7717177313136b0f0f670b0b5f07075b07075307074f0000"
	"470000430000ffebdfffe3d3ffdbc7ffd3bbffcfb3ffc7a7ffbf9bffbb93ffb383f7ab7befa373e79b6bdf9363d7"
	"8b5bcf8353cb7f4fbf7b4bb37347ab6f43a36b3f9b633b8f5f378757337f532f774f2b6b47275f4323533f1f4b37"
	"1b3f2f17332b132b230fefefefe7e7e7dfdfdfdbdbdbd3d3d3cbcbcbc7c7c7bfbfbfb7b7b7b3b3b3abababa7a7a7"
	"9f9f9f9797979393938b8b8b8383837f7f7f7777776f6f6f6b6b6b6363635b5b5b5757574f4f4f4747474343433b"
	"3b3b3737372f2f2f27272723232377ff6f6fef6767df5f5fcf575bbf4f53af474b9f3f4393373f832f37732b2f63"
	"2327531b1f431717330f13230b0b1707bfa78fb79f87af977fa78f779f876f9b7f6b937b638b735b836b577b634f"
	"775f4b6f574367533f5f4b37574333533f2f9f83638f7753836b4b775f3f6753335b472b4f3b2343331b7b7f636f"
	"7357676b4f5b634753573b474f333f472b373f27ffff73ebdb57d7bb43c39b2faf7b1f9b5b13874307732b00ffff"
	"ffffdbdbffbbbbff9b9bff7b7bff5f5fff3f3fff1f1fff0000ef0000e30000d70000cb0000bf0000b30000a70000"
	"9b00008b00007f00007300006700005b00004f0000430000e7e7ffc7c7ffababff8f8fff7373ff5353ff3737ff1b"
	"1bff0000ff0000e30000cb0000b300009b00008300006b000053ffffffffebdbffd7bbffc79bffb37bffa35bff8f"
	"3bff7f1bf37317eb6f0fdf670fd75f0bcb5707c34f00b74700af4300ffffffffffd7ffffb3ffff8fffff6bffff47"
	"ffff23ffff00a73f009f3700932f008723004f3b27432f1b3723132f1b0b00005300004700003b00002f00002300"
	"001700000b000000ff9f43ffe74bff7bffff00ffcf00cf9f009b6f006ba76b6b"
)


class Archive(object):
	"""One game archive, asked for lumps by name."""

	def __init__(self, path):
		self.path = path
		self.zip = None
		self.entries = {}

		if zipfile.is_zipfile(path):
			self.zip = zipfile.ZipFile(path)
			for name in self.zip.namelist():
				if name.endswith("/"):
					continue
				stem = os.path.splitext(os.path.basename(name))[0].upper()
				# Last wins, the way the engine resolves a repeated name. An archive holding both a
				# low-res lump and a hi-res replacement is stating a preference by order.
				self.entries.setdefault(stem, [])
				self.entries[stem].append(name)
			return

		with open(path, "rb") as f:
			head = f.read(12)
			if len(head) < 12:
				return
			sig, count, offset = struct.unpack("<4sii", head)
			if sig not in (b"PWAD", b"IWAD"):
				return
			f.seek(offset)
			for _ in range(count):
				rec = f.read(16)
				if len(rec) < 16:
					break
				at, size, raw = struct.unpack("<ii8s", rec)
				if size <= 0:
					continue
				stem = raw.rstrip(b"\0").decode("latin-1").upper()
				self.entries.setdefault(stem, [])
				self.entries[stem].append((at, size))

	def read(self, want):
		"""Every copy of a name, in the order the archive holds them."""
		out = []
		for entry in self.entries.get(want, []):
			if self.zip is not None:
				out.append(self.zip.read(entry))
			else:
				with open(self.path, "rb") as f:
					f.seek(entry[0])
					out.append(f.read(entry[1]))
		return out


def parse_palette(data):
	if not data or len(data) < 768:
		return None
	return [tuple(data[i * 3:i * 3 + 3]) for i in range(256)]


def default_palette():
	return parse_palette(bytes.fromhex("".join(DEFAULT_PALETTE_HEX)))


def decode_patch(data, palette):
	"""The column-and-post picture format. Returns RGBA, or None if it is not one."""
	if len(data) < 8:
		return None

	width, height, _left, _top = struct.unpack("<hhhh", data[:8])
	if not (0 < width <= 4096 and 0 < height <= 4096):
		return None
	if len(data) < 8 + 4 * width:
		return None

	columns = struct.unpack("<%dI" % width, data[8:8 + 4 * width])
	img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
	px = img.load()

	for x in range(width):
		p = columns[x]
		if p >= len(data):
			return None
		while True:
			# The terminator is allowed to be the very last byte in the file, so only the marker
			# itself is required to be in range here. Demanding its length byte too rejected every
			# well-formed picture whose final column ended the lump.
			if p >= len(data):
				return None
			top = data[p]
			if top == 0xFF:
				break
			if p + 1 >= len(data):
				return None
			run = data[p + 1]
			p += 3							# past the marker and the unused padding byte
			if p + run > len(data):
				return None
			for i in range(run):
				y = top + i
				if 0 <= y < height:
					px[x, y] = palette[data[p + i]] + (255,)
			p += run + 1					# and the trailing padding byte

	return img


def decode(data, palette):
	if data[:8] == b"\x89PNG\r\n\x1a\n":
		try:
			img = Image.open(io.BytesIO(data))
			img.load()
			return img.convert("RGBA")
		except Exception:
			return None
	if palette is None:
		return None
	try:
		return decode_patch(data, palette)
	except Exception:
		return None


# [rc4l] How much of a picture may be one flat colour before it is not a picture.
#
# Archives ship blank graphics: a placeholder somebody meant to fill in, or a spacer a menu uses for
# its own layout. They decode perfectly and they say nothing, and a blank slab in place of a name is
# strictly worse than the name. Set high because real art with a large flat background is common and
# must still pass.
BLANK_FRACTION = 0.98


def informative(img):
	"""Whether there is anything in this picture worth showing."""
	flat = Image.new("RGB", img.size, (0, 0, 0))
	flat.paste(img, (0, 0), img)

	pixels = flat.size[0] * flat.size[1]
	if pixels == 0:
		return False

	colours = flat.getcolors(maxcolors=pixels)
	if not colours:
		return True						# more colours than pixels to count: certainly not blank

	return (max(n for n, _c in colours) / float(pixels)) < BLANK_FRACTION


def menu_logo(archives):
	"""What the menu definition draws at the top of the main menu, if it defines one.

	M_DOOM is a CONVENTION, not a rule. A mod that replaces the main menu outright names its own
	graphic, and then the picture a player actually sees is one this tool would never have looked
	for: the pack has a perfectly good logo and appears to have none.

	So the definition is read first and its answer preferred, because it is the one that says what
	is really on screen. Anything without one falls through to the convention.
	"""
	# Only the main menu's own graphic. A definition names several menus and the others are
	# options screens and readouts, whose art says nothing about the pack.
	head = re.compile(r'^\s*LISTMENU\s+"([^"]+)"', re.I | re.M)
	patch = re.compile(r'^\s*StaticPatch\s+[-\d]+\s*,\s*[-\d]+\s*,\s*"([^"]+)"', re.I | re.M)

	for arc in reversed(archives):
		for raw in reversed(arc.read("MENUDEF")):
			text = raw.decode("latin-1", "replace")

			for m in head.finditer(text):
				if m.group(1).strip().lower() != "mainmenu":
					continue

				# To the next menu, so a graphic belonging to the screen after this one is not
				# mistaken for this one's.
				nxt = head.search(text, m.end())
				block = text[m.end():nxt.start() if nxt else len(text)]

				found = patch.search(block)
				if found:
					return found.group(1).upper()

	return None


def as_picture(path):
	"""The file itself, when it is a picture rather than an archive to search.

	Some things have no usable art inside them at all: a title built by a script, a menu graphic left
	blank, a logo that only ever existed on the project's own page. For those the source is a picture
	somebody supplies, and it goes through everything below unchanged -- same slot, same budget, same
	treatment -- so a supplied picture and an extracted one cannot end up looking like they came from
	different tools.
	"""
	try:
		img = Image.open(path)
		img.load()
		return img.convert("RGBA")
	except Exception:
		return None


def textures_alias(archives, want):
	"""The patch a TEXTURES definition draws for `want`, if the name is defined there rather than
	being a lump of its own.

	A pack may define its logo as a composite instead of shipping it under the name the engine
	looks for:

	    graphic M_DOOM, 2000, 2000
	    {
	        xscale 4.3
	        yscale 4.3
	        Patch GVHLOGO, 0, 0
	    }

	Asking the archive for M_DOOM then finds nothing, and the pack looks like it has no logo when
	it plainly has one. Ghouls vs Humans does this in both Legacy of Darkness and Classic: Reborn,
	and it is a common enough shape that missing it costs art across the catalogue rather than in
	one place.

	Only the first patch is taken. A composite of several is a picture assembled from pieces, and
	the first is the one at the origin; a logo built that way is rare enough to be worth less than
	the complication of compositing it here.
	"""
	head = re.compile(r'^\s*(?:graphic|texture|sprite|walltexture|flat)\s+"?([\w\-\.]+)"?\s*,',
	                  re.I | re.M)
	patch = re.compile(r'^\s*patch\s+"?([\w\-\.]+)"?\s*,', re.I | re.M)

	for arc in reversed(archives):
		for raw in reversed(arc.read("TEXTURES")):
			text = raw.decode("latin-1", "replace")

			for m in head.finditer(text):
				if m.group(1).upper() != want.upper():
					continue

				# To the next definition, so a patch belonging to the one after this is not read as
				# part of it.
				nxt = head.search(text, m.end())
				block = text[m.end():nxt.start() if nxt else len(text)]

				found = patch.search(block)
				if found and found.group(1).upper() != want.upper():
					return found.group(1).upper()

	return None

def resolve(paths, lumps=LUMPS):
	"""The art the loaded set actually shows, and where it came from.

	Later archives override earlier ones, so this walks the load order backwards and takes the first
	answer it finds. That is the engine's own rule, and following it is the only way the picture
	matches what a player would see.
	"""
	archives = []
	for p in paths:
		if not os.path.exists(p):
			continue

		# A picture given directly is the answer, not somewhere to look for one.
		if not zipfile.is_zipfile(p):
			with open(p, "rb") as f:
				head = f.read(4)
			if head != b"PWAD" and head != b"IWAD":
				img = as_picture(p)
				if img is not None and img.getbbox():
					return img, "file", os.path.basename(p)

		archives.append(Archive(p))

	# The palette a patch is drawn against, resolved by the same rule. A pack shipping its own
	# recolours everything, and reading it against somebody else's is how art comes out wrong.
	palette = None
	for arc in reversed(archives):
		for raw in reversed(arc.read("PLAYPAL")):
			palette = parse_palette(raw)
			if palette:
				break
		if palette:
			break

	if palette is None:
		palette = default_palette()

	# The menu's own choice goes first, ahead of the convention, because it is the one that says
	# what is actually drawn.
	named = menu_logo(archives)
	if named:
		lumps = (named,) + tuple(x for x in lumps if x != named)

	for want in lumps:
		# [rc4l] A name defined in TEXTURES rather than shipped as a lump resolves to the patch it
		# draws, so a pack that composites its logo is not read as having none.
		names = [want]
		alias = textures_alias(archives, want)
		if alias:
			names.append(alias)

		for want in names:
			for arc in reversed(archives):
				for raw in reversed(arc.read(want)):
					img = decode(raw, palette)
					if img is None or not img.getbbox():
						continue

					# A blank graphic is not art. Skipping it rather than failing outright lets the
					# next candidate answer, which is usually the convention behind the menu's own
					# choice.
					if not informative(img.crop(img.getbbox())):
						continue

					return img, want, os.path.basename(arc.path)

	return None, None, None


# How far from the surrounding colour a pixel may be and still count as part of it.
KEY_TOLERANCE = 32

# How much of the border must agree before there is a surround to remove at all.
KEY_BORDER_SHARE = 0.9


def unbox(img):
	"""Cut away a flat surround that a picture has instead of transparency.

	A picture that came out of an archive already says what is transparent, because somebody decided.
	A picture from anywhere else usually does not: it is a logo sitting on whatever colour it was
	saved on, and drawing it unchanged puts a hard rectangle of that colour on the panel.

	Flood filled from the EDGES rather than keyed globally, so the dark parts inside a logo survive.
	Keying every dark pixel would eat the logo's own shadows and outlines, which is how this goes
	wrong.
	"""
	if img.split()[3].getextrema()[0] < 255:
		return img					# it already has transparency; that answer is authoritative

	w, h = img.size
	if (w < 3) or (h < 3):
		return img

	px = img.load()

	border = []
	for x in range(w):
		border.append(px[x, 0][:3])
		border.append(px[x, h - 1][:3])
	for y in range(h):
		border.append(px[0, y][:3])
		border.append(px[w - 1, y][:3])

	common = max(set(border), key=border.count)
	if border.count(common) < KEY_BORDER_SHARE * len(border):
		return img					# no single surround, so nothing to take away

	def alike(c):
		return (abs(c[0] - common[0]) <= KEY_TOLERANCE and
		        abs(c[1] - common[1]) <= KEY_TOLERANCE and
		        abs(c[2] - common[2]) <= KEY_TOLERANCE)

	seen = bytearray(w * h)
	stack = []
	for x in range(w):
		stack.append((x, 0))
		stack.append((x, h - 1))
	for y in range(h):
		stack.append((0, y))
		stack.append((w - 1, y))

	while stack:
		x, y = stack.pop()
		if (x < 0) or (y < 0) or (x >= w) or (y >= h):
			continue
		if seen[y * w + x]:
			continue
		if not alike(px[x, y]):
			continue

		seen[y * w + x] = 1
		px[x, y] = (0, 0, 0, 0)
		stack.append((x + 1, y))
		stack.append((x - 1, y))
		stack.append((x, y + 1))
		stack.append((x, y - 1))

	return img


def fit(img, height, max_width):
	"""To the slot, aspect kept. Height leads; width only clamps something unusually wide."""
	# Before the crop, so a surround that becomes transparent is then trimmed away as well.
	img = unbox(img.copy())
	if not img.getbbox():
		return img
	img = img.crop(img.getbbox())
	tall = img.resize((img.size[0], max(1, int(round(img.size[1] * PIXEL_ASPECT)))), Image.LANCZOS)

	scale = height / float(tall.size[1])
	if tall.size[0] * scale > max_width:
		scale = max_width / float(tall.size[0])

	return tall.resize((max(1, int(round(tall.size[0] * scale))),
	                    max(1, int(round(tall.size[1] * scale)))), Image.LANCZOS)


def compress(img, budget, background=BACKGROUND):
	"""Under budget. Returns the bytes, how many colours it took, and whether it got there.

	Colours are spent FIRST and pixels only after they run out, because a logo is flat colour and
	posterises gracefully where a smaller one just goes illegible. But the ceiling is a guarantee, not
	a preference: an unusually wide logo has twice the pixels of an ordinary one and can exhaust every
	colour step while still being too big, and writing it anyway would quietly break the promise the
	budget exists to make. So size is the LAST thing spent, and it is spent rather than the ceiling.

	Transparency is kept, because most logos have it and the surface behind them is not flat. A
	logo flattened onto one colour shows that colour as a rectangle around itself the moment the
	surface is a gradient, which is what a panel usually is.
	"""
	# [rc4l] Where a pixel is PART transparent, blend it into the background rather than carrying
	# its alpha through.
	#
	# Full per-pixel alpha would need a palette of colour-and-alpha pairs, and the pixels needing it
	# are the antialiased rim and nothing else. Blending them into the surface they will be drawn on
	# reproduces that rim exactly, for one palette entry and no format complexity. It is only
	# approximate to the extent the surface is not the colour given, and a panel varies by a level or
	# two across the height of a logo.
	flat = Image.new("RGB", img.size, background)
	flat.paste(img, (0, 0), img)

	# Fully clear stays clear. This is the part that matters: it is what stops a box being drawn.
	clear = img.split()[3].point(lambda a: 255 if a == 0 else 0)
	transparent = clear.getbbox() is not None

	last = None

	for step in range(SIZE_STEPS):
		if step:
			# Only ever reached when every colour depth was still too big. A tenth off each time,
			# so the first thing that fits is barely smaller than what was asked for.
			scale = SIZE_SHRINK ** step
			size = (max(1, int(round(img.size[0] * scale))),
			        max(1, int(round(img.size[1] * scale))))
			flat = Image.new("RGB", size, background)
			shrunk = img.resize(size, Image.LANCZOS)
			flat.paste(shrunk, (0, 0), shrunk)
			clear = shrunk.split()[3].point(lambda a: 255 if a == 0 else 0)
			transparent = clear.getbbox() is not None

		for colours in COLOUR_STEPS:
			# One index is spent on the clear colour when there is transparency to record.
			want = max(2, colours - 1) if transparent else colours
			quantised = flat.quantize(colors=want, method=Image.MEDIANCUT, dither=Image.NONE)

			save = {}
			if transparent:
				index = max(quantised.getdata()) + 1
				if index > 255:
					index = 255
				palette = quantised.getpalette()
				palette += [0] * (768 - len(palette))
				palette[index * 3:index * 3 + 3] = list(background)
				quantised.putpalette(palette)
				quantised.paste(index, clear)
				save["transparency"] = index

			buf = io.BytesIO()
			quantised.save(buf, "PNG", optimize=True, compress_level=9, **save)
			last = (buf.getvalue(), colours)
			if len(last[0]) <= budget:
				return last[0], colours, True

	return last[0], last[1], False


def extract(paths, out, height, width, budget, lumps=LUMPS, background=BACKGROUND):
	"""The whole job for one slot. Returns a dict describing what happened, or None for no art."""
	img, lump, source = resolve(paths, lumps)
	if img is None:
		return None

	sized = fit(img, height, width)
	data, colours, ok = compress(sized, budget, background)

	if out:
		d = os.path.dirname(out)
		if d and not os.path.isdir(d):
			os.makedirs(d)
		with open(out, "wb") as f:
			f.write(data)

	# Read back rather than assumed: compress may have had to give up pixels to reach the budget, and
	# reporting the size we ASKED for would hide exactly the case worth knowing about.
	written = Image.open(io.BytesIO(data))

	return {
		"out": out,
		"lump": lump,
		"source": source,
		"size": list(written.size),
		"asked": list(sized.size),
		"shrunk": written.size != sized.size,
		"bytes": len(data),
		"colours": colours,
		"within_budget": ok,
		"transparent": "transparency" in written.info,
	}


def main(argv):
	ap = argparse.ArgumentParser(
		description="Extract menu art from game archives at a fixed slot size and byte budget.")
	ap.add_argument("files", nargs="+",
	                help="archives IN LOAD ORDER; later ones override earlier ones")
	ap.add_argument("-o", "--out", help="write the PNG here (otherwise report only)")
	ap.add_argument("--height", type=int, default=36, help="slot height in layout pixels")
	ap.add_argument("--width", type=int, default=252, help="widest the slot allows")
	ap.add_argument("--budget", type=int, default=1024, help="hard byte ceiling for the PNG")
	ap.add_argument("--lumps", default=",".join(LUMPS),
	                help="lump names to try, best first")
	ap.add_argument("--background", default="%d,%d,%d" % BACKGROUND,
	                help="R,G,B the art will be drawn on; antialiased edges blend into it")
	ap.add_argument("--json", action="store_true", help="report as JSON")
	args = ap.parse_args(argv)

	got = extract(args.files, args.out, args.height, args.width, args.budget,
	              tuple(x.strip().upper() for x in args.lumps.split(",") if x.strip()),
	              tuple(int(x) for x in args.background.split(",")))

	if got is None:
		if args.json:
			print(json.dumps({"found": False}))
		else:
			sys.stderr.write("no menu art in those files; fall back to text\n")
		return 1

	if args.json:
		got["found"] = True
		print(json.dumps(got))
	else:
		print("%s from %s: %dx%d, %d colours, %s, %d B%s" % (
			got["lump"], got["source"], got["size"][0], got["size"][1], got["colours"],
			"transparent" if got["transparent"] else "opaque",
			got["bytes"], "" if got["within_budget"] else "  OVER BUDGET"))

	return 0 if got["within_budget"] else 2


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
