// [rc4l] Every wait fuactl makes, stretched by FUACTL_TIMEOUT_SCALE for an engine that runs slower than usual, such as one built with ThreadSanitizer.
export function timeoutScale(env = process.env) {
  const s = Number(env.FUACTL_TIMEOUT_SCALE);
  return Number.isFinite(s) && s >= 1 ? s : 1;
}

// [rc4l] A timeout in milliseconds under that scale.
export function scaled(ms, env = process.env) {
  return Math.round(ms * timeoutScale(env));
}
