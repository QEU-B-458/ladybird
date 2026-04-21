'use strict';

// Requires: data.js (ACCENTS, DEFAULT_TWEAKS)

var ACCENT_HUE  = {};
var DENSITY_VAL = { compact: 0.85, cozy: 1, comfortable: 1.15 };
var TONE_VARS   = {
  deep:    { '--bg-0': 'oklch(0.11 0.005 150)', '--bg-1': 'oklch(0.14 0.006 150)', '--bg-2': 'oklch(0.18 0.007 150)' },
  neutral: { '--bg-0': 'oklch(0.14 0.006 150)', '--bg-1': 'oklch(0.17 0.006 150)', '--bg-2': 'oklch(0.20 0.007 150)' },
  warm:    { '--bg-0': 'oklch(0.15 0.008 80)',  '--bg-1': 'oklch(0.19 0.009 80)',  '--bg-2': 'oklch(0.22 0.010 80)'  },
};

// Build ACCENT_HUE map from ACCENTS array (defined in data.js)
ACCENTS.forEach(function (a) { ACCENT_HUE[a.id] = a.hue; });

function applyTweaks(T) {
  var r = document.documentElement;
  r.style.setProperty('--accent-h', ACCENT_HUE[T.accent]  != null ? ACCENT_HUE[T.accent]  : 175);
  r.style.setProperty('--density',  DENSITY_VAL[T.density] != null ? DENSITY_VAL[T.density] : 1);
  var tone = TONE_VARS[T.tone] || TONE_VARS.neutral;
  Object.keys(tone).forEach(function (k) { r.style.setProperty(k, tone[k]); });
}
