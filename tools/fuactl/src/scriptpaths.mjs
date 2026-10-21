// [rc4l] Paths the standalone scripts in scenarios/ share, taken from this checkout rather than one machine's drive.
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { sweepDirFor } from "./shot.mjs";
import { slashed } from "./launch.mjs";

export const FUACTL_DIR = slashed(path.resolve(fileURLToPath(new URL("..", import.meta.url))));
export const CLI = `${FUACTL_DIR}/src/cli.mjs`;
export const PLAY_SESSION = `${FUACTL_DIR}/.play-session`;
export const STAGED_ENGINE = slashed(path.resolve(FUACTL_DIR, "../../dist-windows/forkundera.exe"));
export const STAGED_SWEEP = sweepDirFor(STAGED_ENGINE);

// [rc4l] Where a script keeps what it reads back between runs: FUACTL_SCRATCH, else the system temp folder.
export function scratchDir(env = process.env) {
  return env.FUACTL_SCRATCH || slashed(os.tmpdir());
}
