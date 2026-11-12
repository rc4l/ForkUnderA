// [rc4l] The server registry on loopback (zandronum@c461fe1ca6dc's receive and send): exit 0 when a server hosted here announces to a registry run beside it, the registry takes it onto its verification list, then its server list, and the server hears the registry answer.
//
// usage:  node scenarios/registryhost.mjs [--engine E]   (FUACTL_REGISTRY names another registry than the one beside E)
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";
import { FREEDOOM2, writeTemplateFile, runScenarioIfMain } from "../src/template.mjs";
import { resolveEngine, launchInstance, stopInstance } from "../src/launch.mjs";
import { attach } from "../src/session.mjs";
import { scaled } from "../src/timeouts.mjs";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };

export function buildRegistryHostPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 128, y: 128, angle: 90, type: 1 });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] The registry beside the engine's build: build-win/Release/forkundera.exe has build-win/server-registry/Release/forkundera-server-registry.exe.
export function registryBesideEngine(engine) {
  return path.join(path.dirname(engine), "..", "server-registry", path.basename(path.dirname(engine)), "forkundera-server-registry.exe");
}

// [rc4l] What the registry's log and the host's diagnosis leave unproven: the announce arrived, the verification came back, the host heard it.
export function judgeRegistryHost(log, gamePort, diag) {
  const at = `127.0.0.1:${gamePort}`;
  const faults = [];
  if (!log.includes(`+ Adding ${at}`) || !/to the verification list/.test(log)) faults.push(`the registry never put ${at} on its verification list`);
  else if (!new RegExp(`\\+ Adding ${at.replace(/\./g, "\\.")} \\(revision -?\\d+\\) to the server list`).test(log)) faults.push(`the registry never listed ${at} after verifying it`);
  if (!diag?.registryReplied) faults.push("the host never heard the registry answer");
  return faults;
}

// [rc4l] Until the registry has listed the server and the host has heard it, or the deadline.
async function waitListed(c, logOf, gamePort, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let diag = null;
  while (Date.now() < deadline) {
    diag = await c.rpc("net.hostdiag", {}).catch(() => null);
    if (judgeRegistryHost(logOf(), gamePort, diag).length === 0) break;
    await cap.sleep(1000);
  }
  return diag;
}

// [rc4l] The registry run in a scratch folder, one server hosted against it, judged from the registry's log and the host's diagnosis.
export async function compareRegistryHost({ wad, port = 7880, gamePort = 10880, engine = resolveEngine(), registry = process.env.FUACTL_REGISTRY || registryBesideEngine(engine) }) {
  if (!fs.existsSync(registry)) return { lines: [`FAIL registry: none at ${registry}`], failed: 1 };
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-registry-"));
  let log = "";
  // [rc4l] Resolved here, since the registry runs in the scratch folder.
  const reg = spawn(path.resolve(registry), [], { cwd: dir, stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
  reg.on("error", (e) => { log += `could not start: ${e.message}\n`; });
  reg.stdout.on("data", (d) => { log += d; });
  reg.stderr.on("data", (d) => { log += d; });
  let inst = null, c = null;
  try {
    inst = await launchInstance({ engine, port, iwad: FREEDOOM2, map: "MAP01",
      extraArgs: ["-file", writeTemplateFile("registryhost", wad), "-host", "-port", String(gamePort), "+sv_broadcast", "0", "+fua_serverregistry_host", "127.0.0.1", "+sv_fua_serverregistry_announce", "1"] });
    c = await attach(inst);
    await cap.waitInLevel(c, scaled(60000));
    const faults = judgeRegistryHost(log, gamePort, await waitListed(c, () => log, gamePort, scaled(45000)));
    // [rc4l] The registry's own log with a failure, since it alone says how far the announce got.
    return { lines: faults.length ? [`FAIL registry: ${faults.join("; ")}`, ...log.trim().split(/\r?\n/).slice(-12).map((l) => `  registry: ${l}`)]
      : [`PASS registry: 127.0.0.1:${gamePort} verified, listed and answered`], failed: faults.length ? 1 : 0 };
  } catch (e) {
    return { lines: [`FAIL registry: ${e.message}`], failed: 1 };
  } finally {
    if (c) c.close();
    if (inst) await stopInstance(inst);
    reg.kill();
  }
}
compareRegistryHost.judgedAlone = true;

runScenarioIfMain(import.meta.url, () => ({ name: "registryhost", wad: buildRegistryHostPk3(), port: 7880 }), compareRegistryHost);
