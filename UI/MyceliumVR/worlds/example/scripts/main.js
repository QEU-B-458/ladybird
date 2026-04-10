let entity;
let time = 0;

function start() {
  mycelium.log("example world booted from world://example/");
  const manifest = mycelium.fs.readText("world://example/world.json");
  mycelium.log(`example manifest bytes: ${manifest.length}`);
  mycelium.fs.writeText("state://example/session-note.txt", "host editable state is writable\n");
  mycelium.log(`state note exists: ${mycelium.fs.exists("state://example/session-note.txt")}`);
  try {
    mycelium.fs.writeText("world://example/should-not-write.txt", "nope");
    mycelium.log("unexpected package write succeeded");
  } catch (error) {
    mycelium.log(`package write denied: ${error}`);
  }
  entity = mycelium.spawnEntity();
  mycelium.setMesh(entity, "world://example/assets/cube.glb");
  mycelium.setMaterial(entity, "example-accent");
}

function update(deltaTime) {
  time += deltaTime;
  const halfTurn = time * 0.5;
  mycelium.setTransform(
    entity,
    Math.sin(time) * 1.5, Math.cos(time * 0.7) * 0.7, Math.sin(time * 0.4) * 0.8,
    0, Math.sin(halfTurn), 0, Math.cos(halfTurn),
    1.4, 1.4, 1.4
  );
}
