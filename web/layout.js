// [rc4l] How the page lays the game out on a phone, engine-free and DOM-free: the canvas fills the view, or keeps a landscape strip in portrait when asked (features/touch-input).
'use strict';

(function (root) {
  // [rc4l] The strip a query asks for in portrait, as width over height: ?strip=4:3 or ?strip=16:9; none for fill.
  function stripAspect(query) {
    const m = /[?&]strip=(\d+):(\d+)(?:&|$)/.exec(query || '');
    if (!m || Number(m[2]) === 0) return null;
    return Number(m[1]) / Number(m[2]);
  }

  // [rc4l] The main area's height for a view: the whole of it, or in portrait with a strip asked for, the width over the strip's aspect so the game keeps a landscape shape with the controls' band below.
  function computeLayout(width, height, { aspect = null, footer = 0 } = {}) {
    const portrait = height > width;
    const available = Math.max(0, height - footer);
    if (portrait && aspect) {
      const strip = Math.min(available, Math.round(width / aspect));
      return { portrait, mainHeight: strip, band: available - strip };
    }
    return { portrait, mainHeight: null, band: 0 };
  }

  const api = { stripAspect, computeLayout };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaLayout = api;
})(typeof window !== 'undefined' ? window : globalThis);
