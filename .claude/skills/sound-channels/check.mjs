// Spawns make_pk3.py's emitters in a running engine and judges what listsoundchannels reports against each channel rule; in a netgame the late joiner is judged too, against the server's looping record.
// usage: node check.mjs <pk3> <iwad> [--net] [--only Emitter,...]   (FUACTL_ENGINE picks the binary; exit 1 on any failure)
import { launchInstance, stopInstance } from "../../../tools/fuactl/src/launch.mjs";
import { attach } from "../../../tools/fuactl/src/session.mjs";
import { launchNetplay, addClient, closeNetplay } from "../../../tools/fuactl/src/netplay.mjs";
import * as cap from "../../../tools/fuactl/src/capture.mjs";

const positional = [];
let net = false, only = null;
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i];
  if (a === "--net") net = true;
  else if (a === "--only") only = process.argv[++i].split(",");
  else positional.push(a);
}
const [pk3, iwad] = positional;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// Up to 31 test tones loop at once, so every instance runs muted; the checks read channels and flags, never levels.
const MUTED = { snd_sfxvolume: 0, snd_musicvolume: 0 };

// What each emitter must leave playing once its one-shots (1 s) are over: sound -> copies, channel ("4+" = slot 4, overlapping), and flag bits it must carry.
const RULES = {
  FuaOvAuto: { what: "CHAN_AUTO overlaps on channel 0, ten at once (the old cap was 8)", expect: Object.fromEntries(Array.from({ length: 10 }, (_, i) => [`fua/ova${i + 1}`, { copies: 1, channel: "0+" }])) },
  FuaOvReplace: { what: "a plain loop replaces the slot's", expect: { "fua/ovr2": { copies: 1, channel: "4" } }, gone: ["fua/ovr1"] },
  FuaOvOverlap: { what: "CHAN_OVERLAP loops share the slot", expect: { "fua/ovo1": { copies: 1, channel: "4+" }, "fua/ovo2": { copies: 1, channel: "4+" } } },
  FuaOvReplaceAll: { what: "a plain loop replaces every overlapped sound on the slot", expect: { "fua/ovp3": { copies: 1, channel: "4" } }, gone: ["fua/ovp1", "fua/ovp2"] },
  FuaOvNoRestack: { what: "the same overlapping loop is not stacked twice", expect: { "fua/ovn1": { copies: 1, channel: "4+" } } },
  FuaOvOneShot: { what: "an overlapping one-shot leaves the slot's loop playing", expect: { "fua/ovq1": { copies: 1, channel: "4" } } },
  FuaOvCut: { what: "a plain one-shot cuts the slot's loop", expect: {}, gone: ["fua/ovc1"] },
  FuaOvNoStop: { what: "CHAN_NOSTOP skips a sound already playing on the slot", expect: { "fua/ovx1": { copies: 1, channel: "4" } } },
  FuaOvNoStopOther: { what: "CHANF_NOSTOP lets a different sound start, as uzdoom does", expect: { "fua/ovy1": { copies: 1, channel: "4" }, "fua/ovy2": { copies: 1, channel: "4+" } } },
  FuaOvLoopFlag: { what: "CHAN_LOOP in the slot loops a sound", expect: { "fua/ovl1": { copies: 1, channel: "4" } } },
  FuaOvPickup: { what: "CHAN_PICKUP is slot 3 with CHAN_MAYBE_LOCAL", expect: { "fua/ovk1": { copies: 1, channel: "3", flags: 0x10 } } },
  FuaOvLooping: { what: "CHANF_LOOPING loops once", expect: { "fua/ovm1": { copies: 1, channel: "4" } } },
  FuaOvUI: { what: "CHANF_UI reaches the sound", expect: { "fua/ovu1": { copies: 1, channel: "4", flags: 0x20 } } },
};
// Checked on its own: it stops its slot 3 s after spawning, so it is judged before (both play) and after (none).
const START = { what: "A_StartSound CHANF_OVERLAP shares the slot, and A_StopSound(slot) stops all of it", expect: { "fua/ovs1": { copies: 1, channel: "4+" }, "fua/ovs2": { copies: 1, channel: "4+" } } };

// listsoundchannels' fua/ lines as { name: [{ channel, flags }] }; channel and flags are null from a binary that predates them, whose copies still count.
function playing(text) {
  const out = {};
  for (const m of text.matchAll(/^(fua\/\S+) at \([^)]*\)(?:, channel (\d+)(\+?), flags 0x([0-9a-f]+))?/gm)) {
    (out[m[1]] ||= []).push({ channel: m[2] === undefined ? null : m[2] + m[3], flags: m[4] === undefined ? null : parseInt(m[4], 16) });
  }
  return out;
}

// One line per rule, PASS or FAIL with what differed; true when all passed.
function judge(label, sounds, rules) {
  let ok = true;
  console.log(`-- ${label}`);
  for (const [name, rule] of Object.entries(rules)) {
    const bad = [];
    for (const [s, e] of Object.entries(rule.expect)) {
      const got = sounds[s] || [];
      if (got.length !== e.copies) bad.push(`${s}: ${got.length} copies, want ${e.copies}`);
      else if (got.length && got[0].channel === null) continue;
      else if (got.length && got[0].channel !== e.channel) bad.push(`${s}: channel ${got[0].channel}, want ${e.channel}`);
      else if (e.flags && got.length && (got[0].flags & e.flags) !== e.flags) bad.push(`${s}: flags 0x${got[0].flags.toString(16)}, want 0x${e.flags.toString(16)} set`);
    }
    for (const s of rule.gone || []) if (sounds[s]) bad.push(`${s} still playing`);
    ok &&= !bad.length;
    console.log(`  ${bad.length ? "FAIL" : "PASS"}  ${name}: ${rule.what}${bad.length ? `  [${bad.join("; ")}]` : ""}`);
  }
  return ok;
}

async function list(c) {
  return playing(await cap.exec(c, "listsoundchannels", { quietMs: 400 }));
}

// Refuses to go on unless the instance really is muted, so a launch that dropped the cvar never plays the tones aloud.
async function assertMuted(c, label) {
  const text = await cap.exec(c, "snd_sfxvolume");
  if (!/"snd_sfxvolume" is "0"/.test(text)) throw new Error(`${label} is not muted (${text.trim()}); refusing to play the test tones aloud`);
}

async function spawnAll(c, names) {
  for (const n of names) await cap.exec(c, `summon ${n}`);
}

const rules = Object.fromEntries(Object.entries(RULES).filter(([n]) => !only || only.includes(n)));
let ok = true;
if (!net) {
  // A locked window is unfocused, so it keeps running as play's --lock does, or its tics and the emitters' states wait for focus.
  const inst = await launchInstance({ iwad, map: "MAP01", port: 7799, extraArgs: ["-file", pk3],
    cvars: { sv_cheats: 1, i_pauseinbackground: 0, fua_render_in_background: 1, ...MUTED } });
  try {
    const c = await attach(inst);
    await cap.waitInLevel(c);
    await assertMuted(c, "the instance");
    if (Object.keys(rules).length) {
      await spawnAll(c, Object.keys(rules));
      await sleep(1800);
      ok = judge("single player", await list(c), rules);
    }
    if (!only || only.includes("FuaOvStart")) {
      await spawnAll(c, ["FuaOvStart"]);
      ok = judge("single player, A_StartSound before its stop", await list(c), { FuaOvStart: START }) && ok;
      await sleep(3500);
      ok = judge("single player, after A_StopSound(CHAN_BODY)", await list(c), { FuaOvStart: { what: START.what, expect: {}, gone: ["fua/ovs1", "fua/ovs2"] } }) && ok;
    }
    c.close();
  } finally {
    await stopInstance(inst);
  }
} else {
  const set = await launchNetplay({ iwad, map: "MAP01", files: [pk3], players: 1, clientCvars: MUTED });
  try {
    const player = set.clients[0];
    await assertMuted(player.c, "the player");
    await spawnAll(player.c, Object.keys(rules));
    await sleep(2500);
    ok = judge("player in a netgame", await list(player.c), rules);
    const late = await addClient(set, { role: "latejoin", cvars: MUTED });
    await assertMuted(late.c, "the late joiner");
    await sleep(2500);
    // The late joiner hears only loops, which the server's record resends; its one-shots were over before it arrived.
    ok = judge("late joiner (the server's looping record)", await list(late.c), rules) && ok;
  } finally {
    await closeNetplay(set);
  }
}
console.log(ok ? "ALL PASS" : "FAILURES");
process.exit(ok ? 0 : 1);
