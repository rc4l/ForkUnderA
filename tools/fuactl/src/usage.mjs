// [rc4l] One command's lines of the CLI's usage text, so `fuactl <cmd> --help` answers without acting.

// [rc4l] The usage lines whose first word is cmd, or the whole text when none is (or no command was given).
export function usageFor(usage, cmd) {
  const lines = String(usage).split("\n").filter((l) => cmd && l.trim().split(/\s+/)[0] === cmd && /^\s/.test(l));
  return lines.length ? lines.join("\n") : usage;
}
