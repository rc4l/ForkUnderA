// [rc4l] Put the game into a state where measuring it is meaningful.
//
// Any measurement that reads pixels or timings is competing with the level: a monster firing puts a
// muzzle flash in frame, a projectile drags a bright sprite across it, blood recolours the floor,
// and a death drops you to a corpse-height view of somewhere else entirely. None of that is
// interesting when the question is "what colour is this room", and all of it moves the numbers.
//
// So: god (survive), sv_fua_friendlymonsters (nothing fights you at all), and fly (a destination
// picked from map geometry is often mid-air, and without fly you fall out of it before the first
// frame). All three default ON because every one of them was added after its absence corrupted a
// measurement; each can be switched off individually when the thing being tested IS that mechanic.
//
// friendlymonsters rather than `notarget`: notarget only blocks a monster ACQUIRING a target, so
// anything already locked on keeps chasing and firing, and it is a toggle, so running twice disarms
// it. See features/friendly-monsters/README.md.
//
// Deliberately NOT freezing the world. `freeze` stops thinkers, which also stops the things a
// measurement sometimes depends on, like a skybox camera rendering or a scrolling sky advancing.
// Quiet is wanted here, not stopped.

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// god and fly are TOGGLES, not setters: issuing "god" twice turns it back off. So the state is read
// from what the engine prints rather than assumed, and the toggle is only re-sent when it went the
// wrong way. Assuming worked right up until a caller ran twice against one instance and disarmed it.
// The patterns are the engine's real strings, taken from language.enu (TXT_LIGHTER / TXT_GRAVITY)
// and from STSTR_DQDON/OFF, not from memory: a guessed "Gravity kicks in" matched nothing, so an
// already-flying player got fly toggled OFF and the tool reported it as never applied.
const CHEATS = {
  god:      { on: /Degreelessness Mode ON/i, off: /Degreelessness Mode OFF/i },
  // Both, not one or the other. The friendly flag stops them ATTACKING, but they still wake, alert
  // and make their see-sounds; notarget stops them noticing you at all. Neither alone is quiet.
  notarget: { on: /notarget ON/i,            off: /notarget OFF/i },
  fly:      { on: /You feel lighter/i,       off: /Gravity weighs you down/i },
};

// Cheats are per-player state and do not survive a level change, so this has to be re-applied after
// every `map`. Cheap enough that callers should just do it unconditionally.
export async function makeUndisturbed(c, { quietMs = 400, god = true, notarget = true, fly = true, spectate = false } = {}) {
  const want = { god, notarget, fly };
  const applied = {};

  for (const [name, patterns] of Object.entries(CHEATS)) {
    if (!want[name]) continue;

    const lines = [];
    const off = c.onEvent((n, d) => { if (n === "out" && d && d.text) lines.push(d.text.trim()); });
    await c.rpc("console.exec", { text: name });
    await sleep(250);
    off();

    const said = lines.join("\n");
    if (patterns.off.test(said)) {
      // It was already on and we just turned it off. Undo that.
      await c.rpc("console.exec", { text: name });
      await sleep(200);
    }
    applied[name] = patterns.on.test(said) || patterns.off.test(said);
  }

  // A cvar, so this is idempotent -- no read-back, no re-arming, and running twice cannot disarm it.
  // The `notarget` option name is kept because that is what a caller means by it.
  if (notarget) {
    await c.rpc("console.exec", { text: "sv_fua_friendlymonsters 1" });
    await sleep(250);
    applied.friendlymonsters = true;
  }

  // Last, because it supersedes the rest: a spectator has no body to shoot at, walks through
  // geometry and already flies. Idempotent by construction, since the CCMD returns early on
  // "Already a spectator!" rather than toggling back to alive.
  if (spectate) {
    await c.rpc("console.exec", { text: "spectate" });
    await sleep(400);
    applied.spectate = true;
  }

  // A beat for anything already in flight to land or expire before the caller starts reading.
  await sleep(quietMs);
  return applied;
}

// Change level and come up protected.
//
// The ordering here is the whole point, and getting it wrong is not obvious from reading the code
// that does. Cheats are per-player and reset with the player, so `god` issued once at connect is
// gone the moment the level changes: the player then stands at the spawn, mortal and targetable,
// for however long the caller waits for the level to settle. On a busy map that is long enough to
// die in, and a dead player is a black or corpse-height view that still screenshots perfectly
// happily -- one such capture came back pure black and was very nearly read as a dark room.
//
// So the settle is split: only long enough to be sure the level is live, cheats, then the rest.
export async function loadMap(c, map, { liveMs = 2500, settleMs = 6000 } = {}) {
  await c.rpc("console.exec", { text: `map ${map}` });

  // Enough for P_SetupLevel and the player to exist -- cheats before this land on nobody.
  await sleep(liveMs);
  await makeUndisturbed(c, { quietMs: 0 });

  // Now the rest of the wait happens with nothing shooting.
  await sleep(settleMs);
}

// Warp somewhere and settle. Returns the position the engine actually put you at, or null when the
// caller gave no usable destination -- so a probe can say "measured wherever I happened to be"
// rather than quietly implying otherwise.
//
// Goes through player.setpos rather than the `warp` cheat because `warp` is x/y only and hardcodes
// ONFLOORZ (c_cmds.cpp), so it cannot put the view in the air or point it anywhere. z, angle and
// pitch are absolute and each optional; omitting z still drops to the floor, same as `warp`.
export async function warpTo(c, x, y, { z, angle, pitch, settleMs = 1200 } = {}) {
  if (!Number.isFinite(x) || !Number.isFinite(y))
    return null;

  const req = { x: Math.round(x), y: Math.round(y) };
  if (Number.isFinite(z)) req.z = z;
  if (Number.isFinite(angle)) req.angle = angle;
  if (Number.isFinite(pitch)) req.pitch = pitch;

  const at = await c.rpc("player.setpos", req);

  // The engine clamps z into the space the pawn fits in, so what comes back is not always what was
  // asked for. Callers get the real position, not the request echoed back at them.
  await sleep(settleMs);
  return at;
}

// Ask the engine where outdoors is on this level. features/sky-tint works this out during its
// rebuild (largest sky-lit BSP leaf) and prints it as a ready-made warp command.
export async function findOutdoorSpot(c, { waitMs = 900, enable = true } = {}) {
  // The spot is a by-product of the sky-tint table, which is only built while the feature is on. With
  // it off the diagnostic answers honestly that nothing sees sky, which reads exactly like a level
  // with no outdoors -- so turn it on first rather than report a wrong answer confidently.
  if (enable) {
    await c.rpc("console.exec", { text: "cl_fua_skytint 1" });
    await sleep(600);
  }

  const lines = [];
  const off = c.onEvent((n, d) => { if (n === "out" && d && d.text) lines.push(d.text.trim()); });
  await c.rpc("console.exec", { text: "fua_skytintinfo" });
  await sleep(waitMs);
  off();

  const joined = lines.filter((l) => l.startsWith("skytint:")).join(" ");
  const m = /warp (-?\d+) (-?\d+)/.exec(joined);
  if (!m)
    return null;

  return { x: Number(m[1]), y: Number(m[2]), info: joined };
}
