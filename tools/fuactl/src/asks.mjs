// [MGOOOOOO] The calls the game thread asked of the main thread, as fua_gamethread_report counts them, and the judgement of a stretch that should ask at most a few: each ask holds the tic until the main thread's next frame.

// [MGOOOOOO] The calls asked of the main thread so far, from fua_gamethread_report's line, or null when it printed none.
export function parseAsks(text) {
  const m = /(\d+) calls asked of the main thread/.exec(String(text));
  return m ? Number(m[1]) : null;
}

// [MGOOOOOO] What is wrong with a stretch, given the counts read before and after it and the most it may ask.
export function judgeAsks(before, after, max) {
  if (before === null || after === null) return ["fua_gamethread_report printed no ask count"];
  const asked = after - before;
  return asked > max ? [`asked the main thread ${asked} times, more than ${max}`] : [];
}

// [MGOOOOOO] The ask count read from a launch.
export async function readAsks(cap, c) {
  return parseAsks(await cap.exec(c, "fua_gamethread_report", { maxMs: 6000 }));
}
