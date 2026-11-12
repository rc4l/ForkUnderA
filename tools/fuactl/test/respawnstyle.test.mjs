import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeStyles, enterScript, buildRespawnStylePk3, STYLE, DECORATE, MAPINFO, CLASS, DURING_TICS, DEAD_TICS, APROP_RENDERSTYLE, SIDES } from "../scenarios/respawnstyle.mjs";
import { PCD } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

test("the styles pass translucent then additive, and name each that is not", () => {
  assert.deepEqual(judgeStyles({ during: STYLE.Translucent, after: STYLE.Add, dead: -1 }, false), []);
  assert.deepEqual(judgeStyles({ during: STYLE.Translucent, after: STYLE.Normal, dead: -1 }, false), ["the player's style after it is 1, not 65 (additive)"]);
  assert.deepEqual(judgeStyles({ during: STYLE.Add, after: STYLE.Add, dead: -1 }, false), ["the player's style during invulnerability is 65, not 64 (translucent)"]);
});

test("a killed player is judged by the corpse's style, not the style after", () => {
  assert.deepEqual(judgeStyles({ during: STYLE.Translucent, after: STYLE.Normal, dead: STYLE.Add }, true), []);
  assert.deepEqual(judgeStyles({ during: STYLE.Translucent, after: STYLE.Add, dead: STYLE.Normal }, true), ["the corpse's style is 1, not 65 (additive)"]);
});

test("the script reads the activator's style three times and kills them only behind its cvar", () => {
  const code = enterScript();
  assert.deepEqual(code.slice(0, 2), [PCD.DELAYDIRECT, DURING_TICS]);
  assert.equal(code.filter((w) => w === PCD.GETACTORPROPERTY).length, 3);
  assert.ok(code.includes(APROP_RENDERSTYLE) && code.includes(DEAD_TICS) && code.includes(SPECIAL.DamageThing));
  assert.ok(code.indexOf(PCD.IFNOTGOTO) < code.indexOf(SPECIAL.DamageThing));
  assert.equal(code.at(-1), PCD.TERMINATE);
  assert.deepEqual(SIDES.map((s) => s.launchCvars.zx_style_kill ?? 0), [0, 1]);
});

test("the player's only class is additive, and the pk3 holds every lump", () => {
  assert.match(DECORATE, /RenderStyle Add/);
  assert.match(MAPINFO, new RegExp(`playerclasses = "${CLASS}"`));
  const text = buildRespawnStylePk3().toString("latin1");
  for (const name of ["maps/MAP01.wad", "decorate.txt", "mapinfo.txt", "cvarinfo.txt", "zx_style_kill = 0"]) assert.ok(text.includes(name), name);
});
