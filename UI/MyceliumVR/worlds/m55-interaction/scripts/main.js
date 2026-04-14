// Milestone 5.5 — Desktop Interaction
// Shows: free-fly camera, entity selection cycling (Tab), transform nudging (IJKL/UO),
//        HUD overlay, F1/F2 overlay gating.
//
// Controls (from controls.js — always active):
//   WASD          — fly camera
//   Mouse         — look
//   Q / E         — move camera down / up
//   LShift        — fast move
//   Tab           — cycle selected entity (logged to console)
//   I/K           — move selected up / down
//   J/L           — move selected left / right
//   U/O           — move selected forward / backward
//   F1            — focus overlay HUD
//   F2            — return to world
//
// What you should see:
//   - 7 cubes of different sizes and positions arranged like a small obstacle course.
//   - Fly through them with WASD + mouse.
//   - Tab to cycle selection — selected entity ID is logged.
//   - Nudge the selected cube with IJKL. Its position updates live.
//   - HUD shows FPS, frame time, entity count, and camera position.

function start() {
    mycelium.log("m55-interaction: start");
    mycelium.log("  Tab      = cycle selection");
    mycelium.log("  IJKL/UO  = nudge selected entity");
    mycelium.log("  WASD     = fly  |  mouse = look  |  F1/F2 = HUD focus");

    const layout = [
        // [x,    y,    z,    sx,  sy,  sz,  material]
        [  0.0,  0.0, -4.0,  1.0, 1.0, 1.0, "accent"        ],
        [ -2.5,  0.5, -5.5,  0.7, 0.7, 0.7, "example-accent"],
        [  2.5, -0.3, -5.0,  1.2, 0.5, 1.2, "default"       ],
        [  0.0,  1.5, -6.5,  0.5, 1.8, 0.5, "accent"        ],
        [ -1.0, -0.5, -3.0,  0.6, 0.6, 0.6, "example-accent"],
        [  1.2,  0.8, -7.0,  1.5, 1.5, 0.3, "default"       ],
        [ -3.5,  0.0, -7.5,  0.9, 0.9, 0.9, "accent"        ],
    ];

    for (const [x, y, z, sx, sy, sz, mat] of layout) {
        const e = mycelium.spawnEntity();
        mycelium.setMesh(e, "cube");
        mycelium.setMaterial(e, mat);
        mycelium.setTransform(e, x, y, z, 0, 0, 0, 1, sx, sy, sz);
    }

    mycelium.setDirectionalLight(0.4,  0.8, -0.3, 1.0, 0.9, 0.8, 0.8);
    mycelium.setAmbientLight(0.2, 0.25, 0.4, 0.3);
    // Warm orange key point light above and to the left.
    mycelium.setPointLight(0,  -2.0,  2.5, -5.0,   1.0, 0.55, 0.1,  3.0, 8.0);
    // Cool blue fill point light to the right and behind.
    mycelium.setPointLight(1,   3.0,  0.5, -8.0,   0.2, 0.5,  1.0,  2.5, 9.0);
    // White spotlight from above, aimed straight down at the centre cube.
    // setSpotLight(index, x,y,z, dirX,dirY,dirZ, innerDeg,outerDeg, r,g,b, intensity, radius)
    mycelium.setSpotLight(2,  0.0, 5.0, -4.0,  0.0,-1.0, 0.0,  15, 30,  1.0,0.95,0.85,  5.0, 12.0);

    mycelium.log(`m55-interaction: ${mycelium.entityCount()} entities — Tab to select`);
}

function update(_deltaTime) {
    // All interaction is handled by controls.js (loaded separately as a control script).
    // Nothing needed here.
}
