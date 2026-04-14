// Milestone 1 — Engine Skeleton
// Shows: entity spawn, setMesh, setTransform, update loop, destroy.
//
// What you should see:
//   - 5 cubes arranged in a row, each floating at different heights.
//   - Every second the entity count is logged.
//   - After 8 seconds the middle cube is destroyed — it disappears from the scene.

const ENTITY_COUNT = 5;
let entities = [];
let time = 0;
let lastLogTime = 0;
let destroyed = false;

function start() {
    mycelium.log("m1-engine: start");

    for (let i = 0; i < ENTITY_COUNT; i++) {
        const e = mycelium.spawnEntity();
        mycelium.setMesh(e, "cube");
        mycelium.setMaterial(e, i % 2 === 0 ? "accent" : "default");
        mycelium.setTransform(
            e,
            (i - 2) * 1.4, 0.0, -3.0,
            0, 0, 0, 1,
            0.7, 0.7, 0.7
        );
        entities.push(e);
    }

    mycelium.log(`m1-engine: spawned ${mycelium.entityCount()} entities`);
}

function update(deltaTime) {
    time += deltaTime;

    // Bob each cube at a different frequency.
    for (let i = 0; i < entities.length; i++) {
        if (entities[i] === null) continue;
        const freq = 0.8 + i * 0.15;
        mycelium.setTransform(
            entities[i],
            (i - 2) * 1.4,
            Math.sin(time * freq) * 0.4,
            -3.0,
            0, 0, 0, 1,
            0.7, 0.7, 0.7
        );
    }

    // Log entity count every second.
    if (time - lastLogTime >= 1.0) {
        mycelium.log(`m1-engine: t=${time.toFixed(1)}s  entities=${mycelium.entityCount()}`);
        lastLogTime = time;
    }

    // Destroy the middle cube after 8 seconds.
    if (!destroyed && time >= 8.0) {
        const mid = entities[2];
        const ok = mycelium.destroyEntity(mid);
        entities[2] = null;
        destroyed = true;
        mycelium.log(`m1-engine: destroyed entity ${mid} → ok=${ok}  remaining=${mycelium.entityCount()}`);
    }
}
