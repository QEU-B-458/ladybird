'use strict';

// Requires: state.js, icons.js, data.js (WORLDS)

function renderTopbar() {
  var s = state.stats;

  var worldMenu = '';
  if (state.worldMenuOpen) {
    worldMenu = '<div class="world-menu">' +
      WORLDS.map(function (w) {
        return '<button data-action="set-world" data-value="' + w.id + '" class="' + (w.id === state.world ? 'active' : '') + '">' +
          w.id + ' <span style="color:var(--fg-4);margin-left:6px">\u00b7 ' + w.hint + '</span>' +
        '</button>';
      }).join('') +
      '<div class="sep"></div>' +
      '<button data-action="noop"><span style="color:var(--fg-3)">+ mount folder\u2026</span></button>' +
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
