'use strict';

// Requires: state.js, stats.js, console.js, hierarchy.js, data.js

// Single receiver for all C++ → overlay pushes.
// C++ calls window.__myceliumBridge.<method>(...) for every data channel.
window.__myceliumBridge = {

  // Called every frame with current stats.
  update: function (fps, frameTimeMs, entityCount, drawCalls, triangleCount, camX, camY, camZ, yaw, pitch, overlayFocused) {
    var hist = state.stats.frameHistory.slice(1);
    hist.push(frameTimeMs);
    state.stats = Object.assign({}, state.stats, {
      fps:          fps,
      frameMs:      frameTimeMs,
      passMs:       frameTimeMs * 0.88,
      entities:     entityCount,
      drawCalls:    drawCalls,
      triangles:    triangleCount,
      camX:         camX,
      camY:         camY,
      camZ:         camZ,
      yaw:          yaw,
      pitch:        pitch,
      frameHistory: hist,
    });
    if (overlayFocused !== undefined) state.focused = !!overlayFocused;
    patchStats();
  },

  // Called every frame with GPU pass timings [{name, ms}, ...].
  renderTimings: function (passes) {
    state.renderTimings = passes;
    patchRenderTimings();
  },

  // Called when the full entity list changes.
  setHierarchy: function (entities) {
    SPONZA_HIERARCHY.length = 0;
    for (var i = 0; i < entities.length; i++) SPONZA_HIERARCHY.push(entities[i]);
    treeRows.clear();
    var tree = document.getElementById('hier-tree');
    if (tree) tree.innerHTML = '';
    diffHierTree();
    if (typeof __mvrLog !== 'undefined') __mvrLog.info('bridge.setHierarchy: ' + entities.length + ' entities');
  },

  // Called when the selected entity ID changes.
  selectionChanged: function (entityId) {
    state.selectedId = entityId;
    render();
  },

  // Called after a selection change with full component data.
  componentUpdate: function (data) {
    state.selectedComponents = data;
    render();
  },

  // Called for each queued log entry (up to 32/frame).
  // level: 'INFO' | 'WARN' | 'ERROR' | 'OK' | 'JS'
  log: function (level, source, message) {
    var k = level === 'WARN' ? 'warn' : level === 'ERROR' ? 'err' : level === 'JS' ? 'js' : level === 'OK' ? 'ok' : 'info';
    pushLog(message, level, source, k);
  },

  // Called once after init with the full registered bridge function list.
  registerFunctions: function (names) {
    state.bridgeFunctions = names;
    if (state.consoleTab === 'bridge') syncConsoleTabs();
  },

  // Called once after init with the list of mounted VFS prefixes.
  registerAssets: function (mounts) {
    state.vfsMounts = mounts;
    if (state.consoleTab === 'assets') syncConsoleTabs();
  },
};

if (typeof __mvrLog !== 'undefined') {
  __mvrLog.info('bridge.js loaded — window.__myceliumBridge registered.');
}
