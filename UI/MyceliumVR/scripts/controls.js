let camera = {
    px: 0,
    py: 0,
    pz: 6,
    yaw: 0,
    pitch: 0,
};

let selectedIndex = 0;
let selectedEntity = null;
let loggedInputSeen = false;
let loggedCameraMotion = false;

function refreshSelectedEntity() {
    const count = mycelium.entityCount();
    if (count === 0) {
        selectedIndex = 0;
        selectedEntity = null;
        return;
    }

    if (selectedIndex >= count)
        selectedIndex = 0;
    selectedEntity = mycelium.entityIdAt(selectedIndex);
}

function moveSelectedEntity(dx, dy, dz) {
    if (selectedEntity === null)
        return;

    const transform = mycelium.getTransform(selectedEntity);
    if (!transform)
        return;

    mycelium.setTransform(
        selectedEntity,
        transform.px + dx, transform.py + dy, transform.pz + dz,
        transform.qx, transform.qy, transform.qz, transform.qw,
        transform.sx, transform.sy, transform.sz
    );
}

function controlStart() {
    const existingCamera = mycelium.getCamera();
    if (existingCamera) {
        camera = existingCamera;
    }
    refreshSelectedEntity();
    mycelium.log("controls script started");
}

function controlUpdate(deltaTime) {
    const hadInput =
        mycelium.input.mouseDeltaX() !== 0 ||
        mycelium.input.mouseDeltaY() !== 0 ||
        mycelium.input.isKeyDown("W") ||
        mycelium.input.isKeyDown("A") ||
        mycelium.input.isKeyDown("S") ||
        mycelium.input.isKeyDown("D") ||
        mycelium.input.isKeyDown("Q") ||
        mycelium.input.isKeyDown("E");
    if (hadInput && !loggedInputSeen) {
        loggedInputSeen = true;
        mycelium.log("controls: first movement input reached controlUpdate");
    }

    const moveSpeed = mycelium.input.isKeyDown("LShift") ? 6.0 : 3.0;
    const editSpeed = 2.0 * deltaTime;
    const yawStep = 90.0 * deltaTime;
    const pitchStep = 90.0 * deltaTime;

    if (mycelium.input.consumeKeyPress("Tab")) {
        const count = mycelium.entityCount();
        if (count > 0) {
            selectedIndex = (selectedIndex + 1) % count;
            refreshSelectedEntity();
            mycelium.log(`selected entity: ${selectedEntity}`);
        }
    }

    const before = { px: camera.px, py: camera.py, pz: camera.pz, yaw: camera.yaw, pitch: camera.pitch };

    camera.yaw += mycelium.input.mouseDeltaX() * 0.12;
    camera.pitch -= mycelium.input.mouseDeltaY() * 0.12;

    if (mycelium.input.isKeyDown("Left"))
        camera.yaw -= yawStep;
    if (mycelium.input.isKeyDown("Right"))
        camera.yaw += yawStep;
    if (mycelium.input.isKeyDown("Up"))
        camera.pitch += pitchStep;
    if (mycelium.input.isKeyDown("Down"))
        camera.pitch -= pitchStep;

    if (camera.pitch > 89.0)
        camera.pitch = 89.0;
    if (camera.pitch < -89.0)
        camera.pitch = -89.0;

    const yawRadians = camera.yaw * Math.PI / 180.0;
    const forwardX = Math.sin(yawRadians);
    const forwardZ = -Math.cos(yawRadians);
    const rightX = Math.cos(yawRadians);
    const rightZ = Math.sin(yawRadians);
    const frameMove = moveSpeed * deltaTime;

    if (mycelium.input.isKeyDown("W")) {
        camera.px += forwardX * frameMove;
        camera.pz += forwardZ * frameMove;
    }
    if (mycelium.input.isKeyDown("S")) {
        camera.px -= forwardX * frameMove;
        camera.pz -= forwardZ * frameMove;
    }
    if (mycelium.input.isKeyDown("A")) {
        camera.px -= rightX * frameMove;
        camera.pz -= rightZ * frameMove;
    }
    if (mycelium.input.isKeyDown("D")) {
        camera.px += rightX * frameMove;
        camera.pz += rightZ * frameMove;
    }
    if (mycelium.input.isKeyDown("Q"))
        camera.py -= frameMove;
    if (mycelium.input.isKeyDown("E"))
        camera.py += frameMove;

    const moved =
        camera.px !== before.px ||
        camera.py !== before.py ||
        camera.pz !== before.pz ||
        camera.yaw !== before.yaw ||
        camera.pitch !== before.pitch;
    if (moved && !loggedCameraMotion) {
        loggedCameraMotion = true;
        mycelium.log(`controls: first camera motion -> pos=(${camera.px.toFixed(2)}, ${camera.py.toFixed(2)}, ${camera.pz.toFixed(2)}) yaw=${camera.yaw.toFixed(2)} pitch=${camera.pitch.toFixed(2)}`);
    }

    mycelium.setCamera(camera.px, camera.py, camera.pz, camera.yaw, camera.pitch);

    if (mycelium.input.isKeyDown("J"))
        moveSelectedEntity(-editSpeed, 0, 0);
    if (mycelium.input.isKeyDown("L"))
        moveSelectedEntity(editSpeed, 0, 0);
    if (mycelium.input.isKeyDown("I"))
        moveSelectedEntity(0, editSpeed, 0);
    if (mycelium.input.isKeyDown("K"))
        moveSelectedEntity(0, -editSpeed, 0);
    if (mycelium.input.isKeyDown("U"))
        moveSelectedEntity(0, 0, -editSpeed);
    if (mycelium.input.isKeyDown("O"))
        moveSelectedEntity(0, 0, editSpeed);
}
