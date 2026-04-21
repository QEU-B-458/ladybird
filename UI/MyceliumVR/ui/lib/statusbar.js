'use strict';

// Requires: state.js

function renderStatusbar() {
  document.getElementById('statusbar').innerHTML =
    '<span class="item"><span class="dot' + (state.focused ? '' : ' warn') + '"></span> ' + (state.focused ? 'UI focused' : 'Scene focused') + '</span>' +
    '<span class="item"><span class="dot"></span> ' + (state.mode === 'edit' ? 'Editor' : 'Playing') + '</span>' +
    '<span class="spacer"></span>' +
    '<span class="ghost">NVIDIA RTX 4070 \u00b7 vk 1.3.268</span>' +
    '<span class="item"><span class="dot"></span> realm ready</span>' +
    '<span class="ghost">F1 hud \u00b7 F2 focus \u00b7 Tab mode</span>';
}
