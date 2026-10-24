import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import { parseCaptureLine, parseCapture, commandNames, commandName, countByName, streams, diffCaptures, diffCaptureLines, recordCapture, ENUMS } from "../src/netcap.mjs";

const ENUM_SOURCE = `
BEGIN_ENUM( ServerConnectionCommand )
{
	ENUM_ELEMENT( SVCC_AUTHENTICATE ),
	ENUM_ELEMENT( SVCC_MAPLOAD ),
	ENUM_ELEMENT( SVCC_ERROR ),
	ENUM_ELEMENT( NUM_SERVERCONNECT_COMMANDS )
}
END_ENUM( ServerConnectionCommand )
BEGIN_ENUM( SVC )
{
	ENUM_ELEMENT2 ( SVC_HEADER, NUM_SERVERCONNECT_COMMANDS ),	// GENERAL PROTOCOL COMMANDS
	ENUM_ELEMENT ( SVC_PING ),
	ENUM_ELEMENT ( SVC_EXTENDEDCOMMAND ),
}
END_ENUM( SVC )
BEGIN_ENUM( SVC2 )
{
	ENUM_ELEMENT ( SVC2_FIRST ),
	ENUM_ELEMENT2 ( SVC2_TEN, 10 ),
	ENUM_ELEMENT ( SVC2_ELEVEN ),
}
END_ENUM( SVC2 )`;

test("a capture line reads into its fields and anything else is skipped", () => {
  assert.deepEqual(parseCaptureLine("350 2 r 5 -1 3 0a0b0c"), { tic: 350, client: 2, reliable: true, id: 5, ext: -1, size: 3, hex: "0a0b0c" });
  assert.equal(parseCaptureLine("7 0 u 5 11 2 ffff").reliable, false);
  assert.equal(parseCaptureLine("[netcap] capturing"), null);
  assert.equal(parseCapture("1 0 r 3 -1 0 \nnoise\r\n2 1 u 4 -1 1 aa\n").length, 2);
});

test("command names come from the enums, SVC after the connection commands and SVC2 from 0", () => {
  const names = commandNames(ENUM_SOURCE);
  assert.equal(names.SVC[3], "SVC_HEADER");
  assert.equal(names.SVC[4], "SVC_PING");
  assert.equal(names.SVC2[0], "SVC2_FIRST");
  assert.equal(names.SVC2[10], "SVC2_TEN");
  assert.equal(names.SVC2[11], "SVC2_ELEVEN");
  assert.equal(commandName(names, 4, -1), "SVC_PING");
  assert.equal(commandName(names, 5, 11), "SVC2_ELEVEN");
  assert.equal(commandName(names, 99, -1), "99");
  assert.equal(commandName(names, 5, 99), "SVC_EXTENDEDCOMMAND/99");
  assert.equal(commandName(names, 98, 2), "98/2");
});

test("the engine's own enums name its ping and its extended command", () => {
  const names = commandNames(fs.readFileSync(ENUMS, "utf8"));
  const ids = Object.fromEntries(Object.entries(names.SVC).map(([k, v]) => [v, Number(k)]));
  assert.equal(ids.SVC_HEADER, 3);
  assert.ok(ids.SVC_PING > ids.SVC_HEADER);
  assert.ok(ids.SVC_EXTENDEDCOMMAND > ids.SVC_PING);
  assert.ok(Object.values(names.SVC2).includes("SVC2_FUADAMAGETAKEN"));
});

test("captures are counted by name and split per client with tics from the first", () => {
  const names = commandNames(ENUM_SOURCE);
  const lines = parseCapture("100 0 r 4 -1 0 \n101 1 u 4 -1 0 \n102 0 r 5 10 1 ab");
  assert.deepEqual(countByName(lines, names), { SVC_PING: 2, SVC2_TEN: 1 });
  assert.deepEqual(streams(lines), { 0: ["0 r 4 -1 ", "2 r 5 10 ab"], 1: ["1 u 4 -1 "] });
  assert.deepEqual(streams([]), {});
});

test("two captures are the same, or differ by counts and where each client's stream parts", () => {
  const names = commandNames(ENUM_SOURCE);
  const a = parseCapture("100 0 r 4 -1 0 \n101 0 r 5 10 1 ab");
  assert.equal(diffCaptures(a, parseCapture("500 0 r 4 -1 0 \n501 0 r 5 10 1 ab"), names).same, true);
  const d = diffCaptures(a, parseCapture("100 0 r 4 -1 0 \n101 0 r 5 10 1 cd\n101 1 r 4 -1 0 "), names);
  assert.equal(d.same, false);
  assert.deepEqual(d.counts, [{ name: "SVC_PING", a: 1, b: 2 }]);
  assert.deepEqual(d.partings.map((p) => [p.client, p.index]), [[0, 1], [1, 0]]);
  const lines = diffCaptureLines(d, names);
  assert.equal(lines[0], "DIFFER");
  assert.ok(lines.includes("  SVC_PING: 1 -> 2"));
  assert.ok(lines.includes("  client 0 parts at command 1: tic +1 reliable SVC2_TEN vs tic +1 reliable SVC2_TEN"));
  assert.ok(lines.includes("  client 1 parts at command 0: (nothing) vs tic +1 reliable SVC_PING"));
  assert.deepEqual(diffCaptureLines({ same: true }, names), ["SAME: every client was sent the same commands in the same order"]);
});

test("a capture is started, waited out and stopped, or refused when the server will not start it", async () => {
  const said = [];
  const exec = async (c, text) => { said.push(text); return text.startsWith("fua_netcap_start") ? "[netcap] capturing to x.netcap" : "[netcap] 3 commands captured"; };
  const file = await recordCapture({}, "out", { seconds: 0, exec, sleep: async () => {} });
  assert.equal(file, "out.netcap");
  assert.match(said[0], /^fua_netcap_start ".*out\.netcap"$/);
  assert.equal(said[1], "fua_netcap_stop");
  await assert.rejects(recordCapture({}, "x.netcap", { seconds: 0, exec: async () => "[netcap] cannot write", sleep: async () => {} }), /did not start capturing/);
});
