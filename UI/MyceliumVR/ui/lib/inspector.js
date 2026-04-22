'use strict';

// Requires: state.js, icons.js

function quatToEuler(qx, qy, qz, qw) {
  var sinr = 2 * (qw * qx + qy * qz);
  var cosr = 1 - 2 * (qx * qx + qy * qy);
  var rx = Math.atan2(sinr, cosr) * (180 / Math.PI);

  var sinp = 2 * (qw * qy - qz * qx);
  var ry = (Math.abs(sinp) >= 1)
    ? (sinp < 0 ? -90 : 90)
    : Math.asin(sinp) * (180 / Math.PI);

  var siny = 2 * (qw * qz + qx * qy);
  var cosy = 1 - 2 * (qy * qy + qz * qz);
  var rz = Math.atan2(siny, cosy) * (180 / Math.PI);

  return [rx, ry, rz];
}

function vec3HTML(vals, axes) {
  axes = axes || ['X', 'Y', 'Z'];
  return '<div class="vec3">' + vals.map(function (v, i) {
    return '<div class="axis ' + axes[i].toLowerCase() + '">' +
      '<span class="axis-label">' + axes[i] + '</span>' +
      '<input readonly value="' + v.toFixed(3) + '">' +
    '</div>';
  }).join('') + '</div>';
}

function sliderHTML(val) {
  return '<div class="slider">' +
    '<div class="fill" style="width:' + (val * 100) + '%"></div>' +
    '<div class="val">' + val.toFixed(2) + '</div>' +
  '</div>';
}

function sectionHTML(key, title, body, dot) {
  dot = dot !== false;
  var open = state.openSections.has(key);
  return '<div class="component-group">' +
    '<div class="component-head" data-action="section" data-key="' + key + '">' +
      '<span class="chev' + (open ? ' open' : '') + '">' + ICO.chev + '</span>' +
      '<span class="title">' + title + '</span>' +
      (dot ? '<span class="dot"></span>' : '') +
    '</div>' +
    (open ? '<div class="component-body">' + body + '</div>' : '') +
  '</div>';
}

function renderInspector(container) {
  var c = state.selectedComponents;

  // No selection or no data yet.
  if (!c) {
    container.innerHTML =
      '<div class="panel"><div class="panel-header">Inspector</div>' +
      '<div style="padding:24px;color:var(--fg-4);font-size:11px;font-family:var(--f-mono)">no entity selected</div></div>';
    return;
  }

  var xf = c.transform;
  var euler = quatToEuler(xf.qx, xf.qy, xf.qz, xf.qw);

  var entityHex = '#' + c.id.toString(16).toUpperCase().padStart(5, '0');

  // Determine alpha + cull summary for the header.
  var alphaStr = c.tags.alphaBlend ? 'blend' : c.tags.alphaClip ? 'clip' : c.tags.alphaHash ? 'hash' : 'opaque';
  var cullStr  = c.cull || 'back';

  var attached = Array.isArray(c.attachedComponents) && c.attachedComponents.length > 0
    ? c.attachedComponents.slice()
    : ['Transform'];
  var tagLabels = attached.filter(function (name) {
    return name === 'Selected' || name === 'Static' || name === 'AlphaBlend' || name === 'AlphaClip' || name === 'AlphaHash';
  });

  var transformSection = sectionHTML('transform', 'Component \u00b7 Transform',
    '<div class="prop-row"><div class="k">Position</div><div class="v">' +
      vec3HTML([xf.px, xf.py, xf.pz]) + '</div></div>' +
    '<div class="prop-row"><div class="k">Rotation</div><div class="v">' +
      vec3HTML(euler) + '</div></div>' +
    '<div class="prop-row"><div class="k">Scale</div><div class="v">' +
      vec3HTML([xf.sx, xf.sy, xf.sz]) + '</div></div>'
  );

  var meshSection = '';
  if (c.meshRenderer) {
    meshSection = sectionHTML('meshrenderer', 'Component \u00b7 MeshRenderer',
      '<div class="prop-row"><div class="k">Mesh</div><div class="v">'     + c.meshRenderer.mesh     + '</div></div>' +
      '<div class="prop-row"><div class="k">Material</div><div class="v">' + c.meshRenderer.material  + '</div></div>' +
      '<div class="prop-row"><div class="k">Normal</div><div class="v">'   +
        (c.meshRenderer.normalMap ? c.meshRenderer.normalMap : '\u2014') + '</div></div>'
    );
  }

  var panelSection = '';
  if (c.panel) {
    panelSection = sectionHTML('panel', 'Component \u00b7 Panel',
      '<div class="prop-row"><div class="k">URL</div><div class="v">' +
        (c.panel.url || '\u2014') + '</div></div>' +
      '<div class="prop-row"><div class="k">Size</div><div class="v">' +
        c.panel.width.toFixed(2) + ' \u00d7 ' + c.panel.height.toFixed(2) + ' m</div></div>'
    );
  }

  var cullSection = '';
  if (c.cull) {
    cullSection = sectionHTML('culloverride', 'Component \u00b7 CullOverride',
      '<div class="prop-row"><div class="k">Mode</div><div class="v">' + c.cull + '</div></div>'
    );
  }

  var tagsSection = tagLabels.length > 0
    ? sectionHTML('tags', 'Tag Components',
        '<div class="chip-row">' +
          tagLabels.map(function (t) { return '<div class="chip accent">' + t + '</div>'; }).join('') +
        '</div>' +
        '<div class="small dim" style="margin-top:6px;font-family:var(--f-mono);font-size:10px">' +
          'zero-size marker types \u00b7 registered via World::register_meta()' +
        '</div>',
        false)
    : '';

  container.innerHTML =
    '<div class="panel inspector">' +
      '<div class="panel-header">Inspector<span class="count">\u00b7 entt</span>' +
        '<div class="spacer"></div>' +
        '<button class="icon-btn" title="Add component">' + ICO.plus + '</button>' +
      '</div>' +
      '<div class="panel-body">' +
        '<div class="entity-header">' +
          '<div class="name-row">' +
            '<span class="name">' + c.name + '</span>' +
            '<span class="id">entity ' + entityHex + '</span>' +
          '</div>' +
          '<div class="meta">' +
            '<span><span class="dot on"></span> visible</span>' +
            '<span><span class="dot' + (c.tags.isStatic ? ' on' : '') + '"></span> static</span>' +
            '<span>alpha \u00b7 ' + alphaStr + '</span>' +
            '<span>cull \u00b7 ' + cullStr + '</span>' +
          '</div>' +
          '<div class="meta" style="margin-top:4px;font-size:9px;color:var(--fg-4)">' +
            '<span>archetype \u00b7 ' + attached.length + ' components</span>' +
          '</div>' +
          '<div class="chip-row" style="margin-top:6px">' +
            attached.map(function (t) { return '<div class="chip">' + t + '</div>'; }).join('') +
          '</div>' +
        '</div>' +
        transformSection +
        meshSection +
        panelSection +
        cullSection +
        tagsSection +
      '</div>' +
    '</div>';
}
