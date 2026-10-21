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

  // [rc4l] Whether this browser puts its pointer-lock notice in the window's chrome, which takes 47 rows from the page and re-lays the game out (Safari); Chrome and Firefox float theirs over the page.
  function noticeShiftsLayout(navigator) {
    const ua = (navigator && navigator.userAgent) || '';
    return /Safari\//.test(ua) && !/Chrom|Edg|OPR|Firefox/.test(ua);
  }

  // [rc4l] A lock asked for outside fullscreen takes the canvas fullscreen in the same gesture, where the notice floats over the page instead of shrinking it; Safari refuses a lock asked for after the fullscreen promise, the gesture being spent by then.
  function fullscreenFirst(element, doc, wanted) {
    const request = element.requestPointerLock;
    if (!wanted || typeof request !== 'function' || typeof element.requestFullscreen !== 'function') return false;
    element.requestPointerLock = function (...args) {
      if (!doc.fullscreenElement && doc.fullscreenEnabled !== false) {
        const entering = element.requestFullscreen();
        if (entering && typeof entering.catch === 'function') entering.catch(() => {});
      }
      return request.apply(this, args);
    };
    return true;
  }

  const api = { guard, followSize, noticeShiftsLayout, fullscreenFirst };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaPointer = api;
})(typeof window !== 'undefined' ? window : globalThis);
