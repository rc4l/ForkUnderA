// [Doruk-Aksoy] A hosted map whose server script changes the song (ACS SetMusic), halves its volume (SetMusicVolume), then changes the song again, which drops the factor: exit 0 when after each step the player in the game and a client joining then hear the same song at the same volume, and each client's music cache keeps the pk3 side's deflated songs (none of the wad's stored ones) and inflates none at a change, muted.
//
// usage:  node scenarios/netmusic.mjs
import { Buffer } from "node:buffer";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { acsScripts, callFunc, PCD } from "../src/acs.mjs";
import { addClient } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";

const SETMUSIC = 153, SET_MUSIC_VOLUME = 97;
export const SONGS = ["FUASONG1", "FUASONG2", "FUASONG3"];
const MAPINFO = `map MAP01 "Music join test"\n{\n\tmusic = "${SONGS[0]}"\n}\n`;

// [Doruk-Aksoy] Each server script, in order, and the song and volume every client must hear after it, as parseHeard writes them.
export const STEPS = [
  { name: "SetMusic(FUASONG2)", script: 901, want: "FUASONG2 @ 1.00", changesSong: true },
  { name: "SetMusicVolume(0.5)", script: 902, want: "FUASONG2 @ 0.50", changesSong: false },
  { name: "SetMusic(FUASONG3)", script: 903, want: "FUASONG3 @ 1.00", changesSong: true },
];

// [Doruk-Aksoy] Clients that play the songs, since a song never started keeps the old factor, at a volume nobody hears.
export const QUIET = { snd_sfxvolume: 0, snd_musicvolume: 0.001 };

// [Doruk-Aksoy] Seconds of 8-bit mono silence as a WAV, the smallest song the engine streams and starts.
export function silentWav(seconds = 1, rate = 11025) {
  const n = Math.round(seconds * rate);
  const b = Buffer.alloc(44 + n, 0x80);
  b.write("RIFF", 0, "latin1");
  b.writeUInt32LE(36 + n, 4);
  b.write("WAVEfmt ", 8, "latin1");
  b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20);
  b.writeUInt16LE(1, 22);
  b.writeUInt32LE(rate, 24);
  b.writeUInt32LE(rate, 28);
  b.writeUInt16LE(1, 32);
  b.writeUInt16LE(8, 34);
  b.write("data", 36, "latin1");
  b.writeUInt32LE(n, 40);
  return b;
}

// [Doruk-Aksoy] One room with four starts and STEPS' scripts, as a map's lumps.
function musicMapLumps() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [i, y] of [[1, 96], [2, 192], [3, 288], [4, 384]]) m.thing({ x: 96, y, angle: 0, type: i });
  const setMusic = (i) => [PCD.PUSHNUMBER, i, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0, SETMUSIC, PCD.TERMINATE];
  const behavior = acsScripts([
    { open: false, number: 901, code: setMusic(0) },
    { open: false, number: 902, code: [...callFunc(SET_MUSIC_VOLUME, [0.5 * 65536]), PCD.DROP, PCD.TERMINATE] },
    { open: false, number: 903, code: setMusic(1) },
  ], [SONGS[1], SONGS[2]]);
  return mapLumps("MAP01", m, { behavior });
}

// [Doruk-Aksoy] MAPINFO, the songs and the map as lumps of one wad, which stores every song.
export function buildNetMusicWad() {
  return wad([{ name: "MAPINFO", data: MAPINFO }, ...SONGS.map((name) => ({ name, data: silentWav() })), ...musicMapLumps()]);
}

// [Doruk-Aksoy] The same as a modern mod ships it: a pk3 with every file deflated, the songs in music/ and the map in maps/.
export function buildNetMusicPk3() {
  return pk3([
    { name: "mapinfo.txt", data: MAPINFO },
    ...SONGS.map((name) => ({ name: `music/${name.toLowerCase()}.wav`, data: silentWav() })),
    { name: "maps/map01.wad", data: wad(musicMapLumps()) },
  ], { deflate: true });
}

// [Doruk-Aksoy] The song a client plays and its relative volume, from what changemus and testmusicvol printed, as "SONG @ 0.50".
export function parseHeard(songText, volumeText) {
  const song = /currently playing (\S+)/i.exec(songText);
  const volume = /Current relative volume is ([0-9.]+)/.exec(volumeText);
  return `${song ? song[1].toUpperCase() : "nothing"} @ ${volume ? volume[1] : "?"}`;
}

// [Doruk-Aksoy] What fua_musiccache printed: the songs kept, and the music changes since the precache that played bytes in memory or inflated a song, null where it printed none.
export function parseMusicCache(text) {
  const kept = /(\d+) songs kept/.exec(text);
  const changes = /precache: (\d+) from memory, (\d+) inflated/.exec(text);
  return { kept: kept ? Number(kept[1]) : null, fromMemory: changes ? Number(changes[1]) : null, inflated: changes ? Number(changes[2]) : null };
}

// [Doruk-Aksoy] What is wrong with the caches: the server keeps none, each client keeps the side's songs and inflates none at a change, and the player's changes play from memory exactly when it keeps any.
export function cacheFaults({ server, player, joiners }, songsKept) {
  const faults = [];
  if (server.kept !== 0) faults.push(`the server keeps ${server.kept ?? "?"} songs, not 0`);
  for (const [who, r] of [["the player", player], ...joiners.map((r, i) => [`late joiner ${i + 1}`, r])]) {
    if (r.kept !== songsKept) faults.push(`${who} keeps ${r.kept ?? "?"} songs, not ${songsKept}`);
    if (r.inflated !== 0) faults.push(`${who} inflated a song at ${r.inflated ?? "?"} music changes`);
  }
  const changes = STEPS.filter((s) => s.changesSong).length;
  if (songsKept ? !(player.fromMemory >= changes) : player.fromMemory !== 0) {
    faults.push(`the player played ${player.fromMemory ?? "?"} music changes from memory, not ${songsKept ? `at least ${changes}` : 0}`);
  }
  return faults;
}

// [Doruk-Aksoy] The verdict from what the player and each step's late joiner heard, against what the step leaves, and from the caches.
export function judgeNetMusic(label, { rows, caches, songsKept }) {
  const heardFaults = rows.flatMap((r) => [["the player", r.player], ["the client joining then", r.joiner]]
    .filter(([, got]) => got !== r.want).map(([who, got]) => `after ${r.step}, ${who} hears ${got}, not ${r.want}`));
  const faults = [...heardFaults, ...cacheFaults(caches, songsKept)];
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: the player and a client joining after each of ${rows.length} steps hear ${rows.map((r) => r.want).join(", then ")}; each keeps ${songsKept} songs and inflated none at a change`, failed: 0 };
}

async function heard(c) {
  return parseHeard(await cap.exec(c, "changemus"), await cap.exec(c, "testmusicvol"));
}

const cacheOf = async (c) => parseMusicCache(await cap.exec(c, "fua_musiccache", { quietMs: 300 }));

// [Doruk-Aksoy] A client that plays its music at an audible volume or its sounds at all is refused before anything starts.
export async function assertQuiet(c, who) {
  const text = (await cap.exec(c, "snd_musicvolume")) + (await cap.exec(c, "snd_sfxvolume"));
  if (!/"snd_musicvolume" is "0\.001"/.test(text) || !/"snd_sfxvolume" is "0"/.test(text)) throw new Error(`${who} is not muted: ${text.trim()}`);
}

export async function judgeNetplay(set, label, { config }) {
  const sc = set.server.c;
  const [player] = set.clients;
  await assertQuiet(player.c, "the player");
  const start = await heard(player.c);
  if (start !== `${SONGS[0]} @ 1.00`) return { line: `FAIL ${label}: the player starts hearing ${start}, not MAPINFO's ${SONGS[0]} @ 1.00`, failed: 1 };
  const rows = [], joiners = [];
  for (const step of STEPS) {
    await cap.exec(sc, `puke ${step.script}`);
    await cap.waitTics(sc, 35);
    const late = await addClient(set, { role: "latejoin", cvars: config.clientCvars });
    await assertQuiet(late.c, "the late joiner");
    await cap.waitTics(late.c, 35);
    rows.push({ step: step.name, want: step.want, player: await heard(player.c), joiner: await heard(late.c) });
    joiners.push(await cacheOf(late.c));
  }
  const caches = { server: await cacheOf(sc), player: await cacheOf(player.c), joiners };
  return judgeNetMusic(label, { rows, caches, songsKept: config.songsKept });
}

// [Doruk-Aksoy] The songs stored in a wad and deflated in a pk3; --side wad --cvar fua_debug_netdrop=232/74 leaves the volume command unsent and --side pk3 --cvar snd_fua_musiccache=0 keeps no song, each failing.
export const CONFIGS = [
  { name: "wad", players: 1, clientCvars: QUIET, songsKept: 0 },
  { name: "pk3", players: 1, clientCvars: QUIET, songsKept: SONGS.length, wad: buildNetMusicPk3() },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netmusic", wad: buildNetMusicWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
