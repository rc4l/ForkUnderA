import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeWadInfo, judgeOutOfRange, buildScript, buildOutOfRangeScript, NS_SPRITES, BAD_INDEX, WAD_INFO } from "../scenarios/acswadinfo.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

const log = ({ ns = NS_SPRITES, wad = 5, name = "x.pk3", parent = -1, inner = 6, innerParent = 5 } = {}) =>
  `ns ${ns} wad ${wad} name ${name} parent ${parent} innerwad ${inner} innerparent ${innerParent}\n`;

test("the sprite's namespace, its pk3 and the embedded wad's parent pass as read back", () => {
  assert.deepEqual(judgeWadInfo(log(), "X.pk3"), []);
});

test("each wrong answer names itself", () => {
  assert.deepEqual(judgeWadInfo(log({ ns: 0 }), "x.pk3"), [`the sprite's namespace is 0, not ${NS_SPRITES}`]);
  assert.deepEqual(judgeWadInfo(log({ name: "y.pk3" }), "x.pk3"), ['its wad is named "y.pk3", not "x.pk3"']);
  assert.deepEqual(judgeWadInfo(log({ parent: 0 }), "x.pk3"), ["the pk3's parent is 0, not -1"]);
  assert.deepEqual(judgeWadInfo(log({ inner: 5, innerParent: -1 }), "x.pk3"), ["the embedded wad is 5, beside the pk3's 5", "the embedded wad's parent is -1, not the pk3's 5"]);
  assert.match(judgeWadInfo("Unknown command", "x.pk3")[0], /logged no wad info/);
});

test("the script asks GetWadInfo through CALLFUNC", () => {
  const code = buildScript();
  assert.ok(code.some((w, i) => w === PCD.CALLFUNC && code[i + 2] === ACSF.GetWadInfo));
});

test("an out-of-range wad passes only as neither optional nor authenticated", () => {
  assert.deepEqual(judgeOutOfRange("optional 0 auth 0"), []);
  assert.deepEqual(judgeOutOfRange("optional 1 auth 0"), [`wad ${BAD_INDEX} reads as optional 1, authenticated 0, not 0 and 0`]);
  assert.match(judgeOutOfRange("")[0], /logged nothing/);
});

test("script 2 asks the out-of-range wad both questions", () => {
  const code = buildOutOfRangeScript();
  const asked = code.flatMap((w, i) => (w === ACSF.GetWadInfo && code[i - 2] === PCD.CALLFUNC ? [code[i - 3]] : []));
  assert.deepEqual(asked, [WAD_INFO.OPTIONAL, WAD_INFO.HASAUTHENTICATEDLUMPS]);
  assert.equal(code.filter((w, i) => w === BAD_INDEX && code[i - 1] === PCD.PUSHNUMBER).length, 2);
});
