'use strict';

// Static mock data — replaced at runtime by engine bridge calls.

var SPONZA_HIERARCHY = [
  { id: 1,  name: 'world_root',     kind: 'scene',  expanded: true, depth: 0 },
  { id: 2,  name: 'sponza.gltf',    kind: 'gltf',   expanded: true, depth: 1, parent: 1 },
  { id: 3,  name: 'walls_ground',   kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0098', mat: 'floor_01',       tris: 2448,  alpha: 'opaque', static: true },
  { id: 4,  name: 'walls_upper',    kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0023', mat: 'stone_wall_01',  tris: 9124,  alpha: 'opaque', static: true },
  { id: 5,  name: 'arches_ground',  kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0044', mat: 'arch_stone',     tris: 15102, alpha: 'opaque', static: true },
  { id: 6,  name: 'arches_upper',   kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0045', mat: 'arch_stone',     tris: 14208, alpha: 'opaque', static: true },
  { id: 7,  name: 'columns_round',  kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0017', mat: 'marble_col',     tris: 28944, alpha: 'opaque', static: true },
  { id: 8,  name: 'columns_square', kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0018', mat: 'marble_sq',      tris: 22110, alpha: 'opaque', static: true },
  { id: 9,  name: 'ceiling',        kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0009', mat: 'ceiling_carved', tris: 18336, alpha: 'opaque', static: true },
  { id: 10, name: 'curtains_red',   kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0051', mat: 'fabric_red',     tris: 5420,  alpha: 'blend',  static: true },
  { id: 11, name: 'curtains_blue',  kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0052', mat: 'fabric_blue',    tris: 5420,  alpha: 'blend',  static: true },
  { id: 12, name: 'curtains_green', kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0053', mat: 'fabric_green',   tris: 5420,  alpha: 'blend',  static: true },
  { id: 13, name: 'foliage_vines',  kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0072', mat: 'vine_leaf',      tris: 6480,  alpha: 'clip',   static: true },
  { id: 14, name: 'chain',          kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0064', mat: 'chain_iron',     tris: 1802,  alpha: 'hash',   static: true },
  { id: 15, name: 'lion_head',      kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0088', mat: 'stone_relief',   tris: 12408, alpha: 'opaque', static: true },
  { id: 16, name: 'vase_round',     kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0077', mat: 'vase_ceramic',   tris: 3210,  alpha: 'opaque', static: true },
  { id: 17, name: 'floor_plants',   kind: 'mesh',   depth: 2, parent: 2, mesh: 'Mesh_0081', mat: 'plant_leaf',     tris: 4892,  alpha: 'clip',   static: true },
  { id: 30, name: 'lighting',       kind: 'group',  expanded: true, depth: 1, parent: 1 },
  { id: 31, name: 'sun_directional',kind: 'light',  depth: 2, parent: 30, meta: 'dir · shadows' },
  { id: 32, name: 'point_lanterns', kind: 'light',  depth: 2, parent: 30, meta: '12 · cluster' },
  { id: 40, name: 'panels',         kind: 'group',  expanded: true, depth: 1, parent: 1 },
  { id: 41, name: 'docs_panel',     kind: 'panel',  depth: 2, parent: 40, meta: 'docs.ladybird.dev' },
  { id: 42, name: 'scripting_repl', kind: 'panel',  depth: 2, parent: 40, meta: 'world://repl.html' },
  { id: 50, name: 'camera_main',    kind: 'camera', depth: 1, parent: 1 },
];

var LION_HEAD = {
  id: 15, entityId: '#00F2A1', name: 'lion_head',
  static: true, alpha: 'opaque', cull: 'back', visible: true,
  components: {
    transform:    { position: [-6.82, 3.14, 0.00], rotation: [0.00, 90.0, 0.00], scale: [1.00, 1.00, 1.00] },
    meshRenderer: { mesh: 'Mesh_0088', source: 'world://sponza.gltf', vertices: 6204, triangles: 12408, normalMap: true },
    material:     { name: 'stone_relief', albedo: 'sponza_relief_albedo.ktx2', normal: 'sponza_relief_nrm.ktx2', mr: 'sponza_relief_mr.ktx2', alpha: 'opaque', metallic: 0.05, roughness: 0.68, ior: 1.50 },
    cullOverride: { mode: 'back', source: 'material' },
    tags: ['Static', 'ShadowCaster'],
  },
};

var RENDER_PASSES = [
  { id: 'upl',  name: 'GeometryUploads',        ms: 0.08 },
  { id: 'sh',   name: 'ShadowPass',             ms: 1.24 },
  { id: 'cull', name: 'ComputeCullPass',        ms: 0.21 },
  { id: 'cla',  name: 'ClusterLightAssignPass', ms: 0.34 },
  { id: 'dz',   name: 'DepthPrepass',           ms: 0.92 },
  { id: 'geo',  name: 'GeometryPass',           ms: 4.15 },
  { id: 'ui',   name: 'UIPanelPass',            ms: 0.28 },
  { id: 'bth',  name: 'BloomThreshold',         ms: 0.19 },
  { id: 'bbh',  name: 'BloomBlurH',             ms: 0.24 },
  { id: 'bbv',  name: 'BloomBlurV',             ms: 0.24 },
  { id: 'bco',  name: 'BloomComposite',         ms: 0.11 },
  { id: 'prc',  name: 'PresentComposite',       ms: 0.07 },
];

var INITIAL_LOGS = [
  { ts: '00:00.012', lvl: 'INFO', src: 'Engine',        msg: "Session opened; mounting world 'm255-sponza'", k: 'info' },
  { ts: '00:00.041', lvl: 'INFO', src: 'VFS',           msg: 'Mounted world:// → worlds/m255-sponza', k: 'info' },
  { ts: '00:00.088', lvl: 'OK',   src: 'ScriptRuntime', msg: 'LibJS realm ready · bridge functions registered (37)', k: 'ok' },
  { ts: '00:00.112', lvl: 'INFO', src: 'Vulkan',        msg: 'Selected device: NVIDIA RTX 4070 · queue families [gfx=0 xfer=1 cmp=0]', k: 'info' },
  { ts: '00:00.264', lvl: 'INFO', src: 'glTF',          msg: 'Loaded sponza.gltf · 103 meshes · 87 materials · 4.2M tris', k: 'info' },
  { ts: '00:00.318', lvl: 'INFO', src: 'PBRManager',    msg: 'Bindless texture array: 242 resident (of 4096)', k: 'info' },
  { ts: '00:00.402', lvl: 'JS',   src: 'main.js',       msg: "mycelium.spawnScene('sponza.gltf') → 15 roots", k: 'js' },
  { ts: '00:00.410', lvl: 'WARN', src: 'GeometryPass',  msg: 'fabric_blue pipeline fallback · NORMAL_MAP variant missing', k: 'warn' },
  { ts: '00:00.511', lvl: 'OK',   src: 'RenderGraph',   msg: 'Graph compiled · 12 passes · 38 barriers', k: 'ok' },
  { ts: '00:00.512', lvl: 'INFO', src: 'ClusterLight',  msg: 'Grid 16×9×24 = 3456 clusters · 12 point lights assigned', k: 'info' },
];

var WORLDS = [
  { id: 'm255-sponza',     hint: 'classic test scene' },
  { id: 'm255-scene',      hint: 'general' },
  { id: 'm2-renderer',     hint: 'render-graph dev' },
  { id: 'm55-interaction', hint: 'panels + input' },
];

var ACCENTS = [
  { id: 'cyan',    name: 'Cyan',    hue: 175 },
  { id: 'amber',   name: 'Amber',   hue: 75  },
  { id: 'magenta', name: 'Magenta', hue: 330 },
  { id: 'lime',    name: 'Lime',    hue: 135 },
  { id: 'blue',    name: 'Azure',   hue: 240 },
];

var BRIDGE_FNS = [
  'mycelium.spawnEntity', 'mycelium.destroyEntity', 'mycelium.setTransform',
  'mycelium.setMesh', 'mycelium.setMaterial', 'mycelium.spawnScene',
  'mycelium.setCullOverride', 'mycelium.setPanel', 'mycelium.setCamera',
  'mycelium.setSunlight', 'mycelium.fs.readText', 'mycelium.fs.writeText',
  'mycelium.input.pointer', 'mycelium.input.keys', 'mycelium.log',
];

var DEFAULT_TWEAKS = {
  accent: 'cyan', density: 'cozy', layout: 'classic', tone: 'neutral',
  hudPerf: true, hudCamera: false, hudSession: true, reticle: true,
};
