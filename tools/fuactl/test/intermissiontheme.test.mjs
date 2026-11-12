import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeOrders, judgeMusic, buildIntermissionThemePk3, MAPINFO, TEAMINFO, MAP_WIN, TEAM_WIN, TEAM_LOSE, TEAM_STARTS, SIDES } from "../scenarios/intermissiontheme.mjs";

test("the theme tracks pass as TEAMINFO gave Blue's and Red's none", () => {
  assert.deepEqual(judgeOrders(`orders ${TEAM_WIN.order} ${TEAM_LOSE.order} 0 0`), []);
  assert.deepEqual(judgeOrders("orders 0 0 0 0"), [`the theme tracks read 0 0 0 0, not ${TEAM_WIN.order} ${TEAM_LOSE.order} 0 0`]);
  assert.match(judgeOrders("")[0], /logged no theme tracks/);
});

test("the intermission's music passes by name, any case, and track", () => {
  assert.deepEqual(judgeMusic(`music "zxwin" order ${MAP_WIN.order}`, MAP_WIN), []);
  assert.deepEqual(judgeMusic(`music "d_stwin" order 0`, MAP_WIN), [`the intermission plays "d_stwin" at track 0, not "${MAP_WIN.name}" at ${MAP_WIN.order}`]);
  assert.match(judgeMusic("Unknown command", MAP_WIN)[0], /fua_music reported nothing/);
});

test("MAPINFO, TEAMINFO and the songs are in the pk3, each side wanting its own theme", () => {
  assert.match(MAPINFO, new RegExp(`winnermusic = "${MAP_WIN.name}", ${MAP_WIN.order}`));
  assert.ok(TEAMINFO.includes(`WinnerTheme "${TEAM_WIN.name}:${TEAM_WIN.order}"`) && TEAMINFO.includes(`LoserTheme "${TEAM_LOSE.name}" ${TEAM_LOSE.order}`));
  const text = buildIntermissionThemePk3().toString("latin1");
  for (const s of [MAP_WIN, TEAM_WIN, TEAM_LOSE]) assert.ok(text.includes(`music/${s.name.toLowerCase()}.mid`), s.name);
  assert.deepEqual(SIDES.map((s) => s.want), [MAP_WIN, TEAM_WIN, TEAM_WIN]);
  assert.deepEqual(SIDES.find((s) => s.name === "ctf").launchCvars, { ctf: 1 });
  for (const n of Object.values(TEAM_STARTS)) assert.ok(TEAMINFO.includes(`PlayerStartThingNumber ${n}`) && text.includes(`type = ${n};`), String(n));
});
