#!/usr/bin/env python3
"""Generate a minimal valid cube .glb file for the example world."""

import struct
import json
import os

# Square pyramid - visually distinct from the built-in cube so we can confirm glTF loading
# 5 vertices: 4 base corners + 1 apex
positions = [
    -0.5, -0.5, -0.5,  # 0 base front-left
     0.5, -0.5, -0.5,  # 1 base front-right
     0.5, -0.5,  0.5,  # 2 base back-right
    -0.5, -0.5,  0.5,  # 3 base back-left
     0.0,  0.5,  0.0,  # 4 apex
]

# 6 triangles: 2 for base, 4 sides
indices = [
    0, 2, 1,  0, 3, 2,  # base
    0, 1, 4,            # front side
    1, 2, 4,            # right side
    2, 3, 4,            # back side
    3, 0, 4,            # left side
]

# Binary chunk: positions (float32) then indices (uint16)
pos_bytes = struct.pack(f"{len(positions)}f", *positions)
idx_bytes = struct.pack(f"{len(indices)}H", *indices)

# Pad each to 4-byte alignment
def pad4(data):
    r = len(data) % 4
    return data + b"\x00" * (4 - r if r else 0)

pos_bytes_padded = pad4(pos_bytes)
idx_bytes_padded = pad4(idx_bytes)

bin_data = pos_bytes_padded + idx_bytes_padded

pos_offset = 0
pos_length = len(pos_bytes)
idx_offset = len(pos_bytes_padded)
idx_length = len(idx_bytes)

pos_min = [min(positions[i::3]) for i in range(3)]
pos_max = [max(positions[i::3]) for i in range(3)]

gltf = {
    "asset": {"version": "2.0", "generator": "MyceliumVR generate_cube_glb.py"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"mesh": 0, "name": "Cube"}],
    "meshes": [{
        "name": "Cube",
        "primitives": [{
            "attributes": {"POSITION": 0},
            "indices": 1,
            "mode": 4  # TRIANGLES
        }]
    }],
    "accessors": [
        {
            "bufferView": 0,
            "byteOffset": 0,
            "componentType": 5126,  # FLOAT
            "count": len(positions) // 3,
            "type": "VEC3",
            "min": pos_min,
            "max": pos_max
        },
        {
            "bufferView": 1,
            "byteOffset": 0,
            "componentType": 5123,  # UNSIGNED_SHORT
            "count": len(indices),
            "type": "SCALAR"
        }
    ],
    "bufferViews": [
        {"buffer": 0, "byteOffset": pos_offset, "byteLength": pos_length, "target": 34962},  # ARRAY_BUFFER
        {"buffer": 0, "byteOffset": idx_offset, "byteLength": idx_length, "target": 34963},  # ELEMENT_ARRAY_BUFFER
    ],
    "buffers": [{"byteLength": len(bin_data)}]
}

json_bytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
json_bytes_padded = pad4(json_bytes + b" " * (4 - len(json_bytes) % 4 if len(json_bytes) % 4 else 0))

# GLB header + JSON chunk + BIN chunk
json_chunk = struct.pack("<II", len(json_bytes_padded), 0x4E4F534A) + json_bytes_padded
bin_chunk  = struct.pack("<II", len(bin_data),          0x004E4942) + bin_data

total_length = 12 + len(json_chunk) + len(bin_chunk)
header = struct.pack("<III", 0x46546C67, 2, total_length)

glb = header + json_chunk + bin_chunk

out_path = os.path.join(os.path.dirname(__file__), "../worlds/example/assets/cube.glb")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
with open(out_path, "wb") as f:
    f.write(glb)

print(f"Wrote {len(glb)} bytes to {os.path.normpath(out_path)}")
print(f"  {len(positions) // 3} vertices, {len(indices) // 3} triangles")
