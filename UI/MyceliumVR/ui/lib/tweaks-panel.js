'use strict';

// Requires: state.js, icons.js, data.js (ACCENTS)

function renderTweaksPanel() {
  var root = document.getElementById('tweaks-root');
  if (!root) {
    root = document.createElement('div');
    root.id = 'tweaks-root';
    var shell = document.getElementById('shell');
    if (shell) shell.appendChild(root);
  }

  var T = state.tweaks;

  var drawerHTML = '';
  if (state.tweaksOpen) {
    drawerHTML =
      '<div class="tweaks-panel" style="position:absolute;bottom:84px;right:14px;z-index:50">' +
        '<div class="hdr"><span class="dot"></span> Tweaks</div>' +

        '<div class="sect"><div class="lbl">Accent</div><div class="swatches">' +
          ACCENTS.map(function (a) {
            return '<button class="swatch' + (T.accent === a.id ? ' active' : '') + '" title="' + a.name + '"' +
              ' data-action="tweak" data-key="accent" data-value="' + a.id + '"' +
              ' style="background:oklch(0.82 0.12 ' + a.hue + ');box-shadow:0 0 12px oklch(0.82 0.12 ' + a.hue + '/0.4);color:oklch(0.82 0.12 ' + a.hue + ')">' +
            '</button>';
          }).join('') +
        '</div></div>' +

        '<div class="sect"><div class="lbl">Density</div><div class="opts">' +
          ['compact', 'cozy', 'comfortable'].map(function (d) {
            return '<button class="opt' + (T.density === d ? ' active' : '') + '" data-action="tweak" data-key="density" data-value="' + d + '">' +
              d[0].toUpperCase() + d.slice(1) + '</button>';
          }).join('') +
        '</div></div>' +

        '<div class="sect"><div class="lbl">Editor Layout</div><div class="opts">' +
          [['classic','Classic'],['vertical','Vertical'],['focused','Focused']].map(function (opt) {
            return '<button class="opt' + (T.layout === opt[0] ? ' active' : '') + '" data-action="tweak" data-key="layout" data-value="' + opt[0] + '">' + opt[1] + '</button>';
          }).join('') +
        '</div></div>' +

        '<div class="sect"><div class="lbl">Play Mode HUD</div>' +
          '<div style="display:flex;flex-direction:column;gap:6px">' +
            [['hudPerf','Performance'],['hudCamera','Camera'],['hudSession','Session'],['reticle','Reticle']].map(function (row) {
              return '<div class="toggle-row"><span>' + row[1] + '</span>' +
                '<span class="toggle' + (T[row[0]] ? ' on' : '') + '" data-action="tweak-bool" data-key="' + row[0] + '"></span></div>';
            }).join('') +
          '</div>' +
        '</div>' +

        '<div class="sect"><div class="lbl">Background tone</div><div class="opts">' +
          [['deep','Deep'],['neutral','Neutral'],['warm','Warm']].map(function (opt) {
            return '<button class="opt' + (T.tone === opt[0] ? ' active' : '') + '" data-action="tweak" data-key="tone" data-value="' + opt[0] + '">' + opt[1] + '</button>';
          }).join('') +
        '</div></div>' +
      '</div>';
  }

  root.innerHTML =
    '<button class="tweaks-fab' + (state.tweaksOpen ? ' open' : '') + '" data-action="toggle-tweaks" title="Tweaks"' +
      ' style="position:absolute;bottom:36px;right:14px;z-index:50">' + ICO.tweaks + '</button>' +
    drawerHTML;
}
