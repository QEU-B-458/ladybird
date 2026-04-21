'use strict';

// Requires: data.js

function _storageGet(key, fallback) {
  try { return localStorage.getItem(key) || fallback; } catch (e) { return fallback; }
}

function _loadTweaks() {
  try { return JSON.parse(localStorage.getItem('mvr:tweaks') || 'null') || {}; } catch (e) { return {}; }
}

var state = {
  mode:         _storageGet('mvr:mode',  'edit'),
  world:        _storageGet('mvr:world', 'm255-sponza'),
  selectedId:   15,
  visibility:   {},
  passToggled:  {},
  selectedPass: null,
  logs:         [],
  focused:      false,
  tweaks:       Object.assign({}, DEFAULT_TWEAKS, _loadTweaks()),
  worldMenuOpen: false,
  tweaksOpen:    false,
  hierQ:         '',
  expandedIds:   new Set(SPONZA_HIERARCHY.filter(function (e) { return e.expanded; }).map(function (e) { return e.id; })),
  openSections:  new Set(['transform', 'meshrenderer', 'material']),
  consoleTab:       'console',
  renderTimings:    [],
  selectedComponents: null,
  bridgeFunctions:  [],
  vfsMounts:        [],
  stats: {
    fps: 144.0, frameMs: 6.94, entities: 17, drawCalls: 38,
    triangles: 155312, passMs: 8.07,
    camX: -8.44, camY: 3.20, camZ: 0.00, yaw: 92.3, pitch: -4.1,
    frameHistory: Array.from({ length: 48 }, function () { return 6.5 + Math.random() * 1.2; }),
  },
};

function setState(patch) {
  var next = typeof patch === 'function' ? patch(state) : patch;
  Object.assign(state, next);
  try {
    localStorage.setItem('mvr:mode',   state.mode);
    localStorage.setItem('mvr:world',  state.world);
    localStorage.setItem('mvr:tweaks', JSON.stringify(state.tweaks));
  } catch (e) {}
  render();
}
