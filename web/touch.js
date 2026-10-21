// [rc4l] The page as a touch screen's host (features/touch-input): the canvas's pointers, the view's insets and orientation forwarded into Module.fuaTouch for the engine to read each tic; the controls themselves are the engine's.
'use strict';

(function (root) {
  // [rc4l] Installs the forwarding on a canvas: Module.fuaTouch holds active (the controls wanted), pointers (id, x, y, ended), width, height, insets and portrait; the engine's read clears the ended pointers.
  function install(canvas, Module, { logic, win = root, doc = root.document, query = win.location.search } = {}) {
    const state = { active: false, pointers: [], width: 0, height: 0, insets: [0, 0, 0, 0], portrait: false };
    Module.fuaTouch = state;
    let sawKeyboard = false;

    const coarse = () => !!(win.matchMedia && win.matchMedia('(pointer: coarse)').matches);
    const touchPoints = () => win.navigator.maxTouchPoints || 0;
    const wanted = () => logic.touchWanted({ coarse: coarse(), touchPoints: touchPoints(), query, sawKeyboard });

    // [rc4l] The safe-area insets, read back from the custom properties fua.css sets from env().
    const insets = () => {
      const style = win.getComputedStyle(doc.documentElement);
      return ['--fua-inset-left', '--fua-inset-top', '--fua-inset-right', '--fua-inset-bottom'].map((name) => logic.insetPx(style.getPropertyValue(name)));
    };
    const measure = () => {
      const view = logic.viewOf(canvas.clientWidth, canvas.clientHeight, insets());
      state.width = view.width; state.height = view.height; state.insets = view.insets; state.portrait = view.portrait;
      state.active = wanted();
    };

    // [rc4l] A pointer's place in canvas pixels, which equal CSS pixels here (the canvas is sized by its layout box).
    const place = (ev) => { const r = canvas.getBoundingClientRect(); return { x: ev.clientX - r.left, y: ev.clientY - r.top }; };
    const forward = (type) => (ev) => {
      // [rc4l] Fingers and pens; a mouse only when ?touch=1 forces the controls, for the checks.
      if (ev.pointerType === 'mouse' && !/[?&]touch=1(?:&|$)/.test(query)) return;
      if (type === 'down') { sawKeyboard = false; if (canvas.setPointerCapture) { try { canvas.setPointerCapture(ev.pointerId); } catch (e) { /* [rc4l] a synthetic pointer has no capture */ } } }
      measure();
      if (!state.active) return;
      const { x, y } = place(ev);
      state.pointers = logic.applyPointer(state.pointers, { type, id: ev.pointerId, x, y });
      if (type === 'down') ev.preventDefault();
    };
    canvas.addEventListener('pointerdown', forward('down'));
    canvas.addEventListener('pointermove', forward('move'));
    canvas.addEventListener('pointerup', forward('up'));
    canvas.addEventListener('pointercancel', forward('cancel'));
    // [rc4l] A real key hides the controls until the next touch; a key the page itself sends (its checks) does not count.
    win.addEventListener('keydown', (ev) => { if (ev.isTrusted) { sawKeyboard = true; state.active = wanted(); } });
    win.addEventListener('resize', measure);
    measure();
    return { measure, state };
  }

  const api = { install };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaTouch = api;
})(typeof window !== 'undefined' ? window : globalThis);
