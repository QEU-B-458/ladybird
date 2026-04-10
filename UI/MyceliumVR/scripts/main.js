let entity;
let time = 0;

function start() {
    mycelium.log("default script started");
    entity = mycelium.spawnEntity();
    mycelium.setMesh(entity, "cube");
    mycelium.setMaterial(entity, "accent");
}

function update(deltaTime) {
    time += deltaTime;
    mycelium.setTransform(
        entity,
        Math.sin(time) * 1.5, Math.cos(time * 0.7) * 0.7, 0,
        0, 0, 0, 1,
        1, 1, 1
    );
}
