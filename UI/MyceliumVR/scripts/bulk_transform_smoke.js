const entityCount = 1000;
const stride = 11;

let entities = [];
let transforms;
let time = 0;
let reportedPass = false;

function assert(condition, message) {
  if (!condition)
    throw new Error(message);
}

function start() {
  mycelium.log("bulk transform smoke started");

  transforms = mycelium.acquireTransformBuffer(entityCount);
  assert(transforms instanceof Float32Array, "acquireTransformBuffer should return Float32Array");
  assert(transforms.length === entityCount * stride, "transform buffer length should match layout");

  for (let i = 0; i < entityCount; ++i) {
    const entity = mycelium.spawnEntity();
    entities.push(entity);
    mycelium.setMesh(entity, "cube");
    mycelium.setMaterial(entity, i % 2 === 0 ? "accent" : "default");
  }

  assert(mycelium.entityCount() === entityCount, "bulk smoke should spawn 1,000 entities");
}

function update(deltaTime) {
  time += deltaTime;

  for (let i = 0; i < entityCount; ++i) {
    const offset = i * stride;
    const lane = i % 40;
    const row = Math.floor(i / 40);

    transforms[offset + 0] = entities[i];
    transforms[offset + 1] = (lane - 20) * 0.08;
    transforms[offset + 2] = Math.sin(time * 2 + i * 0.01) * 0.6 + (row % 20) * 0.02;
    transforms[offset + 3] = 0;
    transforms[offset + 4] = 0;
    transforms[offset + 5] = 0;
    transforms[offset + 6] = 0;
    transforms[offset + 7] = 1;
    transforms[offset + 8] = 0.35;
    transforms[offset + 9] = 0.35;
    transforms[offset + 10] = 0.35;
  }

  const applied = mycelium.commitTransformBuffer(entityCount);
  assert(applied === entityCount, "commitTransformBuffer should apply all rows");

  if (!reportedPass && time > 0.25) {
    mycelium.log("bulk transform smoke passed");
    reportedPass = true;
  }
}
