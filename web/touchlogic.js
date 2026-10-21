// [rc4l] The page's decisions about a touch screen, engine-free and DOM-free: whether the on-screen controls are wanted, and the pointer list the engine reads (features/touch-input).
'use strict';

(function (root) {
  // [rc4l] Wanted on a coarse pointer with touch points, unless a real key was pressed since the last touch; ?touch=1 or ?touch=0 decides outright.
  function touchWanted({ coarse, touchPoints, query, sawKeyboard }) {
    const forced = /[?&]touch=([01])(?:&|$)/.exec(query || '');
    if (forced) return forced[1] === '1';
    return !!coarse && (touchPoints || 0) > 0 && !sawKeyboard;
  }

  // [rc4l] One pointer event folded into the list: down adds (or revives) its id where it landed (ox, oy, which a flick before the engine's next read still keeps), move places it, up or cancel marks it ended, which the engine's read then clears.
  function applyPointer(pointers, { type, id, x, y }) {
    const rest = pointers.filter((p) => p.id !== id);
    const was = pointers.find((p) => p.id === id);
    switch (type) {
      case 'down': return rest.concat([{ id, x, y, ox: x, oy: y, ended: false }]);
      case 'move': return was && !was.ended ? rest.concat([{ id, x, y, ox: was.ox, oy: was.oy, ended: false }]) : pointers;
      case 'up':
      case 'cancel': return was ? rest.concat([{ id, x: was.x, y: was.y, ox: was.ox, oy: was.oy, ended: true }]) : pointers;
      default: return pointers;
    }
  }

  // [rc4l] The view the engine lays the controls on: the canvas's size in its own pixels, the safe-area insets, and portrait when taller than wide.
  function viewOf(width, height, insets) {
    const i = (insets || []).map((v) => (Number.isFinite(v) ? v : 0));
    while (i.length < 4) i.push(0);
    return { width: Math.max(0, Math.round(width || 0)), height: Math.max(0, Math.round(height || 0)), insets: i.slice(0, 4), portrait: (height || 0) > (width || 0) };
  }

  // [rc4l] A safe-area inset read back from a computed style ("12px", "" or undefined) as pixels.
  function insetPx(text) {
    const n = parseFloat(text);
    return Number.isFinite(n) ? n : 0;
  }

  const api = { touchWanted, applyPointer, viewOf, insetPx };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaTouchLogic = api;
})(typeof window !== 'undefined' ? window : globalThis);
