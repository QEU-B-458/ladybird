let entity;
let panelEntity;
let time = 0;

function start() {
    mycelium.log("default script started");
    entity = mycelium.spawnEntity();
    mycelium.setMesh(entity, "cube");
    mycelium.setMaterial(entity, "accent");
    mycelium.setTransform(
        entity,
        -1.8, 0.0, -1.5,
        0, 0, 0, 1,
        1, 1, 1
    );

    panelEntity = mycelium.spawnEntity();
    mycelium.createPanel(panelEntity, "about:newtab", 1.8, 1.1);
    mycelium.setTransform(
        panelEntity,
        2.1, 0.4, -0.5,
        0, 0, 0, 1,
        1, 1, 1
    );
}

function update(deltaTime) {
    time += deltaTime;
    mycelium.setTransform(
        entity,
        -1.8 + Math.sin(time) * 0.8, Math.cos(time * 0.7) * 0.5, -1.5,
        0, 0, 0, 1,
        1, 1, 1
    );
}
