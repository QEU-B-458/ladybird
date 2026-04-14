// Milestone 2 — Vulkan Renderer / PBR Materials
// Shows: Cook-Torrance PBR, metallic/roughness, normal map, AO, emissive, IBL ambient.
//
// Asset: DamagedHelmet.glb (glTF Sample Assets, CC BY 4.0)
//   Full PBR maps: albedo, metalRoughness (G=rough B=metal), normal, AO, emissive.
//
// What you should see:
//   - A damaged sci-fi helmet floating in the centre, slowly rotating.
//   - Metal parts reflect specular highlights that shift as the helmet turns.
//   - Damaged/scratched sections show roughness variation — dull vs shiny patches.
//   - Emissive lens glow even when facing away from the key light.
//   - Normal map shows surface dents/damage not in the geometry.
//   - After 5s the sun slowly orbits — watch specular highlights move.

let helmetEntity = null;
let time = 0;

// Base orientation: -90° around X so the helmet face points toward the camera.
// h = sin/cos of -45° = ±1/√2
const H = Math.SQRT1_2; // ≈ 0.7071

function start() {
    mycelium.log("m2-renderer: start");

    helmetEntity = mycelium.spawnEntity();
    mycelium.setMesh(helmetEntity, "world://m2-renderer/assets/DamagedHelmet.glb");
    // No setMaterial — PBR textures auto-resolved from the GLB's embedded material.
    // Base pose: +90° around X (face forward, right-side up), no Y spin yet.
    mycelium.setTransform(
        helmetEntity,
        0.0, 0.0, -3.0,
        H, 0, 0, H,
        1.0, 1.0, 1.0
    );

    // Warm key light from upper-right.
    mycelium.setDirectionalLight(0.6, -0.8, -0.4,  1.0, 0.9, 0.75,  2.5);
    // Cool fill from the left to keep shadowed side readable.
    mycelium.setAmbientLight(0.15, 0.2, 0.35, 0.4);
    // Warm rim point light behind/above to show emissive and specular edges.
    mycelium.setPointLight(0,  0.0, 1.2, -1.0,  1.0, 0.7, 0.3,  3.0, 8.0);

    mycelium.log("m2-renderer: helmet spawned, " + mycelium.entityCount() + " entities");
}

function update(deltaTime) {
    time += deltaTime;

    // Slowly spin around world Y, composed with the base -90° X tilt.
    // q = q_ySpin * q_baseX, where q_baseX = (-H, 0, 0, H).
    // Result: (x=-cy*H, y=sy*H, z=sy*H, w=cy*H)
    const a = time * 0.35;
    const sy = Math.sin(a * 0.5);
    const cy = Math.cos(a * 0.5);
    mycelium.setTransform(
        helmetEntity,
        0.0, 0.0, -3.0,
        cy * H, sy * H, -sy * H, cy * H,
        1.0, 1.0, 1.0
    );

    // After 5s the sun orbits so specular highlights travel across the surface.
    if (time > 5.0) {
        const sunAngle = (time - 5.0) * 0.25;
        const lx = Math.cos(sunAngle) * 0.7;
        const lz = Math.sin(sunAngle) * 0.5;
        mycelium.setDirectionalLight(lx, -0.8, lz,  1.0, 0.9, 0.75,  2.5);
    }
}
