'use strict';

// Requires: state.js, hierarchy.js (hasChildren, diffHierTree)

document.addEventListener('click', function (e) {
  // Close world menu on outside click
  if (state.worldMenuOpen && !e.target.closest('#world-picker')) {
    setState({ worldMenuOpen: false });
    return;
  }

  var el = e.target.closest('[data-action]');
  if (!el) return;

  var action = el.dataset.action;
  var value  = el.dataset.value;
  var key    = el.dataset.key;
  var id     = parseInt(el.dataset.id, 10);

  switch (action) {
    case 'mode':
      setState({ mode: value });
      break;

    case 'toggle-world-menu':
      setState({ worldMenuOpen: !state.worldMenuOpen });
      break;

    case 'set-world':
      e.stopPropagation();
      setState({ world: value, worldMenuOpen: false });
      document.title = 'mvr:switch_world:' + value;
      break;

    case 'select':
      var selId = parseInt(el.dataset.id, 10);
      setState({ selectedId: selId });
      document.title = 'mvr:select:' + selId;
      break;

    case 'expand':
      e.stopPropagation();
      if (!hasChildren(id)) break;
      {
        var exp = new Set(state.expandedIds);
        if (exp.has(id)) exp.delete(id); else exp.add(id);
        setState({ expandedIds: exp });
      }
      break;

    case 'vis':
      e.stopPropagation();
      {
        var vis = Object.assign({}, state.visibility);
        vis[id] = vis[id] !== false ? false : true;
        setState({ visibility: vis });
      }
      break;

    case 'section':
      {
        var secs = new Set(state.openSections);
        if (secs.has(key)) secs.delete(key); else secs.add(key);
        setState({ openSections: secs });
      }
      break;

    case 'ctab':
      setState({ consoleTab: el.dataset.tab });
      break;

    case 'ltab':
      setState({ leftTab: el.dataset.tab });
      break;

    case 'toggle-tweaks':
      setState({ tweaksOpen: !state.tweaksOpen });
      break;

    case 'tweak':
      setState({ tweaks: Object.assign({}, state.tweaks, { [key]: value }) });
      break;

    case 'tweak-bool':
      setState({ tweaks: Object.assign({}, state.tweaks, { [key]: !state.tweaks[key] }) });
      break;

    case 'toggle-focus':
      setState({ focused: !state.focused });
      break;
  }
});

window.addEventListener('keydown', function (e) {
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
  if (e.key === 'F2')  { e.preventDefault(); setState({ focused: !state.focused }); }
  if (e.key === 'Tab') { e.preventDefault(); setState({ mode: state.mode === 'edit' ? 'play' : 'edit' }); }
});
