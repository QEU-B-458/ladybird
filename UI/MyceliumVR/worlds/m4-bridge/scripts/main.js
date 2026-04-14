// Milestone 4 — Fast ECS Bridge
// Shows: acquireTransformBuffer / commitTransformBuffer bulk path,
//        BridgeRegistry API introspection, 1000-entity wave with stable frame time.
//
// What you should see:
//   - 1000 small cubes arranged in a 25×40 grid, animating as a sine wave.
//   - All 1000 transforms updated every frame via one bulk buffer call — no per-entity overhead.
//   - Console logs frame time and entity count every second. Should stay well under 16ms.
//   - At t=3s the full mycelium API list is printed to console (BridgeRegistry introspection).

const COLS    = 25;
const ROWS    = 40;
const TOTAL   = COLS * ROWS;   // 1000
const STRIDE  = 11;            // [entityId, px, py, pz, qx, qy, qz, qw, sx, sy, sz]
const SPACING = 0.38;
const SCALE   = 0.28;

let entities  = [];
let transforms;
let time      = 0;
let lastLog   = 0;
let apiPrinted = false;

function start() {
    mycelium.log(`m4-bridge: spawning ${TOTAL} entities...`);

    transforms = mycelium.acquireTransformBuffer(TOTAL);

    for (let i = 0; i < TOTAL; i++) {
        const e = mycelium.spawnEntity();
        mycelium.setMesh(e, "cube");
        mycelium.setMaterial(e, i % 3 === 0 ? "accent" : (i % 3 === 1 ? "example-accent" : "default"));
        entities.push(e);
    }

    mycelium.log(`m4-bridge: ${mycelium.entityCount()} entities ready`);
    mycelium.log(`m4-bridge: transform buffer length = ${transforms.length} (expected ${TOTAL * STRIDE})`);
}

function update(deltaTime) {
    time += deltaTime;

    const t0 = Date.now();

    // Fill the entire buffer in one JS loop — no individual setTransform calls.
    for (let i = 0; i < TOTAL; i++) {
        const col = i % COLS;
        const row = Math.floor(i / COLS);
        const x   = (col - COLS * 0.5) * SPACING;
        const z   = -4.0 - row * SPACING;
        const y   = Math.sin(time * 2.0 + col * 0.4 + row * 0.25) * 0.5;

        const offset = i * STRIDE;
        transforms[offset + 0]  = entities[i];   // entityId
        transforms[offset + 1]  = x;
        transforms[offset + 2]  = y;
        transforms[offset + 3]  = z;
        transforms[offset + 4]  = 0;             // qx
        transforms[offset + 5]  = 0;             // qy
        transforms[offset + 6]  = 0;             // qz
        transforms[offset + 7]  = 1;             // qw
        transforms[offset + 8]  = SCALE;
        transforms[offset + 9]  = SCALE;
        transforms[offset + 10] = SCALE;
    }

    const applied = mycelium.commitTransformBuffer(TOTAL);
    const elapsed = Date.now() - t0;

    // Log stats every second.
    if (time - lastLog >= 1.0) {
        mycelium.log(`m4-bridge: t=${time.toFixed(1)}s  bulk_ms=${elapsed}  applied=${applied}  entities=${mycelium.entityCount()}`);
        lastLog = time;
    }

    // Print full API list once at t=3s.
    if (!apiPrinted && time >= 3.0) {
        apiPrinted = true;
        const fns = mycelium.api.functions();
        mycelium.log(`m4-bridge: BridgeRegistry exposes ${fns.length} functions:`);
        mycelium.log("  " + fns.join(", "));
    }
}
