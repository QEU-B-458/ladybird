'use strict';

// Requires: state.js, stats.js, icons.js, data.js

function perfHudHTML(corner) {
  var s     = state.stats;
  var maxMs = Math.max(12, Math.max.apply(null, s.frameHistory));
  var bars  = s.frameHistory.map(function (ms) {
    return '<div class="bar' + (ms > 10 ? ' spike' : '') + '" style="height:' +
      Math.min(100, (ms / maxMs) * 100) + '%"></div>';
  }).join('');
  return '<div class="hud ' + corner + '">' +
    '<h3><span class="hyphal-dot"></span> Performance</h3>' +
    '<div class="stat-grid">' +
      '<div class="k">FPS</div>   <div class="v accent" id="stat-fps">'  + s.fps.toFixed(1)          + '</div>' +
      '<div class="k">Frame</div> <div class="v" id="stat-fms">'          + s.frameMs.toFixed(2)      + ' ms</div>' +
      '<div class="k">GPU</div>   <div class="v" id="stat-gpu">'          + s.passMs.toFixed(2)       + ' ms</div>' +
      '<div class="k">Draws</div> <div class="v" id="stat-draw">'         + s.drawCalls               + '</div>' +
      '<div class="k">Tris</div>  <div class="v" id="stat-tri">'          + (s.triangles/1000).toFixed(1) + 'k</div>' +
    '</div>' +
    '<div class="spark" id="stat-spark">' + bars + '</div>' +
  '</div>';
}

function camHudHTML(corner) {
  var s = state.stats;
  return '<div class="hud ' + corner + '">' +
    '<h3><span class="hyphal-dot"></span> Camera</h3>' +
    '<div class="stat-grid">' +
      '<div class="k">Pos</div>      <div class="v mono" id="stat-pos">'   + s.camX.toFixed(2) + ', ' + s.camY.toFixed(2) + ', ' + s.camZ.toFixed(2) + '</div>' +
      '<div class="k">Yaw</div>      <div class="v" id="stat-yaw">'        + s.yaw.toFixed(1)   + '\u00b0</div>' +
      '<div class="k">Pitch</div>    <div class="v" id="stat-pitch">'      + s.pitch.toFixed(1) + '\u00b0</div>' +
      '<div class="k">FOV</div>      <div class="v">70.0\u00b0</div>' +
      '<div class="k">Near/Far</div> <div class="v">0.1 / 100</div>' +
    '</div>' +
  '</div>';
}

function sessionHudHTML(corner) {
  return '<div class="hud ' + corner + '">' +
    '<h3><span class="hyphal-dot"></span> Session</h3>' +
    '<div class="stat-grid">' +
      '<div class="k">World</div>   <div class="v mono">' + state.world + '</div>' +
      '<div class="k">Script</div>  <div class="v mono">main.js \u00b7 run</div>' +
      '<div class="k">Focus</div>   <div class="v" style="color:' + (state.focused ? 'var(--accent)' : 'var(--fg-2)') + '">' + (state.focused ? 'UI' : 'Scene') + '</div>' +
      '<div class="k">Overlay</div> <div class="v">visible</div>' +
    '</div>' +
    '<div class="small" style="margin-top:8px;color:var(--fg-4);font-family:var(--f-mono);font-size:10px;letter-spacing:0.04em">' +
      '[F1] hide &nbsp; [F2] <span data-action="toggle-focus" style="color:var(--accent);cursor:pointer">toggle focus</span>' +
    '</div>' +
  '</div>';
}

function renderViewport(wrap) {
  wrap.innerHTML =
    '<div class="viewport"></div>' +
    perfHudHTML('corner-tl') +
    camHudHTML('corner-tr');
}

function renderViewportPlay(wrap) {
  var T = state.tweaks;
  wrap.innerHTML =
    (T.hudPerf    ? perfHudHTML('corner-tl')    : '') +
    (T.hudCamera  ? camHudHTML('corner-tr')     : '') +
    (T.hudSession ? sessionHudHTML('corner-bl') : '') +
    (T.reticle ?
      '<div style="position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);width:18px;height:18px;pointer-events:none">' +
        '<div style="position:absolute;inset:0;border:1px solid oklch(0.96 0.008 150/0.35);border-radius:50%"></div>' +
        '<div style="position:absolute;left:50%;top:50%;width:3px;height:3px;background:oklch(0.96 0.008 150/0.8);transform:translate(-50%,-50%);border-radius:50%"></div>' +
      '</div>'
    : '');
}
