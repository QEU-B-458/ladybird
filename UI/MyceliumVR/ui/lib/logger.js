// MyceliumVR UI runtime debugger.
// Must load first — catches errors thrown by every subsequent script.
(function () {
  'use strict';

  var panel = null;
  var lines = [];

  function ensurePanel() {
    if (panel) return;
    panel = document.createElement('div');
    panel.id = '__mvr_log_panel';
    panel.style.cssText = [
      'display:none',
      'position:fixed',
      'inset:0',
      'z-index:99999',
      'background:#0d0907',
      'color:#e8d5c0',
      'font:12px/1.75 "JetBrains Mono",ui-monospace,SFMono-Regular,Menlo,monospace',
      'padding:0',
      'overflow:hidden',
      'flex-direction:column',
    ].join(';');

    var header = document.createElement('div');
    header.style.cssText = [
      'padding:12px 16px 10px',
      'border-bottom:1px solid #3a2010',
      'background:#1a0e08',
      'color:#ff8855',
      'font-size:11px',
      'letter-spacing:0.12em',
      'text-transform:uppercase',
      'font-weight:600',
      'flex-shrink:0',
      'display:flex',
      'align-items:center',
      'gap:12px',
    ].join(';');

    var dot = document.createElement('span');
    dot.style.cssText = 'width:8px;height:8px;border-radius:50%;background:#ff4422;box-shadow:0 0 8px #ff4422;flex-shrink:0;animation:__mvrBlink 1s ease-in-out infinite';
    var title = document.createElement('span');
    title.textContent = 'MyceliumVR — UI Runtime Error';
    var dismiss = document.createElement('button');
    dismiss.textContent = 'dismiss';
    dismiss.style.cssText = [
      'all:unset',
      'margin-left:auto',
      'font-size:10px',
      'color:#886655',
      'cursor:pointer',
      'letter-spacing:0.08em',
      'padding:2px 8px',
      'border:1px solid #3a2010',
      'border-radius:3px',
    ].join(';');
    dismiss.onclick = function () {
      panel.style.display = 'none';
    };
    header.appendChild(dot);
    header.appendChild(title);
    header.appendChild(dismiss);

    var scroll = document.createElement('div');
    scroll.id = '__mvr_log_scroll';
    scroll.style.cssText = 'flex:1;overflow-y:auto;padding:10px 16px';

    panel.appendChild(header);
    panel.appendChild(scroll);

    // Inject blink animation
    if (!document.getElementById('__mvr_blink_style')) {
      var s = document.createElement('style');
      s.id = '__mvr_blink_style';
      s.textContent = '@keyframes __mvrBlink{0%,100%{opacity:.4}50%{opacity:1}}';
      document.head.appendChild(s);
    }

    function mount() { document.body.appendChild(panel); }
    if (document.body) mount();
    else document.addEventListener('DOMContentLoaded', mount);
  }

  var COLORS = {
    error: '#ff5533',
    warn:  '#ffaa33',
    info:  '#5ecfff',
    debug: '#88cc88',
    ok:    '#66dd88',
  };

  function write(level, parts) {
    var msg = parts.join(' ');
    lines.push({ level: level, msg: msg, ts: Date.now() });

    ensurePanel();
    panel.style.display = 'flex';

    var scroll = document.getElementById('__mvr_log_scroll');
    if (!scroll) return;

    var row = document.createElement('div');
    row.style.cssText = 'display:flex;gap:10px;padding:2px 0;border-bottom:1px solid #1e1208;align-items:flex-start';

    var badge = document.createElement('span');
    badge.textContent = level.toUpperCase().padEnd(5);
    badge.style.cssText = 'flex-shrink:0;font-size:9px;letter-spacing:0.08em;color:' + (COLORS[level] || '#aaa') + ';margin-top:2px';

    var text = document.createElement('span');
    text.style.cssText = 'flex:1;white-space:pre-wrap;word-break:break-all;font-size:11px;color:' + (COLORS[level] || '#e8d5c0');
    text.textContent = msg;

    row.appendChild(badge);
    row.appendChild(text);
    scroll.appendChild(row);
    scroll.scrollTop = scroll.scrollHeight;

    // Mirror to native console
    try {
      var fn = console[level === 'error' ? 'error' : level === 'warn' ? 'warn' : 'log'];
      fn.call(console, '[MVR ' + level.toUpperCase() + ']', msg);
    } catch (e) {}
  }

  // Public API — available as window.__mvrLog from any script
  window.__mvrLog = {
    error: function () { write('error', Array.prototype.slice.call(arguments)); },
    warn:  function () { write('warn',  Array.prototype.slice.call(arguments)); },
    info:  function () { write('info',  Array.prototype.slice.call(arguments)); },
    debug: function () { write('debug', Array.prototype.slice.call(arguments)); },
    ok:    function () { write('ok',    Array.prototype.slice.call(arguments)); },
    lines: lines,
    show:  function () { ensurePanel(); if (panel) panel.style.display = 'flex'; },
    hide:  function () { if (panel) panel.style.display = 'none'; },
  };

  // Catch uncaught synchronous errors from any script
  window.addEventListener('error', function (e) {
    var loc = (e.filename ? e.filename.replace(/.*\//, '') : '?') + ':' + (e.lineno || '?') + ':' + (e.colno || '?');
    var stack = e.error && e.error.stack ? '\n' + e.error.stack : '';
    write('error', [(e.message || 'Unknown error'), '(' + loc + ')' + stack]);
  });

  // Catch unhandled promise rejections
  window.addEventListener('unhandledrejection', function (e) {
    var r = e.reason;
    var msg = r ? (r.message || String(r)) : 'unknown rejection';
    var stack = r && r.stack ? '\n' + r.stack : '';
    write('error', ['Unhandled promise rejection:', msg + stack]);
  });

  // After 2.5s, check whether the UI shell actually mounted
  document.addEventListener('DOMContentLoaded', function () {
    setTimeout(function () {
      var shell = document.getElementById('shell') || document.querySelector('.topbar');
      if (!shell) {
        write('warn', ['UI shell not found after 2.5s — init() may have crashed or #root element is missing.']);
        write('info', ['Expected load order: logger.js → data.js → state.js → icons.js → tweaks.js → stats.js → hierarchy.js → inspector.js → console.js → viewport.js → topbar.js → statusbar.js → tweaks-panel.js → events.js → bridge.js → main.js']);
        write('info', ['Expected DOM: <div id="root"> in body before the scripts.']);
      } else {
        write('ok', ['UI shell mounted — runtime OK.']);
        // Auto-hide after 3s if no errors
        if (lines.every(function (l) { return l.level !== 'error' && l.level !== 'warn'; })) {
          setTimeout(function () { if (panel) panel.style.display = 'none'; }, 3000);
        }
      }
    }, 2500);
  });
})();
