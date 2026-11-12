// [rc4l] Player.RejumpDelay in a netgame (zandronum@f6f2e6ee1991), from DECORATE and set from ACS: exit 0 when a held jump waits on the floor as long as the delay says, the engine's own 7 tics at -1, on the server and on the client that predicts it.
//
// usage:  node scenarios/netrejumpdelay.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acsScripts, callFunc, ACSF, PCD, SCRIPT, SCRIPTF } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] APROP_RejumpDelay, numbered as Zandronum's.
export const APROP_REJUMPDELAY = 150;
export const STRINGS = ["server ground ", "client ground ", "set ", " "];
const SERVER_AT = 0, CLIENT_AT = 1, SET_AT = 2, SPACE_AT = 3;
export const MEASURE = 1, MEASURE_HERE = 2, SET = 3;
// [rc4l] The gaps each loop logs: the standing start, then two between jumps.
export const GAPS = 3;

// [rc4l] A loop counting the tics the activator stands on the floor at 0 between jumps, logging each count after the prefix at `prefixAt`, in map variables `ground` and `count`.
export function measureScript(prefixAt, ground, count, at) {
  return new AcsScript()
    .label("loop").emit(PCD.PUSHNUMBER, 0, PCD.GETACTORZ, PCD.PUSHNUMBER, 0, PCD.EQ).jump(PCD.IFGOTO, "ground")
    .emit(PCD.PUSHMAPVAR, ground).jump(PCD.IFNOTGOTO, "next")
    .emit(PCD.BEGINPRINT, PCD.PUSHNUMBER, prefixAt, PCD.PRINTSTRING, PCD.PUSHMAPVAR, ground, PCD.PRINTNUMBER, PCD.ENDLOG)
    .emit(PCD.PUSHNUMBER, 0, PCD.ASSIGNMAPVAR, ground, PCD.INCMAPVAR, count, PCD.PUSHMAPVAR, count, PCD.PUSHNUMBER, GAPS, PCD.GE).jump(PCD.IFGOTO, "done")
    .jump(PCD.GOTO, "next")
    .label("ground").emit(PCD.INCMAPVAR, ground)
    .label("next").emit(PCD.DELAYDIRECT, 1).jump(PCD.GOTO, "loop")
    .label("done").emit(PCD.TERMINATE)
    .words(at);
}

// [rc4l] SET sets the activator's RejumpDelay to its first argument when its second is 1, then logs the delay read back and checked against the first.
export function setScript(at) {
  const prop = [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, APROP_REJUMPDELAY];
  return new AcsScript()
    .emit(PCD.PUSHSCRIPTVAR, 1).jump(PCD.IFNOTGOTO, "log")
    .emit(...prop, PCD.PUSHSCRIPTVAR, 0, PCD.SETACTORPROPERTY)
    .label("log").emit(PCD.BEGINPRINT, PCD.PUSHNUMBER, SET_AT, PCD.PRINTSTRING, ...prop, PCD.GETACTORPROPERTY, PCD.PRINTNUMBER, PCD.PUSHNUMBER, SPACE_AT, PCD.PRINTSTRING,
      ...callFunc(ACSF.CheckActorProperty, [0, APROP_REJUMPDELAY, [PCD.PUSHSCRIPTVAR, 0]]), PCD.PRINTNUMBER, PCD.ENDLOG, PCD.TERMINATE)
    .words(at);
}

export function buildScripts() {
  const server = measureScript(SERVER_AT, 0, 1, 0);
  const client = measureScript(CLIENT_AT, 2, 3, server.length);
  return [
    { number: MEASURE, type: SCRIPT.CLOSED, flags: SCRIPTF.NET, code: server },
    { number: MEASURE_HERE, type: SCRIPT.CLOSED, flags: SCRIPTF.NET | SCRIPTF.CLIENTSIDE, code: client },
    { number: SET, type: SCRIPT.CLOSED, flags: SCRIPTF.NET, args: 2, code: setScript(server.length + client.length) },
  ];
}

// [rc4l] A class whose jumps wait 30 tics, which the DECORATE config plays as.
export const SLOW_CLASS = "ZxSlowJumper";
export const DECORATE = `actor ${SLOW_CLASS} : DoomPlayer\n{\n  Player.RejumpDelay 30\n}\n`;

// [rc4l] The map, and with `slow` the class that waits 30 tics as everyone's, which a binary without the property refuses to load.
export function buildNetRejumpDelayPk3({ slow = false } = {}) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  const files = [{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(buildScripts(), STRINGS) })) }];
  if (slow) files.push({ name: "decorate.txt", data: DECORATE }, { name: "mapinfo.txt", data: `gameinfo\n{\n  playerclasses = "${SLOW_CLASS}"\n}\n` });
  return pk3(files);
}

// [rc4l] The gaps a log holds after `prefix`, the standing start dropped.
export function gapsIn(log, prefix) {
  return [...String(log).matchAll(new RegExp(`${prefix} (\\d+)`, "g"))].map((m) => Number(m[1])).slice(1);
}

// [rc4l] What is wrong with one configuration: the delay read back, and each gap within a tic of `want` on both sides.
export function judgeJumps({ set, server, client }, { delay, want }) {
  const faults = [];
  const m = /set (-?\d+) (\d+)/.exec(String(set));
  if (!m) faults.push(`setting RejumpDelay ${delay} logged nothing`);
  else if (Number(m[1]) !== delay || m[2] !== "1") faults.push(`RejumpDelay read back ${m[1]} and checked ${m[2]}, not ${delay} and 1`);
  for (const [who, gaps] of [["server", server], ["client", client]]) {
    if (gaps.length < GAPS - 1) faults.push(`the ${who} logged ${gaps.length} gaps between jumps, not ${GAPS - 1}`);
    else if (gaps.some((g) => Math.abs(g - want) > 1)) faults.push(`the ${who} stood ${gaps.join(" and ")} tics between jumps, not ${want}`);
  }
  return faults;
}

// [rc4l] Each delay and the tics a held jump then stands on the floor: the engine's landing wait of 7, the delay itself, and one tic to see the floor at 0; the DECORATE config only reads its class's delay back.
export const CONFIGS = [
  { name: "engine", players: 1, delay: -1, set: true, want: 7 },
  { name: "decorate", players: 1, delay: 30, set: false, want: 30, wad: buildNetRejumpDelayPk3({ slow: true }) },
  { name: "none", players: 1, delay: 0, set: true, want: 1 },
];

export async function judgeNetplay(set, label, { config }) {
  const sc = set.server.c, cc = set.clients[0].c;
  let setLog = "";
  // [rc4l] A puke can be dropped while the client is still joining, so it is sent until the server answers.
  for (let tries = 0; tries < 5 && !/set -?\d+/.test(setLog); tries++) {
    const heard = cap.listen(sc);
    try {
      await cap.exec(cc, `puke ${SET} ${config.delay} ${config.set ? 1 : 0}`, { quietMs: 60 });
      await cap.waitTics(sc, 35);
    } finally { heard.off(); }
    setLog = heard.text();
  }
  const onServer = cap.listen(sc), onClient = cap.listen(cc);
  try {
    await cap.exec(cc, "+jump", { quietMs: 60 });
    await cap.exec(cc, `puke ${MEASURE}`, { quietMs: 60 });
    await cap.exec(cc, `puke ${MEASURE_HERE}`, { quietMs: 60 });
    for (let waited = 0; waited < 400; waited += 35) {
      await cap.waitTics(sc, 35);
      if (gapsIn(onServer.text(), "server ground").length >= GAPS - 1 && gapsIn(onClient.text(), "client ground").length >= GAPS - 1) break;
    }
    await cap.exec(cc, "-jump", { quietMs: 60 });
  } finally { onServer.off(); onClient.off(); }
  const faults = judgeJumps({ set: setLog, server: gapsIn(onServer.text(), "server ground"), client: gapsIn(onClient.text(), "client ground") }, config);
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: RejumpDelay ${config.delay} kept a held jump ${config.want} tics on the floor on the server and the client`], failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "netrejumpdelay", wad: buildNetRejumpDelayPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
