# TinyGLTF v3 Vendor Folder

This directory is reserved for a vendored TinyGLTF v3 integration for MyceliumVR.

Why this exists:
- The current `vcpkg` `tinygltf` package in this checkout resolves to the older `tiny_gltf.h` API.
- MyceliumVR may want to target the newer TinyGLTF v3 API explicitly instead of depending on that older packaged interface.

Expected vendored files:
- `tiny_gltf_v3.h`
- `tinygltf_json.h`
- any upstream license/copyright files required by TinyGLTF v3

Recommended integration model:
- Keep the third-party headers isolated in this folder.
- Wrap them through a MyceliumVR-owned loader such as `Support/GltfLoader.*`.
- Avoid leaking TinyGLTF-specific types into the wider engine API.

Current state:
- This folder is only a scaffold right now.
- The active build is still using the `vcpkg` TinyGLTF package until we switch the loader over.
