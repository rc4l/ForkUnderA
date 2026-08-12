# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

"""Regenerate every piece of catalogue menu art, from the repository root.

    python .claude/skills/catalogue-menu-art/generate.py --store <folder of the loaded files>

Writes art.png beside an entry that plays one way, art.<variant>.png for each way of playing, and
art.png beside each mix. Reports every slot, and every slot that has no art and falls back to text.

Reproducible on purpose: the settings and the handful of overrides live here rather than in
somebody's shell history, so the next run produces the same catalogue.
"""

import argparse
import glob
import io
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import menuart

# Sized and budgeted together; see SKILL.md for the measurements behind both numbers. The asset is
# deliberately larger than the slot, because the panel is drawn in layout pixels and a real screen
# multiplies them. Shipping at slot size means the engine upscales three times over.
HEIGHT = 64
WIDTH = 512
BUDGET = 4096

# [rc4l] Slots whose preferred lump is the wrong picture.
#
# The order in menuart.LUMPS is right nearly always, and this is not a place to record taste. It is
# for a source that is not what it claims: a logo lump holding a menu frame with nothing in it, say.
# Look at the output before adding a line here, and say what is wrong with the source.
OVERRIDES = {
	# Its logo lump is an empty bordered box. The title screen is the only art in the file.
	"rocketjump/extreme": ("TITLEPIC",),
}


def load_order(store, names):
	return [os.path.join(store, n) for n in names]


def main(argv):
	ap = argparse.ArgumentParser(description="Regenerate the catalogue's menu art.")
	ap.add_argument("--store", required=True,
	                help="folder holding every file the catalogue references")
	ap.add_argument("--catalogue", default="catalogue", help="the catalogue folder")
	ap.add_argument("--check", action="store_true", help="report only, write nothing")
	args = ap.parse_args(argv)

	rows = []
	absent = set()

	def do(out, names, label):
		for n in names:
			if not os.path.exists(os.path.join(args.store, n)):
				absent.add(n)
		lumps = OVERRIDES.get(label, menuart.LUMPS)
		got = menuart.extract(load_order(args.store, names), None if args.check else out,
		                      HEIGHT, WIDTH, BUDGET, lumps)
		rows.append((label, got))

	for p in sorted(glob.glob(os.path.join(args.catalogue, "*", "addon.json"))):
		d = os.path.dirname(p)
		entry = json.load(io.open(p, encoding="utf-8"))
		who = os.path.basename(d)
		base = [f["name"] for f in entry.get("files") or []]
		variants = entry.get("variants") or []

		if not variants:
			do(os.path.join(d, "art.png"), base, who)
			continue

		# Per variant, because the header names the variant. The entry's own files are part of the
		# load order either way, so a pack whose art sits at entry level still resolves for each.
		for v in variants:
			do(os.path.join(d, "art.%s.png" % v["id"]),
			   base + [f["name"] for f in v.get("files") or []],
			   "%s/%s" % (who, v["id"]))

	for p in sorted(glob.glob(os.path.join(args.catalogue, "remix", "*", "remix.json"))):
		d = os.path.dirname(p)
		remix = json.load(io.open(p, encoding="utf-8"))
		do(os.path.join(d, "art.png"), [f["name"] for f in remix.get("files") or []],
		   "remix/" + os.path.basename(d))

	print("%-28s %-9s %-11s %7s %6s" % ("SLOT", "LUMP", "SIZE", "BYTES", "COLS"))
	for label, got in rows:
		if got:
			print("%-28s %-9s %-11s %7d %6d%s" % (
				label, got["lump"], "%dx%d" % tuple(got["size"]), got["bytes"], got["colours"],
				"" if got["within_budget"] else "  OVER BUDGET"))
		else:
			print("%-28s %-9s" % (label, "text"))

	drawn = [g for _l, g in rows if g]
	over = [l for l, g in rows if g and not g["within_budget"]]
	total = sum(g["bytes"] for g in drawn)

	print("\n%d with art, %d falling back to text, %d over budget" % (
		len(drawn), len(rows) - len(drawn), len(over)))
	print("total %.1f KB, average %d B" % (total / 1024.0, total // max(1, len(drawn))))

	if absent:
		print("\n%d referenced files are not in the store, so those slots may be wrong:" % len(absent))
		for n in sorted(absent):
			print("   ", n)

	# Byte counts say nothing about whether a picture decoded correctly. Look at the output.
	print("\nLook at the images before committing them. See SKILL.md.")
	return 1 if over else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
