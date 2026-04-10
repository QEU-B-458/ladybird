let entity = 0;
let time = 0;
let invalidHandleRaised = false;
let destroyed = false;

function assert(condition, message) {
  if (!condition)
    throw new Error(message);
}

function start() {
  mycelium.log("bridge smoke started");

  assert(mycelium.api.hasFunction("setTransform"), "api registry should expose setTransform");
  assert(mycelium.api.functions().includes("spawnEntity"), "api registry should list spawnEntity");
  mycelium.log(mycelium.api.describeFunction("setTransform"));

  assert(mycelium.entityCount() === 0, "world should start empty");

  entity = mycelium.spawnEntity();
  assert(mycelium.entityCount() === 1, "spawnEntity should create one entity");

  mycelium.setMesh(entity, "cube");
  mycelium.setMaterial(entity, "accent");
  mycelium.setTransform(entity, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1);

  try {
    mycelium.setTransform(999999, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1);
  } catch (error) {
    invalidHandleRaised = true;
    mycelium.log("invalid handle rejected: " + error);
  }

  assert(invalidHandleRaised, "invalid EntityId should throw");
}

function update(deltaTime) {
  time += deltaTime;

  if (!destroyed) {
    mycelium.setTransform(entity, Math.sin(time) * 1.5, Math.cos(time) * 0.75, 0, 0, 0, 0, 1, 1, 1, 1);
  }

  if (!destroyed && time > 1.0) {
    assert(mycelium.destroyEntity(entity), "destroyEntity should return true for live entity");
    assert(mycelium.entityCount() === 0, "destroyEntity should remove live entity count");
    assert(!mycelium.destroyEntity(entity), "destroyEntity should return false for stale entity");
    destroyed = true;
    mycelium.log("bridge smoke passed");
  }
}
