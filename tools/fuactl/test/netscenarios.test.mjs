import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad } from "../src/udmf.mjs";
import { buildNetSyncWad, serverChanged, AFTER as SYNC_AFTER, CONFIGS as SYNC_CONFIGS } from "../scenarios/netsync.mjs";
import { buildUnlaggedHitWad, shotOffsets, judgeUnlagged, OFFSET_BAR, CONFIGS as HIT_CONFIGS } from "../scenarios/unlaggedhit.mjs";
import { buildNetBroadcastWad, judgeBroadcast } from "../scenarios/netbroadcast.mjs";
import { buildWadDownloadWad, judgeDownloads, CONFIGS as DL_CONFIGS } from "../scenarios/waddownload.mjs";

const lumpNames = (buf) => readWad(buf).map((l) => l.name);

test("the netgame scenarios' maps build with the scripts that change them", () => {
  assert.deepEqual(lumpNames(buildNetSyncWad()), ["MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
  assert.deepEqual(lumpNames(buildUnlaggedHitWad()), ["MAP01", "TEXTMAP", "ENDMAP"]);
  assert.deepEqual(lumpNames(buildNetBroadcastWad()), ["MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
  assert.deepEqual(lumpNames(buildWadDownloadWad()), ["MAP01", "TEXTMAP", "ENDMAP"]);
  assert.deepEqual(DL_CONFIGS.map((c) => [c.players, c.downloaders]), [[0, 1], [0, 2]]);
  const text = readWad(buildUnlaggedHitWad())[1].data.toString("latin1");
  assert.equal((text.match(/type = [123];/g) || []).length, 3, "the three players' starts");
  assert.ok(SYNC_CONFIGS.some((c) => c.loss) && SYNC_CONFIGS.some((c) => c.lag));
  assert.ok(HIT_CONFIGS.every((c) => c.serverCvars.teamdamage === 1 && c.players === 2 && c.observers === 1));
});

const sector = (i, extra = {}) => ({ i, floor: 0, ceiling: 256, light: 192, color: 0xffffff, floorTex: "FLOOR4_8", ceilingTex: "CEIL3_5", ...extra });

test("netsync holds the server to the scripted changes before judging agreement", () => {
  const changed = { sectors: [sector(0), sector(1, { ceiling: 252 }), sector(2, { floor: 0 }), sector(3, { light: SYNC_AFTER.light, color: SYNC_AFTER.color, floorTex: SYNC_AFTER.floorTex })],
    sides: [{ mid: SYNC_AFTER.wallTex }], actors: [{ c: "ZombieMan", health: -5 }] };
  assert.deepEqual(serverChanged(changed), []);
  const unchanged = { sectors: [sector(0), sector(1, { ceiling: 0 }), sector(2, { floor: 64 }), sector(3, { light: 96 })], sides: [{ mid: "STARTAN2" }], actors: [{ c: "ZombieMan", health: 20 }] };
  assert.deepEqual(serverChanged(unchanged), ["no door opened", "the dim sector's light, colour or floor never changed", "the wall texture never changed", "the lift never lowered", "the zombieman lives (20 health)"]);
});

const shot = (n, target, pos, present = pos, extra = {}) => ({ n, tic: 100 + n, rewind: 98 + n, hit: target, players: [{ p: target, x: pos[0], y: pos[1], z: pos[2], px: present[0], py: present[1], pz: present[2] }], ...extra });

test("shots match by number and measure the target between the server's rewind and the shooter's client", () => {
  const server = { shots: [shot(1, 1, [100, 0, 0], [110, 0, 0]), shot(2, 1, [120, 0, 0]), shot(3, 2, [0, 0, 0])] };
  const client = { shots: [{ n: 1, players: [{ p: 1, x: 103, y: 4, z: 0 }] }, { n: 2, players: [{ p: 1, x: 120, y: 0, z: 0 }] }] };
  const o = shotOffsets(server, client, 1);
  assert.equal(o.length, 2);
  assert.equal(o[0].rewound, 5);
  assert.equal(o[0].present, Math.hypot(7, 4));
  assert.equal(o[0].back, 2);
  assert.equal(o[0].hit, true);
  assert.equal(o[1].rewound, 0);
  assert.deepEqual(shotOffsets(null, null, 1), []);
});

test("unlaggedhit passes close shots on both sides and fails each thing that breaks it", () => {
  const offsets = Array.from({ length: 6 }, (_, i) => ({ rewound: 1 + i * 0.1, present: 20, back: 2, hit: i % 2 === 0 }));
  const ok = judgeUnlagged("x", { offsets, serverShots: 6, clientShots: 7, observerHealth: 70, serverHealth: 70 });
  assert.equal(ok.failed, 0);
  assert.match(ok.line, /^PASS x: 6 shots, 3 hits, rewound offset p50 1\.30/);
  const far = offsets.map((o) => ({ ...o, rewound: OFFSET_BAR + 5 }));
  assert.match(judgeUnlagged("x", { offsets: far, serverShots: 6, clientShots: 6, observerHealth: 70, serverHealth: 70 }).line, /units \(median\) from where the shooter held it/);
  assert.match(judgeUnlagged("x", { offsets, serverShots: 12, clientShots: 6, observerHealth: 70, serverHealth: 70 }).line, /server counted 12 shots and the shooter 6/);
  assert.match(judgeUnlagged("x", { offsets, serverShots: 6, clientShots: 6, observerHealth: 100, serverHealth: 70 }).line, /observer holds the target at 100 health, the server at 70/);
  assert.match(judgeUnlagged("x", { offsets: offsets.slice(0, 2), serverShots: 2, clientShots: 2, observerHealth: 1, serverHealth: 1 }).line, /only 2 shots matched/);
});

test("netbroadcast passes when the observer saw it all and names each miss", () => {
  const seen = { itemsBefore: 2, itemsAfterServer: 0, itemsAfterObserver: 0, rocketSeen: true, serverChanged: true, diffs: [{ who: "observer", diffs: [], compared: {} }] };
  assert.equal(judgeBroadcast("x", seen).failed, 0);
  const missed = judgeBroadcast("x", { ...seen, itemsBefore: 1, itemsAfterServer: 1, itemsAfterObserver: 2, rocketSeen: false, serverChanged: false,
    diffs: [{ who: "observer player 1", diffs: [{ what: "sector 4: light 96/224 (server/client)" }], compared: {} }] });
  assert.equal(missed.failed, 1);
  for (const want of [/held 1 items before/, /picked up 1 of 2 items/, /still holds 2 item\(s\)/, /never saw the player's rocket/, /never changed the server's map/, /observer player 1: .*sector 4: light 96\/224/]) assert.match(missed.line, want);
});

test("waddownload passes clients holding the server's copy and names each that does not", () => {
  const ok = { who: "client 1", wantMd5: "aa", md5: "aa", error: null, diffs: [], compared: {} };
  assert.match(judgeDownloads("x", "m.wad", [ok, { ...ok, who: "client 2" }]).line, /^PASS x: 2 client\(s\) fetched m\.wad from the server/);
  const bad = judgeDownloads("x", "m.wad", [{ ...ok, error: "never reached GS_LEVEL" }, { ...ok, who: "client 2", md5: null },
    { ...ok, who: "client 3", md5: "bb" }, { ...ok, who: "client 4", diffs: [{ what: "sector 1: light 192/112 (server/client)" }] }]);
  assert.equal(bad.failed, 1);
  for (const want of [/client 1 never got in: never reached GS_LEVEL/, /client 2 plays without m\.wad in its download folder/,
    /client 3's m\.wad is not the server's \(md5 bb, not aa\)/, /client 4: .*sector 1: light 192\/112/]) assert.match(bad.line, want);
});

test("lanbrowse passes a LAN row under the server's name and names each miss", async () => {
  const { judgeLan, HOSTNAME, CONFIGS } = await import("../scenarios/lanbrowse.mjs");
  assert.ok(CONFIGS.every((c) => c.broadcast && c.serverCvars.sv_hostname === HOSTNAME));
  assert.equal(judgeLan("x", { row: { lan: true, name: HOSTNAME } }).failed, 0);
  assert.match(judgeLan("x", { error: "never heard it" }).line, /never got in from its LAN listing: never heard it/);
  assert.match(judgeLan("x", {}).line, /holds no row for the server/);
  const wrong = judgeLan("x", { row: { lan: false, name: "other" }, diffs: [{ what: "sector 1: light 192/112 (server/client)" }] }).line;
  for (const want of [/did not arrive by LAN broadcast/, /listed as "other"/, /client: .*sector 1/]) assert.match(wrong, want);
});

test("netmapstate holds the server to every scripted family before judging agreement", async () => {
  const { buildNetMapStateWad, serverChanged, AFTER } = await import("../scenarios/netmapstate.mjs");
  assert.deepEqual(lumpNames(buildNetMapStateWad()), ["MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
  const look = { light: 160, gravity: AFTER.gravity, color: AFTER.color, fade: AFTER.fade, friction: 0.7, rotation: AFTER.rotation, scale: [2, 2, 1, 1], reflect: [0.25, 0], floorTex: AFTER.floorTex };
  const changed = { sectors: [look], sides: [{ mid: AFTER.wallTex }], lines: [{ flags: AFTER.lineFlags | 1, alpha: 0.5 }] };
  assert.deepEqual(serverChanged(changed), []);
  const still = { sectors: [{ light: 160, gravity: 1, color: 0xffffff, fade: 0, friction: 0.90625, rotation: [0, 0], scale: [1, 1, 1, 1], reflect: [0, 0], floorTex: "FLOOR4_8" }],
    sides: [{ mid: "STARTAN2" }], lines: [{ flags: 1, alpha: 1 }] };
  assert.deepEqual(serverChanged(still), ["gravity 1", "colour 16777215", "fade 0", "friction never changed", "rotation 0,0", "floor scale never changed",
    "floor reflection never changed", "floor FLOOR4_8", "the wall texture never changed", "the line's blocking or translucency never changed"]);
  assert.deepEqual(serverChanged({ sectors: [], sides: [], lines: [] }), ["the changed sector is not there"]);
});
