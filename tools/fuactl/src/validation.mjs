// [Doruk-Aksoy] The Khronos validation layer under driven launches: FUACTL_VALIDATE turns fua_dg_validation on at every launch, and each stopped instance's log is read for the layer's messages, which `scenarios --validate` holds to none.
import fs from "node:fs";

// [Doruk-Aksoy] What FUACTL_VALIDATE asks for: "" when unset or 0, "sync" for the synchronization checks too, "gpu" for the checks instrumented into shaders (every descriptor index a draw reads), else "core".
export function validationMode(env = process.env) {
  const v = String(env.FUACTL_VALIDATE || "").trim().toLowerCase();
  if (!v || v === "0") return "";
  return v === "sync" || v === "gpu" ? v : "core";
}

// [Doruk-Aksoy] The command-line words and environment a launch under that mode takes: the layer from the device's creation, with its synchronization checks for sync and its shader-instrumented checks for gpu.
export function validationLaunch(mode) {
  if (!mode) return { args: [], env: {} };
  const env = mode === "sync" ? { VK_LAYER_VALIDATE_SYNC: "1" } : mode === "gpu" ? { VK_LAYER_GPUAV_ENABLE: "1" } : {};
  return { args: ["+set", "fua_dg_validation", "1"], env };
}

// [Doruk-Aksoy] Messages a verdict leaves out, each matched by its id and by text on the line after it, so a real hazard of the same kind still counts, with why it is left out.
export const KNOWN_VALIDATION = [
  { id: "SYNC-HAZARD-WRITE-AFTER-READ", text: "previously accessed by vkAcquireNextImageKHR",
    why: "Diligent moves a swapchain image out of its undefined layout on first use with a TOP_OF_PIPE source stage, before the acquire semaphore's wait; once per image a launch, on contents discarded anyway" },
];

// [Doruk-Aksoy] The layer's messages in an engine log as the console prints them ("[Diligent ERROR] Vulkan debug message (validation): <id>", its text on the next line), counted by severity and id, most frequent first, those `known` matches left out.
export function parseValidationMessages(text, known = KNOWN_VALIDATION) {
  const counts = new Map();
  for (const m of String(text).matchAll(/\[Diligent (ERROR|warn)\] Vulkan debug message \(([^)]*)\): (\S+)(?=[^\n]*\n?([^\n]*))/g)) {
    if (known.some((k) => k.id === m[3] && m[4].includes(k.text))) continue;
    const severity = m[1] === "ERROR" ? "error" : "warning";
    const key = `${severity} ${m[3]}`;
    const e = counts.get(key) || { severity, kind: m[2], id: m[3], count: 0 };
    e.count++;
    counts.set(key, e);
  }
  return [...counts.values()].sort((a, b) => b.count - a.count || a.id.localeCompare(b.id));
}

const reports = [];

// [Doruk-Aksoy] Reads a stopped instance's log for the layer's messages when it ran under validation, keeping them for takeValidationReports.
export function noteValidation(inst, read = (file) => fs.readFileSync(file, "utf8")) {
  if (!inst || !inst.validate || !inst.log) return;
  let text;
  try { text = read(inst.log); } catch { return; }
  const messages = parseValidationMessages(text);
  if (messages.length) reports.push({ log: inst.log, messages });
}

// [Doruk-Aksoy] The reports kept since the last call, which this empties.
export function takeValidationReports() {
  return reports.splice(0, reports.length);
}

// [Doruk-Aksoy] One line for a run's reports, each id's count summed over its launches, errors first, and the logs to read them in; empty when there are none.
export function describeValidation(list) {
  const total = new Map();
  for (const r of list) for (const m of r.messages) {
    const key = `${m.severity} ${m.id}`;
    const e = total.get(key) || { ...m, count: 0 };
    e.count += m.count;
    total.set(key, e);
  }
  if (!total.size) return [];
  const rows = [...total.values()].sort((a, b) => (a.severity === b.severity ? 0 : a.severity === "error" ? -1 : 1) || b.count - a.count || a.id.localeCompare(b.id));
  const sum = rows.reduce((n, m) => n + m.count, 0);
  return [`FAIL validation: ${sum} message(s): ${rows.map((m) => `${m.id} ${m.severity} x${m.count}`).join(", ")} (${list.map((r) => r.log).join(", ")})`];
}
