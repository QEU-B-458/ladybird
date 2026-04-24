'use strict';

// Requires: state.js, icons.js, data.js (WORLDS)

function renderTopbar() {
  var s = state.stats;

  var worldMenu = '';
  if (state.worldMenuOpen) {
    var loadedWorlds = WORLDS.filter(function(w) { return w.is_running || w.is_active; });
    
    worldMenu = '<div class="world-menu">' +
      (loadedWorlds.length === 0 
        ? '<div style="padding:10px;color:var(--fg-4);font-size:10px;text-align:center">No background worlds loaded</div>'
        : loadedWorlds.map(function (w) {
            var status = w.is_active ? 'active' : 'running';
            return '<button data-action="set-world" data-value="' + w.path + '" class="' + status + '">' +
              '<span>' + w.id + '</span>' +
              '<span style="color:var(--fg-4);margin-left:6px;font-size:0.8em">\u00b7 ' + w.hint + '</span>' +
              (w.is_active ? ' <span class="badge">ACTIVE</span>' : ' <span class="badge">BG</span>') +
            '</button>';
          }).join('')) +
      '<div class="sep"></div>' +
      '<button data-action="ltab" data-tab="browser" onclick="setState({worldMenuOpen:false})">' +
        '<span style="color:var(--accent)">+ load new world\u2026</span>' +
      '</button>' +
    '</div>';
  }

  document.getElementById('topbar').innerHTML =
    '<div class="brand">' + ICO.brand + '<span>MyceliumVR</span><span class="tag">v0.4 \u00b7 vulkan</span></div>' +
    '<div class="topbar-sep"></div>' +
    '<div class="mode-switch">' +
      '<button data-action="mode" data-value="edit" class="' + (state.mode === 'edit' ? 'active' : '') + '">Edit</button>' +
      '<button data-action="mode" data-value="play" class="' + (state.mode === 'play' ? 'active' : '') + '">Play</button>' +
    '</div>' +
    '<div class="topbar-sep"></div>' +
    '<span class="small mono" id="topbar-stat" style="color:var(--fg-3)">' +
      s.fps.toFixed(0) + ' fps<span style="color:var(--fg-4)"> \u00b7 ' + s.frameMs.toFixed(2) + 'ms</span>' +
    '</span>' +
    '<div class="topbar-spacer"></div>' +
    '<button class="icon-btn" title="Script runtime">' + ICO.script + '</button>' +
    '<button class="icon-btn" title="Toggle overlay">' + ICO.hud    + '</button>' +
    '<div class="world-picker" id="world-picker" data-action="toggle-world-menu" style="position:relative">' +
      '<span class="dot"></span>' +
      '<span>' + state.world + '</span>' +
      '<span class="path">&nbsp;\u00b7 UI/MyceliumVR/worlds/</span>' +
      ICO.caret +
      worldMenu +
    '</div>';
}
