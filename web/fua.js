// [rc4l] The page's half of the browser build: the WebGPU device the engine attaches to, its data in the file system, its config kept between visits, and main run once all three are ready.
'use strict';

(function () {
  const statusEl = document.getElementById('status');
  const statusText = document.getElementById('status-text');
  const progress = document.getElementById('progress');
  const consoleEl = document.getElementById('console');
  const errorEl = document.getElementById('error');
  const canvas = document.getElementById('canvas');

  // [rc4l] Where the engine keeps its config and saves (posix's $HOME), stored in IndexedDB.
  const HOME = '/home/web_user';

  function log(text) {
    console.log(text);
    consoleEl.textContent += text + '\n';
    if (consoleEl.textContent.length > 200000) consoleEl.textContent = consoleEl.textContent.slice(-100000);
  }

  function fail(text) {
    statusEl.hidden = true;
    errorEl.hidden = false;
    errorEl.textContent = text;
  }

  function setStatus(text, fraction) {
    statusText.textContent = text;
    if (fraction !== undefined) progress.value = fraction;
  }

  // [rc4l] The engine's shaders use more than WebGPU's default limits, so the device asks for everything the adapter has.
  async function makeDevice() {
    if (!navigator.gpu) throw new Error('This browser has no WebGPU. Chrome or Edge 113 and later, Safari 26, and Firefox on Windows have it.');
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('WebGPU is present but found no usable graphics adapter.');
    const want = ['depth-clip-control', 'depth32float-stencil8', 'texture-compression-bc', 'indirect-first-instance', 'float32-filterable'];
    const requiredLimits = {};
    for (const k in adapter.limits) requiredLimits[k] = adapter.limits[k];
    const device = await adapter.requestDevice({ requiredFeatures: want.filter((f) => adapter.features.has(f)), requiredLimits });
    device.lost.then((info) => fail('The graphics device was lost: ' + info.message));
    device.addEventListener('uncapturederror', (e) => log('WebGPU error: ' + e.error.message));
    return device;
  }

  // [rc4l] Every file the engine starts with (its pk3s and Freedoom), fetched with progress and written at the file system's root, where it looks first.
  async function fetchData(FS) {
    const list = await (await fetch('data/files.json')).json();
    const total = list.reduce((n, f) => n + f.size, 0) || 1;
    let done = 0;
    for (const f of list) {
      const res = await fetch('data/' + f.name);
      if (!res.ok) throw new Error('Could not fetch ' + f.name + ' (' + res.status + ')');
      const reader = res.body.getReader();
      const bytes = new Uint8Array(f.size);
      let at = 0;
      for (;;) {
        const { done: end, value } = await reader.read();
        if (end) break;
        bytes.set(value, at);
        at += value.length;
        done += value.length;
        setStatus('Loading ' + f.name, done / total);
      }
      FS.writeFile('/' + f.name, bytes.subarray(0, at));
    }
  }

  function mountHome(FS, IDBFS) {
    FS.mkdirTree(HOME);
    FS.mount(IDBFS, {}, HOME);
    return new Promise((resolve) => FS.syncfs(true, (err) => { if (err) log('Saved data could not be read: ' + err); resolve(); }));
  }

  // [rc4l] Config and saves go back to IndexedDB every few seconds and when the page is hidden or left.
  function keepHome(FS) {
    let busy = false;
    const flush = () => {
      if (busy) return;
      busy = true;
      FS.syncfs(false, (err) => { busy = false; if (err) log('Saving failed: ' + err); });
    };
    setInterval(flush, 5000);
    document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') flush(); });
    window.addEventListener('pagehide', flush);
  }

  let deviceReady, runtimeReady;
  const device = new Promise((r) => (deviceReady = r));
  const runtime = new Promise((r) => (runtimeReady = r));

  window.Module = {
    canvas,
    print: log,
    printErr: log,
    setStatus: () => {},
    onRuntimeInitialized: () => runtimeReady(),
    fuaFatal: (text) => fail(text),
    fuaRestart: () => location.reload(),
    preRun: [() => {
      const FS = Module.FS;
      Module.addRunDependency('fua-data');
      Promise.all([fetchData(FS), mountHome(FS, Module.IDBFS || FS.filesystems.IDBFS)])
        .then(() => Module.removeRunDependency('fua-data'))
        .catch((e) => fail(String(e.message || e)));
    }],
  };

  makeDevice().then((d) => { Module.preinitializedWebGPUDevice = d; deviceReady(); }).catch((e) => fail(String(e.message || e)));

  Promise.all([device, runtime]).then(() => {
    // [rc4l] A console command, for the page's controls and for tests driving the engine.
    Module.fuaCommand = (text) => Module.ccall('fua_command', null, ['string'], [text]);
    statusEl.hidden = true;
    keepHome(Module.FS);
    canvas.focus();
    Module.callMain([]);
  });

  document.getElementById('fullscreen').addEventListener('click', () => canvas.requestFullscreen());
})();
