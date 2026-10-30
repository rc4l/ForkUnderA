// [rc4l] When the loading screen gives way: the engine's start blocks the page, so it waits for the animation to play out, and the screen stays until the game's frames run freely.
'use strict';

(function (root) {
  // [rc4l] What is left of an animation lengthMs long, now ms into the page's life; none for a player who asked for reduced motion, who sees it standing still.
  function holdMs(now, lengthMs, reduced) {
    return reduced ? 0 : Math.max(0, lengthMs - now);
  }

  // [rc4l] Settles once the animation has played out, for the engine's start to wait on.
  function played(hold, wait = setTimeout) {
    return new Promise((resolve) => { wait(resolve, hold); });
  }

  // [rc4l] Settles once the page has painted what it just changed, two frames on, or after limitMs for a tab that gets no frames, since the engine's start blocks painting.
  function painted(frame = root.requestAnimationFrame, wait = setTimeout, limitMs = 200) {
    return new Promise((resolve) => {
      frame(() => frame(resolve));
      wait(resolve, limitMs);
    });
  }

  // [rc4l] Whether the last `need` gaps between frames were all under quickMs, so the engine's start and its first level have stopped blocking the page.
  function settled(gaps, need, quickMs) {
    return gaps.length >= need && gaps.slice(-need).every((gap) => gap < quickMs);
  }

  // [rc4l] Lets clicks through the screen at once and hides it once frames settle or limitMs passes, for a tab that gets no frames or a machine that never draws quickly; returns what cancels that for a page that shows it again.
  function reveal(el, { frame = root.requestAnimationFrame, stopFrame = root.cancelAnimationFrame, now = () => root.performance.now(), wait = setTimeout, cancel = clearTimeout, need = 5, quickMs = 50, limitMs = 8000 } = {}) {
    el.classList.add('started');
    const gaps = [];
    let last = now(), id = 0, timer = 0;
    const hide = () => { stopFrame(id); cancel(timer); el.hidden = true; };
    const tick = () => {
      const t = now();
      gaps.push(t - last);
      last = t;
      if (settled(gaps, need, quickMs)) hide();
      else id = frame(tick);
    };
    id = frame(tick);
    timer = wait(hide, limitMs);
    return () => { stopFrame(id); cancel(timer); el.classList.remove('started', 'launching'); };
  }

  // [rc4l] Whether the page shows its tools, the console and the buttons a player does not see, which ?dev=1 asks for.
  function toolsShown(search) {
    return /[?&]dev=1(?:&|$)/.test(search || '');
  }

  const api = { holdMs, played, painted, settled, reveal, toolsShown };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaSplash = api;
})(typeof window !== 'undefined' ? window : globalThis);
