// xrick web player -- wasm.md phase W1 (RD1).
// Loaded by the page before xrick.js (built by build.sh with -sINVOKE_RUN=0 and
// -sEXPORTED_RUNTIME_METHODS=callMain,FS): the game starts on a click or tap, which
// also lets the browser play sound.
//
// Works with index.html as built, and with any other page (e.g. a CMS template):
// such a page sets window.xrickPlayer BEFORE loading this file to say where its
// elements are and where xrick.wasm is served from -- see `defaults` below. The
// generated xrick.js is never edited.
//
//   <script>window.xrickPlayer = { start: '#xrick', label: '#xrick span',
//     status: '#player_console', buttons: '#controls1 div[data-code]',
//     wasmUrl: '/media/1vqhdi1t/xrick.wasm' };</script>
//   <script src="player.js"></script>
//   <script src="xrick.js" async></script>

'use strict';

// ---- settings ---------------------------------------------------------------------------
var defaults = {
  // start the attract demo instead of a playable game. Off (user decision,
  // 2026-09-28); `?demo` in the URL turns it on for one visit
  startInDemo: false,
  // element clicked/tapped to start; hidden (display:none) once the game runs
  start: '#start',
  // element(s) that show the "click or touch to play" prompt; null = the start element
  label: null,
  // element for status messages (loading, why the game stopped); optional
  status: '#status',
  // on-page buttons: elements with data-code (KeyboardEvent.code) and data-key
  buttons: 'button[data-code]',
  // touch pad (holds the arrow keys); optional
  pad: '#pad',
  // "download trace" button shown with ?trace; optional
  saveTrace: '#savetrace',
  // fullscreen button; optional -- hidden if the browser has no Fullscreen API
  fullscreen: '#fullscreen',
  // where xrick.wasm is served from; null = next to xrick.js (emscripten's default)
  wasmUrl: null
};
// The canvas must be <canvas id="canvas">: SDL3 looks it up as "#canvas" by default.
var cfg = Object.assign({}, defaults, window.xrickPlayer || {});

// ---- arguments ----------------------------------------------------------------------------
// query string -> xrick command line: ?demo -> -demo, ?speed=3 -> -speed 3, ...
// (only options the game knows; -data is gone since assets are compiled in). With none of
// -game/-map/-submap/-demo given, the game starts with no -game at all: the in-game diskette
// carousel (gameselect.c) picks RD1 or RD2, and its own settings menu (SELECT MAP / WARP TO
// ROOM, RD1; SWITCH GAME, both) is how a world/room/game choice is made from there -- the
// page no longer has its own map/world drop-downs. Those menu actions reload the page with
// ?game=/&map=/&submap= set, which this still reads, same as a shared/bookmarked link would.
function buildArgs() {
  var q = new URLSearchParams(window.location.search);
  var args = [];
  var flags = ['demo', 'nosound' /*, 'fullscreen'*/];
  var values = ['speed', /*'zoom',*/ 'keys', 'vol', 'map', 'submap', 'game'];
  flags.forEach(function (k) { if (q.has(k)) args.push('-' + k); });
  values.forEach(function (k) { if (q.get(k)) args.push('-' + k, q.get(k)); });
  if (cfg.startInDemo && args.indexOf('-demo') < 0) args.push('-demo');
  if (q.has('trace')) args.push('-trace', '/trace.txt');   // wasm.md W1.7
  return args;
}

// ---- page helpers -------------------------------------------------------------------------

function find(selector) { return selector ? document.querySelector(selector) : null; }
function findAll(selector) { return selector ? Array.prototype.slice.call(document.querySelectorAll(selector)) : []; }

var canvas = document.getElementById('canvas');
var startBox = find(cfg.start);
var statusBox = find(cfg.status);
var lastLines = [];

function setStatus(text) {
  if (statusBox) statusBox.textContent = text || '';
}

function setLabel(text) {
  var els = cfg.label ? findAll(cfg.label) : (startBox ? [startBox] : []);
  els.forEach(function (el) { el.textContent = text; });
}

function keepLine(text) {
  lastLines.push(text);
  if (lastLines.length > 40) lastLines.shift();
}

// SDL3 maps web key events from KeyboardEvent.code (SDL_emscriptenevents.c,
// Emscripten_HandleKey), so synthetic events must carry `code` -- keyCode alone is
// an unknown key. They are dispatched on window, where SDL listens by default.
function sendKey(down, code, key) {
  window.dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup',
    { code: code, key: key, bubbles: true, cancelable: true }));
}

function bindButton(el) {
  var code = el.dataset.code, key = el.dataset.key;
  if (!code) return;
  var press = function (e) { e.preventDefault(); sendKey(true, code, key); };
  var release = function (e) { e.preventDefault(); sendKey(false, code, key); };
  el.addEventListener('mousedown', press);
  el.addEventListener('mouseup', release);
  el.addEventListener('mouseleave', function (e) { if (e.buttons) release(e); });
  el.addEventListener('touchstart', press, { passive: false });
  el.addEventListener('touchend', release, { passive: false });
  el.addEventListener('touchcancel', release, { passive: false });
}

// touch pad: the offset from the pad's centre holds the arrow keys
function bindPad(pad) {
  var held = {};
  var dirs = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'];
  function set(code, on) {
    if (!!held[code] === on) return;
    held[code] = on;
    sendKey(on, code, code);
  }
  function move(t) {
    var r = pad.getBoundingClientRect();
    var dx = t.clientX - (r.left + r.width / 2), dy = t.clientY - (r.top + r.height / 2);
    set('ArrowRight', dx > r.width / 6);
    set('ArrowLeft', dx < -r.width / 6);
    set('ArrowDown', dy > r.height / 4);
    set('ArrowUp', dy < -r.height / 4);
  }
  function stop(e) { e.preventDefault(); dirs.forEach(function (c) { set(c, false); }); }
  pad.addEventListener('touchstart', function (e) { e.preventDefault(); move(e.targetTouches[0]); }, { passive: false });
  pad.addEventListener('touchmove', function (e) { e.preventDefault(); move(e.targetTouches[0]); }, { passive: false });
  pad.addEventListener('touchend', stop, { passive: false });
  pad.addEventListener('touchcancel', stop, { passive: false });
}

// carousel touch gestures: tap near an edge (or swipe) to switch games, tap the middle
// to launch the active one. Only acts while window.xrickCarouselActive is true -- set/
// cleared from gameselect.c (EMSCRIPTEN-only), so the same gesture during real gameplay
// doesn't fight the #pad/#fire touch controls below the canvas (e.g. an accidental tap
// on the canvas moving Rick off a ledge). Dispatches ArrowLeft/ArrowRight/Enter, same as
// bindPad above and gameselect.c's own SDL_SCANCODE_RETURN confirm check -- none of
// those are remappable, so this can't be broken by a key remap elsewhere in the menu.
function bindCarouselTouch(canvas) {
  var SWIPE_PX = 40;    // minimum horizontal travel to count as a swipe, not a tap
  var TAP_PX = 15;      // maximum travel still considered a tap (not a drag that went nowhere)
  var EDGE_FRAC = 0.22; // left/right this fraction of the canvas width counts as "near an arrow"
  var touch = null;

  function tap(code) {
    sendKey(true, code, code);
    setTimeout(function () { sendKey(false, code, code); }, 80); // long enough to span a frame
  }

  canvas.addEventListener('touchstart', function (e) {
    if (!window.xrickCarouselActive) return;
    e.preventDefault();
    var t = e.changedTouches[0];
    touch = { x: t.clientX, y: t.clientY };
  }, { passive: false });

  canvas.addEventListener('touchmove', function (e) {
    if (window.xrickCarouselActive) e.preventDefault(); // don't let the swipe scroll the page
  }, { passive: false });

  canvas.addEventListener('touchend', function (e) {
    if (!window.xrickCarouselActive || !touch) return;
    e.preventDefault();
    var t = e.changedTouches[0];
    var dx = t.clientX - touch.x, dy = t.clientY - touch.y;
    touch = null;

    if (Math.abs(dx) >= SWIPE_PX && Math.abs(dx) > Math.abs(dy)) {
      // left-to-right swipe (dx > 0) = LEFT; right-to-left (dx < 0) = RIGHT
      tap(dx > 0 ? 'ArrowLeft' : 'ArrowRight');
      return;
    }
    if (Math.abs(dx) > TAP_PX || Math.abs(dy) > TAP_PX) return; // a drag, not a tap or a swipe

    var r = canvas.getBoundingClientRect();
    var frac = (t.clientX - r.left) / r.width;
    if (frac < EDGE_FRAC) tap('ArrowLeft');
    else if (frac > 1 - EDGE_FRAC) tap('ArrowRight');
    else tap('Enter');
  }, { passive: false });

  canvas.addEventListener('touchcancel', function () { touch = null; }, { passive: false });
}

var runtimeReady = false; // set once xrick.wasm is loaded: the game can start

findAll(cfg.buttons).forEach(bindButton);

var padEl = find(cfg.pad);
if (padEl) bindPad(padEl);
bindCarouselTouch(canvas);
window.addEventListener('touchstart', function once() {
  document.body.classList.add('touch');
  window.removeEventListener('touchstart', once);
});

// ---- sound on iPhone / iPad ----------------------------------------------------------------
// iOS plays Web Audio as "ambient" sound, which the Ring/Silent switch mutes (the page
// shows the speaker icon, yet nothing is heard). Two ways to make it "playback" sound,
// both inside the start tap:
// - the Audio Session API, where the browser has it (recent Safari/WebKit);
// - where it does not, a silent looping <audio> element: playing a media element
//   switches the page to playback audio, which un-mutes Web Audio too -- what the 2019
//   player's startAudio() did, lost in the W1.5 rewrite.
function isIOS() {
  return /iPad|iPhone|iPod/.test(navigator.userAgent) ||
         (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);  // iPadOS
}

function silentWavUrl() {
  // 0.25 s of 8-bit mono silence at 8000 Hz (0x80 = the zero level of 8-bit PCM)
  var n = 2000, buf = new ArrayBuffer(44 + n), v = new DataView(buf), i;
  function str(o, s) { for (var k = 0; k < s.length; k++) v.setUint8(o + k, s.charCodeAt(k)); }
  str(0, 'RIFF'); v.setUint32(4, 36 + n, true); str(8, 'WAVE');
  str(12, 'fmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, 8000, true); v.setUint32(28, 8000, true); v.setUint16(32, 1, true); v.setUint16(34, 8, true);
  str(36, 'data'); v.setUint32(40, n, true);
  for (i = 0; i < n; i++) v.setUint8(44 + i, 0x80);
  return URL.createObjectURL(new Blob([buf], { type: 'audio/wav' }));
}

function unlockAudio() {
  try {
    if (navigator.audioSession) navigator.audioSession.type = 'playback';
  } catch (e) { /* not supported: the <audio> element below does it */ }
  if (isIOS() && !unlockAudio.el) {
    var a = document.createElement('audio');
    a.setAttribute('playsinline', '');
    a.loop = true;
    a.src = silentWavUrl();
    var p = a.play();
    if (p && p.catch) p.catch(function () { /* no media playback allowed: nothing more to do */ });
    unlockAudio.el = a;   // keep it playing (and referenced) for the whole session
  }
}

// SDL creates its AudioContext during callMain; resume it while still in the tap
function resumeSdlAudio() {
  var ctx = Module.SDL3 && Module.SDL3.audioContext;
  if (ctx && ctx.state !== 'running' && ctx.resume) ctx.resume();
}

// ---- settings persistence (Phase 4) ---------------------------------------------------
// settings.c's SDL_GetPrefPath("xrick","xrick") resolves to /libsdl/xrick/xrick/ on the
// web build (SDL's Emscripten filesystem backend, SDL_sysfilesystem.c); mounting IDBFS at
// /libsdl -- its root, created here before any of SDL's own mkdir calls -- covers that
// whole path. autoPersist writes each change back to IndexedDB on its own, so nothing
// native-only (settings_save(), in the game's C code) needs to call FS.syncfs(false)
// itself. The one async step is the initial load: it must finish before Module.callMain()
// runs settings_load(), since that call is synchronous C code expecting the file (if any)
// to already be there.
var idbfsReady = false;
function setupIDBFS(cb) {
  if (idbfsReady) { cb(); return; }
  try {
    Module.FS.mkdir('/libsdl');
    Module.FS.mount(Module.FS.filesystems.IDBFS, { autoPersist: true }, '/libsdl');
    Module.FS.syncfs(true, function (err) {
      if (err) console.error('xrick: IDBFS load failed, starting without saved settings', err);
      idbfsReady = true;
      cb();
    });
  } catch (e) {
    console.error('xrick: IDBFS unavailable, settings will not persist', e);
    idbfsReady = true; // proceed without persistence rather than block the game from starting
    cb();
  }
}

// ---- trace (wasm.md W1.7) ------------------------------------------------------------------
// /trace.txt lives in MEMFS
function traceText() {
  try { Module._fflush(0); } catch (e) { /* runtime already exited: stdio was flushed */ }
  try { return Module.FS.readFile('/trace.txt', { encoding: 'utf8' }); } catch (e) { return null; }
}
window.xrickTrace = traceText;

// ?trace: a button saves the trace so far, to diff with a native `xrick -demo -trace`
var saveBtn = find(cfg.saveTrace);
if (saveBtn && new URLSearchParams(window.location.search).has('trace')) {
  saveBtn.hidden = false;
  saveBtn.style.display = '';
  saveBtn.addEventListener('click', function () {
    var t = traceText();
    if (!t) { setStatus('no trace yet'); return; }
    var a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([t], { type: 'text/plain' }));
    a.download = 'xrick-web.trace';
    a.click();
    URL.revokeObjectURL(a.href);
  });
}

// ---- fullscreen -----------------------------------------------------------------------------
// Module.requestFullscreen (set up by SDL3's Emscripten video backend once the window exists,
// i.e. after callMain -- see SDL_emscriptenvideo.c) goes through SDL_SetWindowFullscreen, so
// SDL's own canvas-resize tracking stays correct; screenDiv.requestFullscreen() is the fallback
// before the game has started (or if that hook is for some reason missing). Either way this
// only works called directly from a user gesture (a button's own click), which is exactly
// why this exists instead of relying on the in-game settings menu's FULLSCREEN toggle: that
// one goes through SDL's event queue, a frame after the real keypress, which browsers don't
// treat as "direct enough" to grant fullscreen from.
//
// The fullscreen TARGET is #screen (canvas's wrapping div), not the canvas itself: only the
// fullscreen element's own descendants render while fullscreen, and a <canvas> can't have
// real rendered children -- so an on-canvas exit button would just never be visible. #screen
// can have one, which is exactly what #fsExit below is: an overlay, hidden except while
// #screen is the fullscreen element (style.css), bottom-right over the game, with nothing
// else to tap to leave fullscreen otherwise (reported 2026-10-12: entering worked, leaving
// did not, since the original #fullscreen button lives outside the canvas in the normal
// button row and the same reasoning hid IT once fullscreen).
var screenDiv = find('#screen');

function toggleFullscreen() {
  function fail(label, e) {
    console.error('xrick: fullscreen (' + label + ') failed:', e);
    setStatus('fullscreen failed: ' + (e && e.message ? e.message : e));
  }
  if (document.fullscreenElement) {
    document.exitFullscreen().catch(function (e) { fail('exit', e); });
    return;
  }
  // prefer the plain browser API: it works whether or not the game has started yet, and
  // does not depend on SDL's own window/event-loop state at all. Module.requestFullscreen
  // (SDL3's own hook, once the game has created its window) is tried only as a fallback,
  // since going through SDL_SetWindowFullscreen's extra layers is more that can go wrong.
  try {
    var p = screenDiv.requestFullscreen();
    if (p && p.catch) p.catch(function (e) {
      fail('screen', e);
      if (Module.requestFullscreen) {
        try { Module.requestFullscreen(false, true); } catch (e2) { fail('Module', e2); }
      }
    });
  } catch (e) {
    fail('screen-sync', e);
  }
}

[find(cfg.fullscreen), find('#fsExit')].forEach(function (btn) {
  if (!btn) return;
  if (!document.fullscreenEnabled) {
    btn.hidden = true;
    btn.style.display = 'none';
    return;
  }
  btn.addEventListener('click', toggleFullscreen);
});

// ---- emscripten Module ---------------------------------------------------------------------
// the command line is built once, from the URL, before the start tap -- given to callMain
// only (not Module.arguments)
var Module = {
  canvas: canvas,
  // xrick.js asks this first for every file it loads: serve xrick.wasm from cfg.wasmUrl
  // when the page sets one (e.g. a CMS media URL), else from next to xrick.js
  locateFile: function (path, scriptDirectory) {
    if (path === 'xrick.wasm' && cfg.wasmUrl) return cfg.wasmUrl;
    return scriptDirectory + path;
  },
  print: function (text) { console.log(text); keepLine(text); },
  printErr: function (text) { console.error(text); keepLine(text); },
  setStatus: function (text) { if (text) setStatus(text); },
  onRuntimeInitialized: function () {
    runtimeReady = true;
    setLabel(args.indexOf('-demo') >= 0 ? 'click or touch to watch the demo' : 'click or touch to play');
    setStatus('');
    if (startBox) startBox.addEventListener('click', start);
    else start();   // no start element: nothing to click, but then no sound on most browsers
  },
  onExit: function (status) {
    // an argument the game refuses (e.g. -game 2 on the web, W1.4) ends in sysarg_fail,
    // whose first line is "xrick [version #...]: <reason>"; Esc (quit) exits with 0
    var fail = lastLines.filter(function (l) { return /^xrick \[version #[^\]]*\]: /.test(l); }).pop();
    var why = fail ? fail.replace(/^xrick \[version #[^\]]*\]: /, '') : '';
    setStatus((status ? 'the game could not start' + (why ? ': ' + why : ' (' + status + ')')
                      : 'game ended') + ' -- reload the page to play again');
  }
};

var args = buildArgs();

function start() {
  if (start.done) return; // only ever runs once
  start.done = true;
  unlockAudio(); // inside the tap: iOS playback audio

  if (startBox) startBox.style.display = 'none';
  canvas.style.display = 'block'; // pages may keep the canvas hidden until now
  canvas.focus();
  window.xrickStartArgs = args; // for checking what the game was started with
  setupIDBFS(function () {
    Module.callMain(args);
    resumeSdlAudio();
    reassertCanvasSize();
  });
}

// Some mobile browsers (reported: Chrome on a Samsung Galaxy S23 Edge, 2026-10-12) leave
// the canvas's actual width/height at 0 shortly after callMain creates it -- audio runs
// fine, so the game itself isn't stuck, only the canvas's pixel buffer. Confirmed from the
// browser side that toggling fullscreen fixes it; traced that to sysvid.c's
// sysvid_toggleFullscreen() re-asserting the window size via SDL_SetWindowSize and forcing
// a repaint when leaving fullscreen, rather than it being a generic "any event forces a
// recompute" thing (a plain window resize event, tried first, did nothing -- SDL wasn't
// listening for that one here). sysvid_reassertSize() (sysvid.c, __EMSCRIPTEN__ only) does
// the same two calls directly, exported via build.sh's EXPORTED_FUNCTIONS so this can call
// it without needing the player to find and tap the fullscreen button themselves.
function reassertCanvasSize() {
  try { Module.ccall('sysvid_reassertSize', null, [], []); } catch (e) { /* ignore */ }
}

canvas.addEventListener('webglcontextlost', function (e) {
  e.preventDefault();
  setStatus('WebGL context lost -- reload the page');
});
