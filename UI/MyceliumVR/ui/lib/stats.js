'use strict';

// Requires: state.js

function tickStats() {
  var s = state.stats;
  var fms = 6.3 + Math.random() * 1.6 + (Math.random() > 0.94 ? 3 + Math.random() * 4 : 0);
  var hist = s.frameHistory.slice(1);
  hist.push(fms);
  state.stats = {
    fps: 1000 / fms,
    frameMs: fms,
    passMs:  fms * 0.88,
    entities: s.entities,
    drawCalls: s.drawCalls,
    triangles: s.triangles,
    camX: s.camX + (Math.random() - 0.5) * 0.02,
    camY: s.camY + (Math.random() - 0.5) * 0.005,
    camZ: s.camZ,
    yaw:  s.yaw  + (Math.random() - 0.5) * 0.3,
    pitch: s.pitch,
    frameHistory: hist,
  };
  patchStats();
}

// Patches the live stat DOM nodes without triggering a full render().
function patchStats() {
  var s = state.stats;

  function patch(id, text) {
    var e = document.getElementById(id);
    if (e) e.textContent = text;
  }

  patch('stat-fps',   s.fps.toFixed(1));
  patch('stat-fms',   s.frameMs.toFixed(2) + ' ms');
  patch('stat-gpu',   s.passMs.toFixed(2)  + ' ms');
  patch('stat-draw',  s.drawCalls);
  patch('stat-tri',   (s.triangles / 1000).toFixed(1) + 'k');
  patch('stat-pos',   s.camX.toFixed(2) + ', ' + s.camY.toFixed(2) + ', ' + s.camZ.toFixed(2));
  patch('stat-yaw',   s.yaw.toFixed(1)   + '\u00b0');
  patch('stat-pitch', s.pitch.toFixed(1) + '\u00b0');

  var spark = document.getElementById('stat-spark');
  if (spark) {
    var maxMs = Math.max(12, Math.max.apply(null, s.frameHistory));
    var bars = spark.children;
    for (var i = 0; i < s.frameHistory.length; i++) {
      if (!bars[i]) continue;
      var ms = s.frameHistory[i];
      bars[i].style.height = Math.min(100, (ms / maxMs) * 100) + '%';
      bars[i].className = 'bar' + (ms > 10 ? ' spike' : '');
    }
  }

  var topStat = document.getElementById('topbar-stat');
  if (topStat) {
    topStat.innerHTML = s.fps.toFixed(0) + ' fps<span style="color:var(--fg-4)"> \u00b7 ' + s.frameMs.toFixed(2) + 'ms</span>';
  }
}
