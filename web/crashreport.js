// [rc4l] The browser build's crash reports: what went wrong, where in the code and in which game, offered on the page to copy or save, and sent to the developers' tracker (a Sentry envelope, to GlitchTip) when the build may send and cl_crashreports is on.
'use strict';

(function (root) {
  // [rc4l] At most this many reports leave one page load, so a loop of errors cannot flood the tracker.
  const MAX_SENT = 3;
  // [rc4l] The console lines a report keeps, the last of them.
  const LOG_LINES = 100;
  // [rc4l] How long WebGPU errors may keep coming before the game counts as stopped, and the quiet between two that ends a run of them.
  const STUCK_MS = 2000;
  const RUN_GAP_MS = 500;

  // [rc4l] A DSN (https://KEY@HOST/PROJECT) as the envelope endpoint it names, or null when it is not one.
  function envelopeUrl(dsn) {
    let u;
    try { u = new URL(dsn); } catch (e) { return null; }
    const project = u.pathname.replace(/^\/+|\/+$/g, '');
    if (!u.username || !/^\d+$/.test(project)) return null;
    return `${u.protocol}//${u.host}/api/${project}/envelope/?sentry_key=${u.username}&sentry_version=7`;
  }

  // [rc4l] A frame's place, url:line:col or url:wasm-function[N]:0xOFFSET, its file kept as a path since the origin is only where the page was served.
  function place(where) {
    const wasm = /^(.*):(wasm-function\[\d+\]):(0x[0-9a-f]+)$/i.exec(where);
    const js = wasm ? null : /^(.*):(\d+):(\d+)$/.exec(where);
    const url = wasm ? wasm[1] : js ? js[1] : where;
    let filename = url;
    try { filename = new URL(url).pathname; } catch (e) { /* [rc4l] Not a URL, such as <anonymous>: kept as it was. */ }
    const frame = { filename, in_app: /\/forkundera\.(js|wasm)$/.test(filename) };
    if (wasm) { frame.instruction_addr = wasm[3]; frame.wasm_function = wasm[2]; }
    if (js) { frame.lineno = Number(js[2]); frame.colno = Number(js[3]); }
    return frame;
  }

  // [rc4l] A stack's frames, oldest call first as Sentry wants them, from Chrome's "at fn (place)" and Firefox's or Safari's "fn@place" lines.
  function parseStack(stack) {
    const frames = [];
    for (const raw of String(stack || '').split('\n')) {
      const line = raw.trim();
      let fn = '', where = null, m;
      if ((m = /^at (.*) \((.*)\)$/.exec(line))) { fn = m[1]; where = m[2]; }
      else if ((m = /^at (.*)$/.exec(line))) { where = m[1]; }
      else if ((m = /^(.*?)@(.+)$/.exec(line))) { fn = m[1]; where = m[2]; }
      if (where === null) continue;
      const frame = place(where);
      frame.function = fn || frame.wasm_function || '?';
      delete frame.wasm_function;
      frames.push(frame);
    }
    return frames.reverse();
  }

  // [rc4l] 32 hex digits, as an event id is.
  function eventId(random = Math.random) {
    let id = '';
    for (let i = 0; i < 32; i++) id += Math.floor(random() * 16).toString(16);
    return id;
  }

  // [rc4l] A WebGPU error in its run of them (state, null before the first): the game has stopped, with why in `stopped`, once the device is out of memory, which leaves what it was making unusable for good, or once errors have kept coming for STUCK_MS, every frame refused.
  function gpuErrorWatch(state, { message, outOfMemory = false, now }) {
    const run = state && now - state.last <= RUN_GAP_MS ? { first: state.first, cause: state.cause, last: now } : { first: now, cause: message, last: now };
    let stopped = '';
    if (outOfMemory) stopped = `out of graphics memory (${message})`;
    else if (now - run.first >= STUCK_MS) stopped = `nothing it draws reaches the screen (${run.cause})`;
    return { state: run, stopped };
  }

  // [rc4l] What the page does with each of a device's uncaptured errors: logs it, reports the first, since a refused binding can stop the drawing for good while the game goes on, and stops the game (stop(why)) once gpuErrorWatch says it can no longer draw.
  function gpuErrorHandler({ log, report, stop, now, isOutOfMemory }) {
    let reported = false, run = null;
    return (error) => {
      log('WebGPU error: ' + error.message);
      const watched = gpuErrorWatch(run, { message: error.message, outOfMemory: isOutOfMemory(error), now: now() });
      run = watched.state;
      if (watched.stopped) { stop(watched.stopped); return; }
      if (reported) return;
      reported = true;
      report(error.message);
    };
  }

  // [rc4l] One report as a Sentry event: kind is 'exception', 'abort', 'fatal', 'device lost', 'drawing stopped' or 'webgpu' (the only one the game may go on after), context the engine's last (crashcontext_compute.h).
  function buildEvent({ kind, message, stack = '', context = {}, browser = '', adapter = '', log = [], telemetry = '', now = new Date(), id = eventId() }) {
    const fatal = kind !== 'webgpu';
    return {
      event_id: id,
      timestamp: now.toISOString(),
      platform: 'javascript',
      level: fatal ? 'fatal' : 'error',
      logger: kind,
      release: context.release || undefined,
      dist: context.dist || undefined,
      environment: 'release',
      message: { formatted: message },
      exception: { values: [{ type: kind, value: message, stacktrace: { frames: parseStack(stack) } }] },
      tags: Object.assign({ map: context.map || undefined, iwad: context.iwad || undefined }, context.tags || {}, { build: 'web', kind }),
      extra: { load_order: (context.files || []).join(', '), log: log.slice(-LOG_LINES).join('\n'), telemetry: telemetry || undefined },
      contexts: { browser: { name: browser }, gpu: { name: adapter } },
      // [rc4l] A fatal or a device's error groups on its message, as the desktop's fatals do; an exception groups on its stack.
      fingerprint: kind === 'exception' || kind === 'abort' ? undefined : [kind, message],
    };
  }

  // [rc4l] The event as an envelope, the body Sentry's and GlitchTip's envelope endpoint takes.
  function envelope(event, dsn, now = new Date()) {
    return [JSON.stringify({ event_id: event.event_id, sent_at: now.toISOString(), dsn }), JSON.stringify({ type: 'event' }), JSON.stringify(event)].join('\n');
  }

  // [rc4l] Whether a report goes to the tracker: 'send', or why not ('unofficial' builds have no DSN, 'off' is cl_crashreports 0, 'limit' is MAX_SENT already gone).
  function decision(context, sent) {
    if (!context || !envelopeUrl(context.dsn || '')) return 'unofficial';
    if (!context.reporting) return 'off';
    return sent >= MAX_SENT ? 'limit' : 'send';
  }

  // [rc4l] What the page says about a report, by its decision and whether the send went through.
  function statusText(why, delivered) {
    if (why === 'send') return delivered ? 'The report was sent to the developers.' : 'The report could not be sent; copy or save it to share it.';
    if (why === 'off') return 'Crash reports are off (cl_crashreports 0); copy or save the report to share it.';
    if (why === 'limit') return 'Enough reports were sent from this visit; copy or save this one to share it.';
    return 'This build does not send crash reports; copy or save the report to share it.';
  }

  // [rc4l] The report as text for a person: the event's summary, its stack newest call first, the engine's last minute, then the console's last lines.
  function reportText(event) {
    const frames = event.exception.values[0].stacktrace.frames.slice().reverse();
    const at = (f) => `  ${f.function} (${f.filename}${f.lineno ? `:${f.lineno}:${f.colno}` : f.instruction_addr ? `:${f.instruction_addr}` : ''})`;
    return [
      `ForkUnderA crash report ${event.event_id}`,
      `What: ${event.logger}: ${event.message.formatted}`,
      `When: ${event.timestamp}`,
      `Release: ${event.release || 'unknown'}${event.dist ? ` (${event.dist})` : ''}`,
      `Map: ${event.tags.map || 'none'}`,
      `Files: ${event.extra.load_order || 'none'}`,
      `Browser: ${event.contexts.browser.name}`,
      `Graphics: ${event.contexts.gpu.name}`,
      'Stack:',
      ...(frames.length ? frames.map(at) : ['  (none)']),
      ...(event.extra.telemetry ? ['Last minute:', event.extra.telemetry.trimEnd()] : []),
      'Console:',
      event.extra.log,
    ].join('\n');
  }

  // [rc4l] The page's reporter: the engine's context as it last told it, the adapter it drew with, the reports it has sent and the last one; report() builds one, sends it when it may, and says what became of it.
  function reporter({ send, browser = '', log = () => [] }) {
    let context = {};
    let adapter = '';
    let telemetry = '';
    let sent = 0;
    let last = null;
    return {
      last() { return last; },
      setContext(json) { context = typeof json === 'string' ? JSON.parse(json) : json; },
      context() { return context; },
      setAdapter(text) { adapter = text; },
      // [rc4l] The engine's summary of its last minute (features/telemetry), which it hands over each second.
      setTelemetry(text) { telemetry = text; },
      async report(kind, message, stack) {
        const event = buildEvent({ kind, message, stack, context, browser, adapter, log: log(), telemetry });
        const why = decision(context, sent);
        let delivered = false;
        if (why === 'send') {
          sent++;
          try { delivered = await send(envelopeUrl(context.dsn), envelope(event, context.dsn)); } catch (e) { delivered = false; }
        }
        last = { event, text: reportText(event), status: statusText(why, delivered), why, delivered };
        return last;
      },
    };
  }

  // [rc4l] Sends an envelope as plain text, which a browser sends without asking the tracker first, and keeps it going if the page is closed.
  function fetchSend(url, body) {
    return fetch(url, { method: 'POST', body, keepalive: true, headers: { 'Content-Type': 'text/plain;charset=UTF-8' } }).then((r) => r.ok);
  }

  const api = { MAX_SENT, STUCK_MS, RUN_GAP_MS, gpuErrorWatch, gpuErrorHandler, envelopeUrl, parseStack, eventId, buildEvent, envelope, decision, statusText, reportText, reporter, fetchSend };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaCrash = api;
})(typeof window !== 'undefined' ? window : globalThis);
