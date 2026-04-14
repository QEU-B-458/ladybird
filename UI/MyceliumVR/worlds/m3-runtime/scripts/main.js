// Milestone 3 — Same-Process LibJS Runtime
// Shows: start/update lifecycle, JS exceptions surfaced without crashing,
//        frame counter, multiple independent behaviours in one script.
//
// What you should see:
//   - A spinning cube in the centre.
//   - Console logs a frame count every second.
//   - At t=5s a deliberate TypeError is thrown — check console for the error.
//     The cube keeps spinning. The app does NOT crash.
//   - At t=10s a second cube spawns, proving start()-style setup can happen at
//     any time via normal JS logic.

let spinner = null;
let second  = null;
let time    = 0;
let lastSecond = 0;
let frame   = 0;
let exceptionFired = false;
let secondSpawned  = false;

function start() {
    mycelium.log("m3-runtime: start — JS runtime is alive");

    spinner = mycelium.spawnEntity();
    mycelium.setMesh(spinner, "cube");
    mycelium.setMaterial(spinner, "accent");
    mycelium.setTransform(spinner, 0, 0, -3, 0, 0, 0, 1, 1, 1, 1);

    mycelium.log("m3-runtime: bridge is reachable from start()");
}

function update(deltaTime) {
    time  += deltaTime;
    frame += 1;

    // Spin the cube.
    const a = time * 1.2;
    mycelium.setTransform(
        spinner,
        0, 0, -3,
        0, Math.sin(a * 0.5), 0, Math.cos(a * 0.5),
        1, 1, 1
    );

    // Log once per second.
    if (time - lastSecond >= 1.0) {
        mycelium.log(`m3-runtime: t=${time.toFixed(1)}s  frames=${frame}`);
        lastSecond = time;
    }

    // At 5s throw a deliberate exception. The runtime should catch it, print it,
    // and continue calling update() next frame.
    if (!exceptionFired && time >= 5.0) {
        exceptionFired = true;
        mycelium.log("m3-runtime: about to throw a deliberate TypeError...");
        null.thisPropertyDoesNotExist; // TypeError: Cannot read properties of null
    }

    // At 10s spawn a second cube — proves the world is still alive post-exception.
    if (!secondSpawned && time >= 10.0) {
        secondSpawned = true;
        second = mycelium.spawnEntity();
        mycelium.setMesh(second, "cube");
        mycelium.setMaterial(second, "example-accent");
        mycelium.setTransform(second, 1.5, 0, -3, 0, 0, 0, 1, 0.8, 0.8, 0.8);
        mycelium.log("m3-runtime: second cube spawned post-exception — runtime survived");
    }
}
