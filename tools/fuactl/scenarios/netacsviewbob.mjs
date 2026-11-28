// [Doruk-Aksoy] fviewbob in a netgame: the player's client flies up and hovers, and exit 0 when the server moves its pawn as the client's userinfo says,
// bobbing with fviewbob 1 and still with fviewbob 0, and the client's own prediction moves it the same way, on a LAN and at 100 ms.
//
// usage:  node scenarios/netacsviewbob.mjs
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { onSnapshots, result } from "../src/netacs.mjs";
import { buildViewBobWad, judgeHover, riseAndHover, sampleHover, cameraOf } from "./acsviewbob.mjs";

// [Doruk-Aksoy] The server's copy of the first player's pawn, from world.actors; its view is left out, which only the client draws.
export const serverPawnOf = (c) => async () => {
  const row = ((await c.rpc("world.actors", { players: 1 }).catch(() => null))?.actors || []).find((a) => a.player === 0);
  return row ? { z: row.z, view: 0 } : null;
};

// [Doruk-Aksoy] What is wrong with the server's pawn heights alone, the view never moving there.
export function judgeServer(samples, bobs) {
  return judgeHover(samples.map((s) => ({ ...s, view: bobs ? 1 : 0 })), bobs).filter((f) => !/^the view/.test(f));
}

export async function judgeNetplay(set, label, { config } = {}, deps = {}) {
  const { rise = riseAndHover, sample = sampleHover } = deps;
  const bobs = config?.clientCvars?.fviewbob !== 0;
  const player = set.clients[0];
  await rise(player.c, set.server.c);
  const faults = [...judgeServer(await sample(serverPawnOf(set.server.c), set.server.c), bobs).map((f) => `the server: ${f}`),
    ...judgeHover(await sample(cameraOf(player.c), set.server.c), bobs).map((f) => `the player's client: ${f}`)];
  return result(label, faults, `the server moved the hovering pawn as the client's fviewbob ${bobs ? 1 : 0} says, and the client predicted it the same`);
}

export const CONFIGS = onSnapshots([
  { name: "on", players: 1, observers: 0, clientCvars: { fviewbob: 1 } },
  { name: "off", players: 1, observers: 0, clientCvars: { fviewbob: 0 } },
  { name: "off-ping100", players: 1, observers: 0, lag: "100", clientCvars: { fviewbob: 0 } },
]);

runScenarioIfMain(import.meta.url, () => ({ name: "netacsviewbob", wad: buildViewBobWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
