// [rc4l] The page's half of the browser build: the WebGPU device the engine attaches to, its data in the file system, its config kept between visits, and main run once all three are ready.
'use strict';

(function () {
  const statusEl = document.getElementById('status');
  const statusText = document.getElementById('status-text');
  const progress = document.getElementById('progress');
  const consoleEl = document.getElementById('console');
  const errorEl = document.getElementById('error');
  const errorText = document.getElementById('error-text');
  const reportEl = document.getElementById('report');
  const reportStatus = document.getElementById('report-status');
  const canvas = document.getElementById('canvas');
  const again = document.getElementById('again');

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
    errorText.textContent = text;
  }

  // [rc4l] Crash reports (crashreport.js): the engine tells the page about the game as it changes, and whatever stops the game is reported once, offered on the error panel to copy or save.
  const crashes = FuaCrash.reporter({ send: FuaCrash.fetchSend, browser: navigator.userAgent, log: () => consoleEl.textContent.split('\n') });
  let crashed = false;
  function crash(kind, message, stack) {
    if (crashed) return;
    crashed = true;
    running = false;
    // [rc4l] The game does not play on behind the panel, as it did when only its drawing had failed.
    if (Module.pauseMainLoop) Module.pauseMainLoop();
    if (document.pointerLockElement) document.exitPointerLock();
    fail(kind === 'fatal' ? message : 'The game crashed: ' + message);
    reportStatus.textContent = 'Preparing a report…';
    reportEl.hidden = false;
    crashes.report(kind, message, stack).then((r) => { reportStatus.textContent = r.status; });
  }
  document.getElementById('report-copy').addEventListener('click', (e) => {
    const last = crashes.last();
    if (!last) return;
    navigator.clipboard.writeText(last.text).then(() => { e.target.textContent = 'Copied'; }, () => { e.target.textContent = 'Copy failed'; });
  });
  document.getElementById('report-save').addEventListener('click', () => {
    const last = crashes.last();
    if (!last) return;
    const link = document.createElement('a');
    link.href = URL.createObjectURL(new Blob([last.text], { type: 'text/plain' }));
    link.download = 'forkundera-crash-' + last.event.event_id.slice(0, 8) + '.txt';
    link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 10000);
  });
  // [rc4l] An error that escapes the engine's frame stops its loop, so it is a crash once the engine is running; a quit's exit is not one.
  function escaped(error, message) {
    if (!running || (error && error.name === 'ExitStatus')) return;
    crash('exception', (error && error.message) || message || String(error), error && error.stack);
  }
  window.addEventListener('error', (e) => escaped(e.error, e.message));
  window.addEventListener('unhandledrejection', (e) => escaped(e.reason, ''));

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
    const info = adapter.info || {};
    crashes.setAdapter([info.vendor, info.architecture, info.description, info.isFallbackAdapter ? '(software)' : ''].filter(Boolean).join(' '));
    device.lost.then((lost) => crash('device lost', 'The graphics device was lost: ' + lost.message, ''));
    // [rc4l] The first error the device reports is reported too, since a refused binding can stop the drawing for good while the game goes on; one that leaves the game unable to draw (gpuErrorWatch) is a crash, which before left the last frame on the canvas with no panel.
    let gpuReported = false, gpuRun = null;
    device.addEventListener('uncapturederror', (e) => {
      log('WebGPU error: ' + e.error.message);
      const outOfMemory = typeof GPUOutOfMemoryError !== 'undefined' && e.error instanceof GPUOutOfMemoryError;
      const watched = FuaCrash.gpuErrorWatch(gpuRun, { message: e.error.message, outOfMemory, now: performance.now() });
      gpuRun = watched.state;
      if (watched.stopped) { crash('drawing stopped', watched.stopped, ''); return; }
      if (gpuReported) return;
      gpuReported = true;
      crashes.report('webgpu', e.error.message, '').then((r) => { if (r.delivered) log('A report of it was sent to the developers.'); });
    });
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

  // [rc4l] Whether the engine is still there to write its config, from main until it quits or fails.
  let running = false;
  let saveHome = () => Promise.resolve(true);

  // [rc4l] Saves go back to IndexedDB every few seconds, and the config with them when the page is hidden or left, one sync at a time.
  function keepHome(FS) {
    let last = Promise.resolve(true), queued = 0;
    const sync = () => new Promise((resolve) => FS.syncfs(false, (err) => { if (err) log('Saving failed: ' + err); resolve(!err); }));
    saveHome = (withConfig) => {
      if (withConfig && running) Module.fuaCommand('writeini');
      queued++;
      last = last.then(sync).finally(() => queued--);
      // [rc4l] The store in progress, or the last one, as a promise of whether it worked, for tests to wait on.
      Module.fuaSaving = last;
      return last;
    };
    setInterval(() => { if (!queued) saveHome(false); }, 5000);
    document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') saveHome(true); });
    window.addEventListener('pagehide', () => saveHome(true));
  }

  // [rc4l] A restart is a page reload, once the config and saves are stored so it keeps them.
  function restart() {
    saveHome(true).finally(() => location.reload());
  }

  // [rc4l] The engine quit, having written its config: everything is stored and the page offers to start again, unless an error is showing.
  function ended() {
    running = false;
    saveHome(false).finally(() => {
      if (!errorEl.hidden) return;
      setStatus('You quit the game.');
      progress.hidden = true;
      again.hidden = false;
      statusEl.hidden = false;
    });
  }

  // [rc4l] A screenshot: the canvas holds a frame only until the task that drew it ends, so the engine asks during the frame and the page saves the canvas just after it, as a download named as the engine named it.
  let shotName = null;
  function captureFrame(path) {
    shotName = path.split('/').pop();
  }
  function afterFrame() {
    if (shotName === null) return;
    const name = shotName;
    shotName = null;
    canvas.toBlob((blob) => {
      if (!blob) { log('The screenshot could not be made.'); return; }
      log('Downloading ' + name);
      const link = document.createElement('a');
      link.href = URL.createObjectURL(blob);
      link.download = name;
      link.click();
      setTimeout(() => URL.revokeObjectURL(link.href), 10000);
    }, 'image/png');
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
    onAbort: (what) => crash('abort', 'The engine stopped: ' + what, new Error().stack),
    fuaFatal: (text) => crash('fatal', text, new Error().stack),
    fuaCrashContext: (json) => crashes.setContext(json),
    // [rc4l] The reporter itself, for checks that send a report or read what the engine told it.
    fuaCrashReporter: crashes,
    fuaRestart: restart,
    fuaExit: ended,
    fuaCaptureFrame: captureFrame,
    postMainLoop: afterFrame,
    preRun: [() => {
      const FS = Module.FS;
      Module.addRunDependency('fua-data');
      Promise.all([fetchData(FS), mountHome(FS, Module.IDBFS || FS.filesystems.IDBFS), FuaWads.load(FS).then((a) => { wadArgs = a; })])
        .then(() => Module.removeRunDependency('fua-data'))
        .catch((e) => fail(String(e.message || e)));
    }],
  };
  // [rc4l] -iwad and -file for the player's own WADs, which the panel chose.
  let wadArgs = [];
  FuaWads.panel(restart);

  makeDevice().then((d) => { Module.preinitializedWebGPUDevice = d; deviceReady(); }).catch((e) => fail(String(e.message || e)));

  Promise.all([device, runtime]).then(() => {
    // [rc4l] A console command, for the page's controls and for tests driving the engine; one that quits ends the engine rather than failing.
    Module.fuaCommand = (text) => {
      try { Module.ccall('fua_command', null, ['string'], [text]); }
      catch (e) { if (!e || e.name !== 'ExitStatus') throw e; }
    };
    statusEl.hidden = true;
    keepHome(Module.FS);
    canvas.focus();
    running = true;
    Module.callMain(wadArgs);
  });

  document.getElementById('fullscreen').addEventListener('click', () => canvas.requestFullscreen());
  again.addEventListener('click', () => location.reload());
})();
