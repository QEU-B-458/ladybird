'use strict';

// Requires: state.js

function patchRenderTimings() {
  var container = document.getElementById('rg-passes');
  if (!container) return;

  var passes = state.renderTimings || [];
  if (passes.length === 0) return;

  var maxMs = 0;
  for (var i = 0; i < passes.length; i++)
    if (passes[i].ms > maxMs) maxMs = passes[i].ms;
  if (maxMs < 0.5) maxMs = 0.5;

  var html = '';
  for (var j = 0; j < passes.length; j++) {
    var p = passes[j];
    var pct = Math.min(100, (p.ms / maxMs) * 100);
    var cls = p.ms >= 4.0 ? 'rg-bar hot' : p.ms >= 1.0 ? 'rg-bar warm' : 'rg-bar';
    html += '<div class="rg-row">' +
      '<div class="rg-name">' + p.name + '</div>' +
      '<div class="rg-track"><div class="' + cls + '" style="width:' + pct.toFixed(1) + '%"></div></div>' +
      '<div class="rg-ms">' + p.ms.toFixed(2) + '</div>' +
    '</div>';
  }
  container.innerHTML = html;
}

function renderGraphHTML() {
  return '<div class="panel">' +
    '<div class="panel-header">' +
      'Render Graph' +
      '<span class="count" id="rg-total">\u00b7 GPU</span>' +
    '</div>' +
    '<div class="panel-body">' +
      '<div class="rg-header">' +
        '<span class="rg-name">Pass</span>' +
        '<span class="rg-track"></span>' +
        '<span class="rg-ms">ms</span>' +
      '</div>' +
      '<div id="rg-passes"></div>' +
    '</div>' +
  '</div>';
}
