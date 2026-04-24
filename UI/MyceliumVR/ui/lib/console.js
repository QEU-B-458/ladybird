'use strict';

// Requires: state.js, data.js (BRIDGE_FNS, INITIAL_LOGS)

var consoleRows = new Map(); // log index → <div>

function renderConsole(container) {
  if (!container._built) {
    container.innerHTML =
      '<div class="console">' +
        '<div class="panel-tabs" id="console-tabs">' +
          '<button class="panel-tab active" data-action="ctab" data-tab="console">Console</button>' +
          '<button class="panel-tab" data-action="ctab" data-tab="bridge">Bridge API</button>' +
          '<button class="panel-tab" data-action="ctab" data-tab="assets">Assets</button>' +
        '</div>' +
        '<div id="console-body" class="console-log"></div>' +
        '<div class="console-input" id="console-input-row">' +
          '<span class="prompt">mycelium&gt;</span>' +
          '<input id="console-input" placeholder="mycelium.setTransform(15, [0,0,0])" autocomplete="off">' +
          '<span style="color:var(--fg-4);font-size:10px">\u23ce</span>' +
        '</div>' +
      '</div>';
    container._built = true;

    var inp = document.getElementById('console-input');
    if (inp) {
      inp.addEventListener('keydown', function (ev) {
        if (ev.key !== 'Enter' || !ev.target.value.trim()) return;
        var cmd = ev.target.value;
        pushLog('> ' + cmd, 'JS', 'REPL', 'js');
        document.title = 'mvr:eval:' + cmd;
        ev.target.value = '';
      });
    }
  }

  syncConsoleTabs();
}

function syncConsoleTabs() {
  var tabs = document.querySelectorAll("[data-action='ctab']");
  for (var i = 0; i < tabs.length; i++) {
    tabs[i].classList.toggle('active', tabs[i].dataset.tab === state.consoleTab);
  }

  var body     = document.getElementById('console-body');
  var inputRow = document.getElementById('console-input-row');
  if (!body) return;

  if (state.consoleTab === 'console') {
    if (inputRow) inputRow.style.display = '';
    appendNewLogs(body);
  } else if (state.consoleTab === 'bridge') {
    if (inputRow) inputRow.style.display = 'none';
    var fns = state.bridgeFunctions.length > 0 ? state.bridgeFunctions : BRIDGE_FNS;
    body.innerHTML =
      '<div style="padding:12px;font-family:var(--f-mono);font-size:11px;color:var(--fg-2)">' +
        '<div class="micro" style="margin-bottom:8px">Bridge API \u00b7 ' + fns.length + ' registered</div>' +
        fns.map(function (fn) {
          return '<div style="padding:2px 0"><span style="color:var(--accent)">fn</span>&nbsp;&nbsp;' + fn +
            '<span style="color:var(--fg-4);margin-left:8px;font-size:10px">(\u2026)</span></div>';
        }).join('') +
      '</div>';
  } else {
    if (inputRow) inputRow.style.display = 'none';
    var mounts = state.vfsMounts;
    body.innerHTML =
      '<div style="padding:12px;font-family:var(--f-mono);font-size:11px;color:var(--fg-2)">' +
        '<div class="micro" style="margin-bottom:8px">Virtual File System \u00b7 ' + mounts.length + ' mounts</div>' +
        (mounts.length === 0
          ? '<div style="color:var(--fg-4);padding:4px 0">No mounts \u2014 waiting for engine\u2026</div>'
          : mounts.map(function (prefix) {
              return '<div style="padding:2px 0"><span style="color:var(--accent)">vfs</span>&nbsp;&nbsp;' + prefix + '</div>';
            }).join('')) +
      '</div>';
  }
}

function appendNewLogs(body) {
  var start = consoleRows.size;
  for (var i = start; i < state.logs.length; i++) {
    var log = state.logs[i];
    var row = document.createElement('div');
    row.className = 'log-row ' + (log.k || 'info');
    row.innerHTML =
      '<span class="ts">'      + log.ts  + '</span>' +
      '<span class="lvl">'     + log.lvl + '</span>' +
      '<span><span class="tag-src">' + log.src + '</span>' + log.msg + '</span>';
    body.appendChild(row);
    consoleRows.set(i, row);
  }
  body.scrollTop = body.scrollHeight;
}

function pushLog(msg, lvl, src, k) {
  var n   = state.logs.length;
  var pad = function (v, l) { return String(v).padStart(l, '0'); };
  var ts  = '00:' + pad(n, 2) + '.' + pad(Math.floor(Math.random() * 900) + 100, 3);
  state.logs = state.logs.concat([{ ts: ts, lvl: lvl, src: src, msg: msg, k: k || 'info' }]);

  if (state.consoleTab === 'console') {
    var body = document.getElementById('console-body');
    if (body) appendNewLogs(body);
  }
}
