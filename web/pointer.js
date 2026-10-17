// [rc4l] The pointer lock the engine asks the browser for, made safe to refuse.
'use strict';

(function (root) {
  // [rc4l] A refusal (no focus, a document the browser will not lock, the player having just pressed Escape) thrown or rejected out of requestPointerLock reached the engine's frame or the page's crash handler and stopped the game; it is logged instead.
  function guard(element, log) {
    const request = element.requestPointerLock;
    if (typeof request !== 'function') return false;
    const refused = (e) => log('Pointer lock refused: ' + ((e && e.message) || e));
    element.requestPointerLock = function (...args) {
      try {
        const result = request.apply(this, args);
        if (result && typeof result.catch === 'function') return result.catch(refused);
        return result;
      } catch (e) {
        refused(e);
        return undefined;
      }
    };
    return true;
  }

  // [rc4l] SDL scales mouse motion by its window over the canvas, axis by axis, and learns the canvas's size only from the window's resize event, so a canvas resized without one (fullscreen in a maximized browser, the page's own layout) left x and y scaled apart; every change of the canvas's size is handed to it as that event.
  function followSize(element, win, Observer) {
    if (typeof Observer !== 'function') return null;
    const observer = new Observer(() => win.dispatchEvent(new win.Event('resize')));
    observer.observe(element);
    return observer;
  }

  const api = { guard, followSize };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaPointer = api;
})(typeof window !== 'undefined' ? window : globalThis);
