# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

"""Regenerate every piece of catalogue menu art, from the repository root.

    python .claude/skills/catalogue-menu-art/generate.py --store <folder of the loaded files>

Writes art.png beside an entry that plays one way, art.<variant>.png for each way of playing, and
art.png beside each mix. Reports every slot, and every slot that has no art and falls back to text.

Reproducible on purpose: the settings live here rather than in somebody's shell history, so the next
run produces the same catalogue. Nothing about any particular pack lives here; a slot that needs
something other than the automatic answer says so in the catalogue.
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

# [rc4l] What a slot may say for itself, when the automatic answer is wrong for it.
#
# Read from the catalogue rather than kept here, because it is a fact about a pack and the packs are
# described over there. A list of pack names in this file would be a list that has to be edited every
# time the catalogue changes, by somebody editing a tool.
#
#   "art": "<lump>"      use this instead of what would be resolved
#   "art": "<file.ext>"  a picture supplied beside the json, for something with no usable art of
#                        its own: a title built by a script, a blank menu graphic, a logo that only
#                        ever existed on the project's own page
#   "art": ""            no picture; draw the name
#   absent               resolve automatically, which is nearly always right
#
# A value with an extension is a file, since lump names have none. The supplied picture goes through
# exactly what an extracted one does, so the two cannot end up looking like different tools made
# them.
ART_KEY = "art"


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

	def do(out, names, label, says):
		for n in names:
			if not os.path.exists(os.path.join(args.store, n)):
				absent.add(n)

		lumps = menuart.LUMPS
		sources = load_order(args.store, names)

		if ART_KEY in says:
			named = (says[ART_KEY] or "").strip()
			if not named:
				lumps = ()
			elif os.path.splitext(named)[1]:
				# A supplied picture, beside the json that named it. Nothing else is searched: the
				# slot said what its art is.
				sources = [os.path.join(os.path.dirname(out), named)]
			else:
				lumps = (named.upper(),)

		# An empty override is "no art", so nothing is looked for and the caller draws the name.
		got = None
		if lumps:
			got = menuart.extract(sources, None if args.check else out,
			                      HEIGHT, WIDTH, BUDGET, lumps)

		# A slot that USED to have art and no longer should must lose the file too, or the panel goes
		# on drawing yesterday's answer.
		if (got is None) and not args.check and os.path.exists(out):
			os.remove(out)

		rows.append((label, got))

	for p in sorted(glob.glob(os.path.join(args.catalogue, "*", "addon.json"))):
		d = os.path.dirname(p)
		entry = json.load(io.open(p, encoding="utf-8"))
		who = os.path.basename(d)
		base = [f["name"] for f in entry.get("files") or []]
		variants = entry.get("variants") or []

		if not variants:
			do(os.path.join(d, "art.png"), base, who, entry)
			continue

		# Per variant, because the header names the variant. The entry's own files are part of the
		# load order either way, so a pack whose art sits at entry level still resolves for each.
		for v in variants:
			# A variant speaks for itself, or falls back to whatever its entry said.
			do(os.path.join(d, "art.%s.png" % v["id"]),
			   base + [f["name"] for f in v.get("files") or []],
			   "%s/%s" % (who, v["id"]),
			   v if ART_KEY in v else entry)

	for p in sorted(glob.glob(os.path.join(args.catalogue, "remix", "*", "remix.json"))):
		d = os.path.dirname(p)
		remix = json.load(io.open(p, encoding="utf-8"))
		do(os.path.join(d, "art.png"), [f["name"] for f in remix.get("files") or []],
		   "remix/" + os.path.basename(d), remix)

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
