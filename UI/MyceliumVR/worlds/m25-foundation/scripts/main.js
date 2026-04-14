// Milestone 2.5 — Renderer Foundation
// Shows: glTF mesh loading, smooth normals, albedo textures, GPU instancing.
//
// Assets (symlinked from glTF-Sample-Assets):
//   assets/Duck.glb          — single hero model with smooth normals
//   assets/DuckCM.png        — albedo texture for the duck
//   assets/BoxTextured.glb   — textured box geometry
//   assets/CesiumLogoFlat.png — Cesium logo texture
//   assets/Avocado.glb       — instancing target (200 copies, one draw call group)
//
// What you should see:
//   - A large duck in the centre with correct smooth-curved normals and yellow texture.
//   - A textured box to the left showing the Cesium logo mapped cleanly onto faces.
//   - A 10×20 grid of avocados (200 instances) behind. Despite 200 entities they share
//     one mesh group — check the console: only one vkCmdDraw for the avocado group.
//   - Fly far back (hold S) — reversed-Z means nothing clips at distance.

const GRID_COLS = 10;
const GRID_ROWS = 20;
const GRID_SPACING = 0.55;

let time = 0;
let duck = null;

function start() {
    mycelium.log("m25-foundation: start");

    // Hero duck — glTF mesh, smooth normals, texture.
    duck = mycelium.spawnEntity();
    mycelium.setMesh(duck, "world://m25-foundation/assets/Duck.glb");
    mycelium.setMaterial(duck, "world://m25-foundation/assets/DuckCM.png");
    mycelium.setTransform(
        duck,
        0.0, -0.5, -3.0,
        0, 0, 0, 1,
        0.012, 0.012, 0.012  // Duck.glb is large in glTF units
    );

    // Textured box — Cesium logo texture.
    const box = mycelium.spawnEntity();
    mycelium.setMesh(box, "world://m25-foundation/assets/BoxTextured.glb");
    mycelium.setMaterial(box, "world://m25-foundation/assets/CesiumLogoFlat.png");
    mycelium.setTransform(
        box,
        -2.2, 0.0, -3.0,
        0, 0, 0, 1,
        1.0, 1.0, 1.0
    );

    // 200 avocados in a grid — all same mesh+material → one draw group, one GPU draw call.
    for (let row = 0; row < GRID_ROWS; row++) {
        for (let col = 0; col < GRID_COLS; col++) {
            const e = mycelium.spawnEntity();
            mycelium.setMesh(e, "world://m25-foundation/assets/Avocado.glb");
            // No setMaterial — the glTF material ("2256_Avocado_d") is auto-resolved
            // from the mesh path via the MeshLibrary alias registered at load time.
            mycelium.setTransform(
                e,
                (col - GRID_COLS * 0.5) * GRID_SPACING,
                -0.5,
                -6.0 - row * GRID_SPACING,
                0, 0, 0, 1,
                20.0, 20.0, 20.0  // Avocado.glb is tiny in glTF units
            );
        }
    }

    mycelium.setDirectionalLight(0.5, -0.9, -0.3, 1.0, 0.95, 0.85, 1.1);
    mycelium.setAmbientLight(0.4, 0.5, 0.7, 0.25);

    mycelium.log(`m25-foundation: ${mycelium.entityCount()} entities (1 duck + 1 box + 200 avocados)`);
    mycelium.log("m25-foundation: avocados share one mesh+material group → 1 draw call for 200 instances");
}

function update(deltaTime) {
    time += deltaTime;

    // Slowly rotate the duck.
    const a = time * 0.4;
    mycelium.setTransform(
        duck,
        0.0, -0.5 + Math.sin(time * 0.5) * 0.1, -3.0,
        0, Math.sin(a * 0.5), 0, Math.cos(a * 0.5),
        0.012, 0.012, 0.012
    );
}
