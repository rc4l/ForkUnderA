import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const { holdMs, played, painted, settled, reveal, toolsShown } = createRequire(import.meta.url)('../../../web/splash.js');

// [rc4l] A screen and a page whose frames, clock and timers a test drives by hand.
function page() {
  const classes = new Set();
  const p = {
    classes, time: 0, frameDue: null, timerDue: null, stopped: 0, cancelled: 0,
    el: { hidden: false, classList: { add: (c) => classes.add(c), remove: (...c) => c.forEach((x) => classes.delete(x)) } },
    frame: (fn) => { p.frameDue = fn; return 3; },
    stopFrame: () => { p.frameDue = null; p.stopped++; },
    now: () => p.time,
    wait: (fn) => { p.timerDue = fn; return 7; },
    cancel: () => { p.cancelled++; },
    step: (ms) => { p.time += ms; p.frameDue(); },
  };
  return p;
}

test('the screen holds for what is left of its animation', () => {
  assert.equal(holdMs(1000, 4000, false), 3000);
  assert.equal(holdMs(4000, 4000, false), 0);
  assert.equal(holdMs(9000, 4000, false), 0);
});

test('reduced motion has nothing to wait for', () => {
  assert.equal(holdMs(100, 4000, true), 0);
});

test('the engine waits for the animation to play out', async () => {
  let asked = 0;
  await played(2500, (fn, ms) => { asked = ms; fn(); });
  assert.equal(asked, 2500);
});

test('launching waits for two frames so its words reach the screen before the engine blocks it', async () => {
  const due = [];
  let done = false;
  const p = painted((fn) => due.push(fn), () => {}).then(() => { done = true; });
  due.shift()();
  await Promise.resolve();
  assert.equal(done, false);
  due.shift()();
  await p;
  assert.equal(done, true);
});

test('a tab with no frames still launches after the limit', async () => {
  let asked = 0;
  await painted(() => {}, (fn, ms) => { asked = ms; fn(); }, 150);
  assert.equal(asked, 150);
});

test('frames have settled once the last few were all quick', () => {
  assert.equal(settled([16, 16], 3, 50), false);
  assert.equal(settled([1600, 16, 16, 16], 3, 50), true);
  assert.equal(settled([16, 16, 1600], 3, 50), false);
  assert.equal(settled([16, 50, 16], 3, 50), false);
});

test('the screen lets clicks through at once and stays through the stalls until frames settle', () => {
  const p = page();
  reveal(p.el, { ...p, need: 2, quickMs: 50 });
  assert.ok(p.classes.has('started'));
  p.step(1600);
  p.step(16);
  assert.equal(p.el.hidden, false);
  p.step(16);
  assert.equal(p.el.hidden, true);
  assert.equal(p.cancelled, 1);
});

test('the screen goes at the limit when frames never settle', () => {
  const p = page();
  reveal(p.el, { ...p, need: 2, quickMs: 50 });
  p.timerDue();
  assert.equal(p.el.hidden, true);
  assert.equal(p.stopped, 1);
});

test('a page that shows the screen again cancels the hide', () => {
  const p = page();
  const undo = reveal(p.el, { ...p, need: 2, quickMs: 50 });
  p.classes.add('launching');
  undo();
  assert.equal(p.classes.has('started'), false);
  assert.equal(p.classes.has('launching'), false);
  assert.equal(p.cancelled, 1);
  assert.equal(p.frameDue, null);
});

test('the page tools show only for dev=1', () => {
  assert.equal(toolsShown('?map=MAP01&dev=1'), true);
  assert.equal(toolsShown('?dev=1&map=MAP01'), true);
  assert.equal(toolsShown('?map=MAP01'), false);
  assert.equal(toolsShown('?dev=10'), false);
  assert.equal(toolsShown(undefined), false);
});
