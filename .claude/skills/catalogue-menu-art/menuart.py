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
LUMPS = ("M_DOOM", "TITLEPIC")

# Archive pixels are 1.2 times as tall as they are wide. Correcting is what makes the output look
# like what a player sees rather than a squashed copy of it.
PIXEL_ASPECT = 1.2

# Fewer colours is the first thing to spend, because it costs less than resolution: a logo is flat
# colour and posterises gracefully, where a smaller logo just goes illegible.
COLOUR_STEPS = (256, 128, 64, 32, 16, 8, 4)

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


def resolve(paths, lumps=LUMPS):
	"""The art the loaded set actually shows, and where it came from.

	Later archives override earlier ones, so this walks the load order backwards and takes the first
	answer it finds. That is the engine's own rule, and following it is the only way the picture
	matches what a player would see.
	"""
	archives = []
	for p in paths:
		if os.path.exists(p):
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

	for want in lumps:
		for arc in reversed(archives):
			for raw in reversed(arc.read(want)):
				img = decode(raw, palette)
				if img is not None and img.getbbox():
					return img, want, os.path.basename(arc.path)

	return None, None, None


def fit(img, height, max_width):
	"""To the slot, aspect kept. Height leads; width only clamps something unusually wide."""
	img = img.crop(img.getbbox())
	tall = img.resize((img.size[0], max(1, int(round(img.size[1] * PIXEL_ASPECT)))), Image.LANCZOS)

	scale = height / float(tall.size[1])
	if tall.size[0] * scale > max_width:
		scale = max_width / float(tall.size[0])

	return tall.resize((max(1, int(round(tall.size[0] * scale))),
	                    max(1, int(round(tall.size[1] * scale)))), Image.LANCZOS)


def compress(img, budget):
	"""Under budget, keeping every pixel. Returns the bytes and how many colours it took.

	Size is settled before this runs and is never traded away: the slot is the slot, and an image
	that fits the budget by being smaller than its slot just gets stretched back and looks worse
	than the posterised version of the right size.
	"""
	# Flattened onto black. Alpha costs a tRNS chunk for a transparency the panel never sees,
	# because it draws on a dark background anyway.
	flat = Image.new("RGB", img.size, (0, 0, 0))
	flat.paste(img, (0, 0), img)

	last = None
	for colours in COLOUR_STEPS:
		quantised = flat.quantize(colors=colours, method=Image.MEDIANCUT, dither=Image.NONE)
		buf = io.BytesIO()
		quantised.save(buf, "PNG", optimize=True, compress_level=9)
		last = (buf.getvalue(), colours)
		if len(last[0]) <= budget:
			return last[0], colours, True

	return last[0], last[1], False


def extract(paths, out, height, width, budget, lumps=LUMPS):
	"""The whole job for one slot. Returns a dict describing what happened, or None for no art."""
	img, lump, source = resolve(paths, lumps)
	if img is None:
		return None

	sized = fit(img, height, width)
	data, colours, ok = compress(sized, budget)

	if out:
		d = os.path.dirname(out)
		if d and not os.path.isdir(d):
			os.makedirs(d)
		with open(out, "wb") as f:
			f.write(data)

	return {
		"out": out,
		"lump": lump,
		"source": source,
		"size": list(sized.size),
		"bytes": len(data),
		"colours": colours,
		"within_budget": ok,
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
	ap.add_argument("--json", action="store_true", help="report as JSON")
	args = ap.parse_args(argv)

	got = extract(args.files, args.out, args.height, args.width, args.budget,
	              tuple(x.strip().upper() for x in args.lumps.split(",") if x.strip()))

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
		print("%s from %s: %dx%d, %d colours, %d B%s" % (
			got["lump"], got["source"], got["size"][0], got["size"][1],
			got["colours"], got["bytes"], "" if got["within_budget"] else "  OVER BUDGET"))

	return 0 if got["within_budget"] else 2


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
