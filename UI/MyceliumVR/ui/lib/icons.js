'use strict';

var ICO = {
  chev:   '<svg width="8" height="8" viewBox="0 0 8 8"><path d="M2.5 1.5 L5.5 4 L2.5 6.5" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  plus:   '<svg width="12" height="12" viewBox="0 0 12 12"><path d="M6 2 V10 M2 6 H10" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>',
  eye:    '<svg width="12" height="12" viewBox="0 0 12 12"><path d="M1 6 Q6 1.5 11 6 Q6 10.5 1 6 Z" fill="none" stroke="currentColor" stroke-width="1"/><circle cx="6" cy="6" r="1.7" fill="currentColor"/></svg>',
  eyeOff: '<svg width="12" height="12" viewBox="0 0 12 12"><path d="M1 6 Q6 1.5 11 6 Q6 10.5 1 6 Z M2 2 L10 10" fill="none" stroke="currentColor" stroke-width="1"/></svg>',
  caret:  '<svg width="8" height="8" viewBox="0 0 8 8"><path d="M1.5 2.5 L4 5.5 L6.5 2.5" stroke="currentColor" fill="none" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  script: '<svg width="14" height="14" viewBox="0 0 14 14" fill="none"><path d="M3 4 L6 7 L3 10 M7 10 H11" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  hud:    '<svg width="14" height="14" viewBox="0 0 14 14" fill="none"><rect x="2" y="3" width="10" height="8" rx="1" stroke="currentColor" stroke-width="1.2"/><path d="M2 6 H12" stroke="currentColor" stroke-width="1.2"/></svg>',
  tweaks: '<svg width="14" height="14" viewBox="0 0 14 14"><path d="M7 1 V13 M1 7 H13" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/></svg>',
  brand:  '<svg width="18" height="18" viewBox="0 0 18 18" fill="none"><circle cx="9" cy="9" r="2" fill="var(--accent)"/><circle cx="9" cy="9" r="6" stroke="var(--accent)" stroke-opacity="0.6" stroke-width="1" stroke-dasharray="2 3"/><path d="M9 3 V7 M9 11 V15 M3 9 H7 M11 9 H15 M4.8 4.8 L6.8 6.8 M11.2 11.2 L13.2 13.2 M13.2 4.8 L11.2 6.8 M6.8 11.2 L4.8 13.2" stroke="var(--accent)" stroke-opacity="0.55" stroke-width="1" stroke-linecap="round"/></svg>',
  kinds: {
    scene:  '<svg width="11" height="11" viewBox="0 0 11 11"><rect x="1.5" y="1.5" width="8" height="8" rx="1" fill="none" stroke="currentColor" stroke-width="1"/></svg>',
    mesh:   '<svg width="11" height="11" viewBox="0 0 11 11"><path d="M1.5 3 L5.5 1.2 L9.5 3 L5.5 5 Z M1.5 3 V7.5 L5.5 9.5 V5 Z M5.5 5 V9.5 L9.5 7.5 V3 Z" fill="none" stroke="currentColor" stroke-width="0.9" stroke-linejoin="round"/></svg>',
    light:  '<svg width="11" height="11" viewBox="0 0 11 11"><circle cx="5.5" cy="4.5" r="2.5" fill="none" stroke="currentColor" stroke-width="1"/><path d="M4 8 h3 M4.4 9.5 h2.2" stroke="currentColor" stroke-width="1" stroke-linecap="round"/></svg>',
    panel:  '<svg width="11" height="11" viewBox="0 0 11 11"><rect x="1.5" y="2" width="8" height="6" rx="0.5" fill="none" stroke="currentColor" stroke-width="1"/><path d="M1.5 4 h8" stroke="currentColor" stroke-width="1"/></svg>',
    camera: '<svg width="11" height="11" viewBox="0 0 11 11"><rect x="1.5" y="3" width="6" height="5" rx="0.5" fill="none" stroke="currentColor" stroke-width="1"/><path d="M7.5 4.5 L9.5 3.5 V7.5 L7.5 6.5 Z" fill="none" stroke="currentColor" stroke-width="1" stroke-linejoin="round"/></svg>',
    group:  '<svg width="11" height="11" viewBox="0 0 11 11"><path d="M1.5 3 H4.5 L5.5 4 H9.5 V8.5 H1.5 Z" fill="none" stroke="currentColor" stroke-width="1" stroke-linejoin="round"/></svg>',
    gltf:   '<svg width="11" height="11" viewBox="0 0 11 11"><path d="M2 2 H9 V9 H2 Z M2 5.5 H9 M5.5 2 V9" stroke="currentColor" stroke-width="0.9" fill="none"/></svg>',
  },
};
