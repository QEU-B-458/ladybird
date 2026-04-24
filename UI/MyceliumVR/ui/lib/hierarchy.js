'use strict';

// Requires: state.js, icons.js, data.js

var treeRows = new Map();

function hierVisibleEntities() {
  var q   = state.hierQ ? state.hierQ.toLowerCase() : '';
  var out = [];
  var visible = new Map();
  for (var i = 0; i < SPONZA_HIERARCHY.length; i++) {
    var e = SPONZA_HIERARCHY[i];
    if (e.depth === 0) { visible.set(e.id, true); out.push(e); continue; }
    var show = visible.get(e.parent) && state.expandedIds.has(e.parent);
    visible.set(e.id, show);
    if (show && (!q || e.name.toLowerCase().indexOf(q) !== -1)) out.push(e);
  }
  return out;
}

function hasChildren(id) {
  for (var i = 0; i < SPONZA_HIERARCHY.length; i++) {
    if (SPONZA_HIERARCHY[i].parent === id) return true;
  }
  return false;
}

function alphaTagHTML(alpha) {
  if (!alpha || alpha === 'opaque') return '';
  var cls = alpha === 'clip' ? 'clip' : alpha === 'hash' ? 'hash' : 'blend';
  return '<div class="tag ' + cls + '">' + alpha.toUpperCase() + '</div>';
}

function renderHierarchy(container) {
  if (!container._built) {
    container.innerHTML =
      '<div class="panel">' +
        '<div class="panel-tabs" id="left-tabs">' +
          '<button class="panel-tab active" data-action="ltab" data-tab="scene">Scene</button>' +
          '<button class="panel-tab" data-action="ltab" data-tab="browser">Browser</button>' +
        '</div>' +
        '<div id="left-body" style="flex:1; display:flex; flex-direction:column; min-height:0">' +
          '<div id="scene-view" style="display:flex; flex-direction:column; flex:1; min-height:0">' +
            '<div class="tree-filter" style="display:flex; gap:8px; align-items:center">' +
              '<input id="hier-filter" style="flex:1" placeholder="filter entities\u2026" autocomplete="off">' +
              '<button title="Refresh Scene" style="padding:4px 8px; font-size:10px; cursor:pointer" onclick="document.title=\'mvr:refresh_scene\'">Refresh</button>' +
            '</div>' +
            '<div class="panel-body"><div class="tree no-select" id="hier-tree"></div></div>' +
          '</div>' +
          '<div id="browser-view" style="display:none; flex:1; min-height:0; overflow-y:auto">' +
            '<div id="worlds-list"></div>' +
          '</div>' +
        '</div>' +
      '</div>';
    container._built = true;

    var filterEl = document.getElementById('hier-filter');
    if (filterEl) {
      filterEl.addEventListener('input', function (ev) {
        state.hierQ = ev.target.value;
        diffHierTree();
      });
    }
  }

  syncLeftTabs();
}

function syncLeftTabs() {
  var tabs = document.querySelectorAll("[data-action='ltab']");
  for (var i = 0; i < tabs.length; i++) {
    tabs[i].classList.toggle('active', tabs[i].dataset.tab === state.leftTab);
  }

  var sceneView   = document.getElementById('scene-view');
  var browserView = document.getElementById('browser-view');
  if (!sceneView || !browserView) return;

  if (state.leftTab === 'scene') {
    sceneView.style.display = 'flex';
    browserView.style.display = 'none';
    document.title = 'mvr:refresh_scene';
    diffHierTree();
  } else {
    sceneView.style.display = 'none';
    browserView.style.display = 'block';
    if (typeof window.renderWorlds === 'function') window.renderWorlds();
  }
}

function diffHierTree() {
  var tree = document.getElementById('hier-tree');
  if (!tree) return;

  var visible = hierVisibleEntities();
  var liveIds = new Set(visible.map(function (e) { return e.id; }));

  // Remove rows no longer visible
  treeRows.forEach(function (row, id) {
    if (!liveIds.has(id)) { row.remove(); treeRows.delete(id); }
  });

  var prev = null;
  for (var i = 0; i < visible.length; i++) {
    var e    = visible[i];
    var sel  = state.selectedId === e.id;
    var open = state.expandedIds.has(e.id);
    var kids = hasChildren(e.id);
    var vis  = state.visibility[e.id] !== false;

    var row = treeRows.get(e.id);
    if (!row) {
      row = document.createElement('div');
      row.dataset.id     = e.id;
      row.dataset.action = 'select';
      treeRows.set(e.id, row);
    }

    row.className     = 'tree-row' + (sel ? ' selected' : '');
    row.dataset.depth = Math.min(3, e.depth);
    row.innerHTML =
      '<span class="chev' + (open ? ' open' : '') + (kids ? '' : ' leaf') + '" data-action="expand" data-id="' + e.id + '">' +
        (kids ? ICO.chev : '') +
      '</span>' +
      '<span class="icon">' + (ICO.kinds[e.kind] || ICO.kinds.mesh) + '</span>' +
      '<span class="name">' + e.name + '</span>' +
      (e.meta ? '<span class="tag static">' + e.meta + '</span>' : '') +
      alphaTagHTML(e.alpha) +
      (e.static && !e.alpha && e.kind === 'mesh' ? '<span class="tag static">S</span>' : '') +
      '<span class="eye' + (vis ? '' : ' off') + '" data-action="vis" data-id="' + e.id + '">' +
        (vis ? ICO.eye : ICO.eyeOff) +
      '</span>';

    if (!row.parentNode) {
      if (prev && prev.nextSibling) tree.insertBefore(row, prev.nextSibling);
      else tree.appendChild(row);
    }
    prev = row;
  }
}
