import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeBoard, buildIntermissionBoardPk3, titlePng, MAPINFO, TITLE } from "../scenarios/intermissionboard.mjs";
import { decodePNGBytes } from "../src/pngcore.mjs";

test("the board passes in the upper half and names where it started otherwise", () => {
  assert.deepEqual(judgeBoard([{ y: 96, text: "Rankings" }], 720), []);
  assert.deepEqual(judgeBoard([{ y: 625, text: "Rankings" }], 720), ["the scoreboard starts at 625 of 720, below the title, not near the top"]);
  assert.deepEqual(judgeBoard([{ y: 96, text: "Deathmatch" }], 720), ["the intermission drew no scoreboard"]);
});

test("the title is a whole clean screen, and the pk3 holds every lump", () => {
  const img = decodePNGBytes(titlePng());
  assert.deepEqual([img.w, img.h], [320, 200]);
  assert.match(MAPINFO, new RegExp(`titlepatch = "${TITLE}"`));
  const text = buildIntermissionBoardPk3().toString("latin1");
  for (const name of ["maps/MAP01.wad", "maps/MAP02.wad", "mapinfo.txt", `graphics/${TITLE.toLowerCase()}.png`]) assert.ok(text.includes(name), name);
});
