import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeMapChange, buildNetBotMapChangePk3, MAPINFO, MAPS } from "../scenarios/netbotmapchange.mjs";

test("the map change passes once the server lives on MAP02 with its bot", () => {
  assert.deepEqual(judgeMapChange({ alive: true, map: "MAP02", bots: 1 }), []);
  assert.deepEqual(judgeMapChange({ alive: false, map: null, bots: 0 }), ["the server died loading MAP02 with a bot in the game"]);
  assert.deepEqual(judgeMapChange({ alive: true, map: "MAP01", bots: 0 }), ["the server is on MAP01, not MAP02", "the server has 0 bots after the change"]);
});

test("MAP01 goes straight to MAP02, and the pk3 holds both maps and the MAPINFO", () => {
  assert.match(MAPINFO, /map MAP01 "one"\s*\{\s*next = "MAP02"\s*nointermission/);
  const text = buildNetBotMapChangePk3().toString("latin1");
  for (const name of [...MAPS.map((m) => `maps/${m}.wad`), "mapinfo.txt"]) assert.ok(text.includes(name), name);
});
