let threwOnce = false;
let passed = false;
let survivor = 0;

function assert(condition, message) {
  if (!condition)
    throw new Error(message);
}

function writeTransformRow(buffer, row, entity, x) {
  const stride = 11;
  const offset = row * stride;
  buffer[offset + 0] = entity;
  buffer[offset + 1] = x;
  buffer[offset + 2] = 0;
  buffer[offset + 3] = 0;
  buffer[offset + 4] = 0;
  buffer[offset + 5] = 0;
  buffer[offset + 6] = 0;
  buffer[offset + 7] = 1;
  buffer[offset + 8] = 1;
  buffer[offset + 9] = 1;
  buffer[offset + 10] = 1;
}

function start() {
  mycelium.log("bridge correctness smoke started");

  const entity = mycelium.spawnEntity();
  assert(mycelium.entityCount() === 1, "spawn should create one live entity");

  mycelium.setTransform(entity, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1);
  mycelium.setTransform(entity, 2, 0, 0, 0, 0, 0, 1, 1, 1, 1);
  assert(mycelium.dirtyTransformCount() === 1, "repeated setTransform should leave one dirty transform");

  assert(mycelium.destroyEntity(entity), "destroy should succeed for live entity");
  assert(mycelium.entityCount() === 0, "destroy should remove entity from live count");
  assert(!mycelium.destroyEntity(entity), "destroy should fail for stale entity");

  let staleHandleRejected = false;
  try {
    mycelium.setMesh(entity, "cube");
  } catch (error) {
    staleHandleRejected = true;
  }
  assert(staleHandleRejected, "stale handle mutation should throw");

  const a = mycelium.spawnEntity();
  const b = mycelium.spawnEntity();
  const c = mycelium.spawnEntity();
  survivor = a;

  const buffer = mycelium.acquireTransformBuffer(4);
  writeTransformRow(buffer, 0, a, 0.25);
  writeTransformRow(buffer, 1, 999999, 0.5);
  writeTransformRow(buffer, 2, b, 0.75);
  writeTransformRow(buffer, 3, c, 1.0);

  const applied = mycelium.commitTransformBuffer(4);
  assert(applied === 3, "bulk commit should skip invalid rows and apply valid rows");
}

function update() {
  if (!threwOnce) {
    threwOnce = true;
    throw new Error("intentional update exception");
  }

  if (!passed) {
    mycelium.setTransform(survivor, 0.5, 0.25, 0, 0, 0, 0, 1, 1, 1, 1);
    mycelium.log("bridge correctness smoke passed");
    passed = true;
  }
}
