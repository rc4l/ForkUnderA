// [rc4l] The page's half of the browser build, wired together: the WebGPU device (gpu.js), the data (data.js), the player's home (home.js) and WADs (wads.js), crashes (crashreport.js), and main run once the device and the data are ready.
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

  function log(text) {
    console.log(text);
    consoleEl.textContent += text + '\n';
    if (consoleEl.textContent.length > 200000) consoleEl.textContent = consoleEl.textContent.slice(-100000);
  }
  // [rc4l] The engine's pointer lock requests, refusals logged instead of thrown into its frame (pointer.js).
  FuaPointer.guard(canvas, log);
  // [rc4l] SDL told of every change of the canvas's size, so the mouse is scaled alike on both axes (pointer.js).
  FuaPointer.followSize(canvas, window, window.ResizeObserver);

  // [rc4l] A file the player saves from the page: a crash report, a screenshot or a telemetry file.
  function download(blob, name) {
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = name;
    link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 10000);
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
    if (last) download(new Blob([last.text], { type: 'text/plain' }), 'forkundera-crash-' + last.event.event_id.slice(0, 8) + '.txt');
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

  // [rc4l] The device's errors: each logged, the first reported, and one that leaves the game unable to draw a crash.
  const gpuError = FuaCrash.gpuErrorHandler({
    log,
    report: (message) => crashes.report('webgpu', message, '').then((r) => { if (r.delivered) log('A report of it was sent to the developers.'); }),
    stop: (why) => crash('drawing stopped', why, ''),
    now: () => performance.now(),
    isOutOfMemory: (error) => typeof GPUOutOfMemoryError !== 'undefined' && error instanceof GPUOutOfMemoryError,
  });

  // [rc4l] Whether the engine is still there to write its config, from main until it quits or fails.
  let running = false;
  let saveHome = () => Promise.resolve(true);

  // [rc4l] A restart is a page reload, once the config and saves are stored so it keeps them, and the engine's arguments when it gives them start the next load.
  function restart(args) {
    if (args) FuaWads.keepRestartArgs(args);
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
      download(blob, name);
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
    // [rc4l] A file the engine wrote for the player (features/telemetry), downloaded as it named it.
    fuaOfferFile: (path) => {
      const name = path.split('/').pop();
      log('Downloading ' + name);
      download(new Blob([Module.FS.readFile(path)], { type: 'text/plain' }), name);
    },
    postMainLoop: afterFrame,
    preRun: [() => {
      const FS = Module.FS;
      Module.addRunDependency('fua-data');
      const loading = FuaData.fetchAll({ FS, onProgress: (name, fraction) => setStatus('Loading ' + name, fraction) });
      Promise.all([loading, FuaHome.mount(FS, Module.IDBFS || FS.filesystems.IDBFS, log), FuaWads.load(FS).then((a) => { wadArgs = a; })])
        .then(() => Module.removeRunDependency('fua-data'))
        .catch((e) => fail(String(e.message || e)));
    }],
  };
  // [rc4l] -iwad and -file for the player's own WADs, which the panel chose, unless a restart handed this load the engine's own arguments.
  let wadArgs = [];
  Module.fuaRestartArgs = FuaWads.takeRestartArgs();
  FuaWads.panel(restart);

  FuaGpu.makeDevice(navigator.gpu).then(({ device: d, adapter }) => {
    crashes.setAdapter(adapter);
    d.lost.then((lost) => crash('device lost', 'The graphics device was lost: ' + lost.message, ''));
    d.addEventListener('uncapturederror', (e) => gpuError(e.error));
    Module.fuaGpuLedger = FuaGpu.track(d);
    Module.preinitializedWebGPUDevice = d;
    deviceReady();
  }).catch((e) => fail(String(e.message || e)));

  Promise.all([device, runtime]).then(() => {
    // [rc4l] A console command, for the page's controls and for tests driving the engine; one that quits ends the engine rather than failing.
    Module.fuaCommand = (text) => {
      try { Module.ccall('fua_command', null, ['string'], [text]); }
      catch (e) { if (!e || e.name !== 'ExitStatus') throw e; }
    };
    statusEl.hidden = true;
    // [rc4l] Module.fuaSaving is the store in progress, or the last one, for tests to wait on.
    saveHome = FuaHome.keeper({ FS: Module.FS, log, writeConfig: () => { if (running) Module.fuaCommand('writeini'); }, onSaving: (p) => { Module.fuaSaving = p; } });
    canvas.focus();
    running = true;
    Module.callMain(Module.fuaRestartArgs || wadArgs);
  });

  document.getElementById('fullscreen').addEventListener('click', () => canvas.requestFullscreen());
  again.addEventListener('click', () => location.reload());
})();
