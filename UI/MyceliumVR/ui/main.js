'use strict';

// Entry point — all logic lives in lib/*.js, loaded before this file.

function render() {
  applyTweaks(state.tweaks);
  renderTopbar();
  renderStage();
  renderStatusbar();
  renderTweaksPanel();
}

function renderStage() {
  var stage  = document.getElementById('stage');
  var left   = document.getElementById('panel-left');
  var wrap   = document.getElementById('viewport-wrap');
  var right  = document.getElementById('panel-right');
  var bottom = document.getElementById('panel-bottom');

  stage.className = 'stage ' + state.mode + ' layout-' + state.tweaks.layout;

  if (state.mode === 'edit') {
    left.style.display = right.style.display = bottom.style.display = '';
    renderHierarchy(left);
    renderViewport(wrap);
    renderInspector(right);
    var rgTmp = document.createElement('div');
    rgTmp.innerHTML = renderGraphHTML();
    right.appendChild(rgTmp.firstChild);
    patchRenderTimings();
    renderConsole(bottom);
  } else {
    left.style.display = right.style.display = bottom.style.display = 'none';
    renderViewportPlay(wrap);
  }
}

(function init() {
  if (typeof __mvrLog !== 'undefined') __mvrLog.info('main.js init() starting');

  var root = document.getElementById('root');
  if (!root) {
    if (typeof __mvrLog !== 'undefined') __mvrLog.error('init() failed: no #root element in DOM');
    return;
  }

  root.id        = 'shell';
  root.className = 'shell';
  root.innerHTML =
    '<div class="topbar" id="topbar"></div>' +
    '<div class="stage edit" id="stage">' +
      '<div class="panel-left"    id="panel-left"></div>'    +
      '<div class="viewport-wrap" id="viewport-wrap"></div>' +
      '<div class="panel-right"   id="panel-right"></div>'   +
      '<div class="panel-bottom"  id="panel-bottom"></div>'  +
    '</div>' +
    '<div class="statusbar" id="statusbar"></div>';

  try {
    render();
    if (typeof __mvrLog !== 'undefined') __mvrLog.ok('render() completed — shell mounted');
  } catch (err) {
    if (typeof __mvrLog !== 'undefined') __mvrLog.error('render() threw: ' + err.message + (err.stack ? '\n' + err.stack : ''));
    throw err;
  }

  setInterval(tickStats, 120);
})();
