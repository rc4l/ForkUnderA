import { test } from "node:test";
import assert from "node:assert/strict";
import { buildAcsShadersPk3, GLDEFS, SHADERS, STEPS, BOXES, SCRIPTS, enable, uniform, shaderCalls, libraryScript, LIBRARY } from "../scenarios/acsshaders.mjs";
import { buildNetShadersPk3, parseShaderStates, stateFaults, countShaderCommands, clocksAgree, SCRIPTS as NET_SCRIPTS, CONFIGS } from "../scenarios/netshaders.mjs";
import { ACSF, PCD, SCRIPTF } from "../src/acs.mjs";

// [Doruk-Aksoy] Every block names a shader the pk3 carries, every step pukes a script the map or its library has, and every box compares two steps.
test("the ACS shader pk3 carries its shaders, scripts and library", () => {
  const file = buildAcsShadersPk3();
  assert.ok(file.includes(Buffer.from(`acs/${LIBRARY.toLowerCase()}.o`)));
  assert.ok(file.includes(Buffer.from("loadacs.txt")));
  for (const [, lump] of GLDEFS.matchAll(/Shader "shaders\/acs\/(\w+)\.fp" 330/g)) assert.ok(SHADERS[lump], `no shader ${lump}`);
  for (const step of STEPS)
    for (const c of step.commands)
      if (typeof c === "string" && c.startsWith("puke")) assert.ok(SCRIPTS[c.split(" ")[1]] || c === "puke 21", c);
  const steps = new Set(STEPS.map((s) => s.name));
  for (const [name, b] of Object.entries(BOXES.MAP01)) for (const p of b.pair) assert.ok(steps.has(p), `${name}: ${p}`);
});

// [Doruk-Aksoy] A call pushes its arguments (strings as their index, floats as fixed point) and drops the result.
test("shader calls compile to CALLFUNC with their arguments", () => {
  assert.deepEqual(enable("u1f", true), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 1, PCD.CALLFUNC, 2, ACSF.fua_SetShaderEnabled, PCD.DROP]);
  assert.deepEqual(enable("red", true, false, 105).slice(-8), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 105, PCD.CALLFUNC, 4, ACSF.fua_SetShaderEnabled, PCD.DROP]);
  assert.deepEqual(uniform("u2f", "b", [0.5, 1]).slice(-8), [PCD.PUSHNUMBER, 32768, PCD.PUSHNUMBER, 65536, PCD.CALLFUNC, 4, ACSF.fua_SetShaderUniform2f, PCD.DROP]);
  assert.deepEqual(uniform("u1i", "m", [7], { int: true, own: true }).slice(-8), [PCD.PUSHNUMBER, 7, PCD.PUSHNUMBER, 1, PCD.CALLFUNC, 4, ACSF.fua_SetShaderUniform1i, PCD.DROP]);
  assert.throws(() => enable("nosuch", true), /no string nosuch/);
});

// [Doruk-Aksoy] A library's strings are tagged after each push, its result forms leave the result for a test, and the library itself is ACSE.
test("a library's calls tag their strings", () => {
  const lib = shaderCalls(["x"], { tagged: true });
  assert.deepEqual(lib.enableResult("x", false), [PCD.PUSHNUMBER, 0, PCD.TAGSTRING, PCD.PUSHNUMBER, 0, PCD.CALLFUNC, 2, ACSF.fua_SetShaderEnabled]);
  const object = libraryScript();
  assert.equal(object.subarray(0, 4).toString("latin1"), "ACSE");
  assert.ok(object.includes(Buffer.from("nosuch")));
});

// [Doruk-Aksoy] The netplay map is ACSE, since its scripts carry the NET and CLIENTSIDE flags, and it brings its song.
test("the netplay pk3 holds flagged scripts and a song", () => {
  const file = buildNetShadersPk3();
  assert.ok(file.includes(Buffer.from("SFLG")));
  assert.ok(file.includes(Buffer.from("mapinfo.txt")));
  assert.ok(NET_SCRIPTS.some((s) => s.flags & SCRIPTF.CLIENTSIDE));
  assert.deepEqual(CONFIGS.map((c) => c.name), ["loopback", "lossy", "roundreset"]);
});

test("fua_postshaders lines read back by shader", () => {
  const s = parseShaderStates("fua_postshaders: 3 entries at 400\nfua_postshaders: u3f on since 12 pad=0,0,0,1 c=0,0,1,1\r\nnoise\nfua_postshaders: u1f off a=0.5,0,0,1\n");
  assert.equal(s.entries, 3);
  assert.equal(s.at, 400);
  assert.deepEqual(s.shaders, { u3f: { on: true, since: 12, uniforms: { pad: [0, 0, 0, 1], c: [0, 0, 1, 1] } }, u1f: { on: false, since: null, uniforms: { a: [0.5, 0, 0, 1] } } });
  assert.deepEqual(parseShaderStates(""), { entries: null, at: null, shaders: {} });
});

// [Doruk-Aksoy] A missing shader, a wrong switch and a wrong value each fault; the numbers past those wanted are not judged.
test("a state differing from the wanted one names how", () => {
  const states = { u3f: { on: true, uniforms: { c: [0, 0, 1, 1] } } };
  assert.deepEqual(stateFaults(states, { u3f: { on: true, uniforms: { c: [0, 0, 1] } } }, "p"), []);
  assert.deepEqual(stateFaults(states, { u3f: { on: false, uniforms: { c: [1, 0, 1] } }, u1f: { on: false } }, "p"),
    ["p has u3f on, not off", "p has u3f's c at 0,0,1, not 1,0,1", "p lists no u1f"]);
  assert.deepEqual(stateFaults(states, { u3f: { on: true, uniforms: { x: [1] } } }, "p"), ["p has u3f's x at , not 1"]);
});

// [Doruk-Aksoy] Only extended commands 75 and 76 count, as netcap's lines name them.
test("a netcap's shader commands are counted", () => {
  const text = "100 0 r 232 76 20 e84c\n100 1 r 232 76 20 e84c\n101 0 r 232 75 9 e84b\n102 0 r 232 74 6 e84a\n103 0 u 50 -1 4 32\nshort\n";
  assert.deepEqual(countShaderCommands(text), { enabled: 1, uniform: 2 });
});

// [Doruk-Aksoy] Two clocks agree within a second of the time between their reads; one restarted at a join does not.
test("a joiner's clock agrees with an earlier player's", () => {
  assert.equal(clocksAgree({ at: 1000, since: 400 }, { at: 700, since: 90 }, 10), true);
  assert.equal(clocksAgree({ at: 1000, since: 400 }, { at: 700, since: 500 }, 10), false);
  assert.equal(clocksAgree({ at: 1000, since: null }, { at: 700, since: 90 }, 10), false);
});
