import { test } from "node:test";
import assert from "node:assert/strict";
import { buildBotLumpsPk3, judgeReport, BOT, BOTINFO, ADD_SCRIPT } from "../scenarios/botlumps.mjs";
import { parseSeamReport } from "../src/seams.mjs";
import { ACSF } from "../src/acs.mjs";

test("AddBot is the ACS function before RemoveBot, as in p_acs.cpp", () => {
  assert.equal(ACSF.AddBot, 168);
  assert.equal(ACSF.RemoveBot, ACSF.AddBot + 1);
});

test("the bot has the stock script and a stock chat lump, and chats at every chance", () => {
  assert.match(BOTINFO, new RegExp(`name = "${BOT}"`));
  assert.match(BOTINFO, /script = "humanbot"/);
  assert.match(BOTINFO, /chatlump = "bots\/chatfiles\/soldier\.txt"/);
  assert.match(BOTINFO, /chatfrequency = 100/);
  const bytes = buildBotLumpsPk3().toString("latin1");
  assert.ok(bytes.includes("botinfo.txt") && bytes.includes("maps/MAP01.wad") && bytes.includes(BOT));
  assert.equal(ADD_SCRIPT, 1);
});

test("a report with no wad/read site by another thread passes", () => {
  const clean = "gc/object  game data  100 entries  0 violations\nwad/read  main  80 entries  0 violations\n0 violations; 0 locks taken while holding another\n";
  assert.deepEqual(judgeReport(parseSeamReport(clean)), []);
});

test("a wad/read site by the game thread fails, named", () => {
  const dirty = "wad/read  main  80 entries  8 violations\n8 violations, the first in wad/read on the game thread in FWadLump:1638; 0 locks taken while holding another\n"
    + "  violated wad/read in FWadLump:1638 by the game thread, a tic running, main thread drawing: 4\n"
    + "  violated wad/read in CacheLump:226 by the game thread, a tic running, main thread drawing: 4\n";
  const faults = judgeReport(parseSeamReport(dirty));
  assert.equal(faults.length, 2);
  assert.match(faults[0], /read a lump off the main thread: wad\/read in FWadLump:1638 by the game thread/);
});

test("a report that lists nothing fails", () => {
  assert.deepEqual(judgeReport(parseSeamReport("unknown command")), ["fua_seams listed nothing"]);
});
