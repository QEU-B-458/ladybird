# MyceliumVR SDK

This folder contains generated developer-facing JavaScript and TypeScript API files for world scripts.

## Files

- `mycelium.d.ts` provides TypeScript types and editor autocomplete for the native `mycelium` global.
- `mycelium.as.ts` provides low-level AssemblyScript declarations for the WASM import surface.
- `mycelium.js` provides a small convenience wrapper named `Mycelium`.
- `wasm-imports.md` documents the planned WASM host import surface.
- `mycelium-api.json` exposes the native API metadata for external tools.

Generated files are emitted by the MyceliumVR executable from the native `BridgeRegistry`:

```bash
./Build/release/bin/Ladybird --generate-sdk all --sdk-output UI/MyceliumVR/sdk
./Build/release/bin/Ladybird --generate-sdk typescript --sdk-output UI/MyceliumVR/sdk
./Build/release/bin/Ladybird --generate-sdk assemblyscript --sdk-output UI/MyceliumVR/sdk
./Build/release/bin/Ladybird --generate-sdk javascript --sdk-output UI/MyceliumVR/sdk
./Build/release/bin/Ladybird --generate-sdk wasm --sdk-output UI/MyceliumVR/sdk
./Build/release/bin/Ladybird --generate-sdk json --sdk-output UI/MyceliumVR/sdk
```

## API Layers

Use the native global for hot-path code:

```js
const entity = mycelium.spawnEntity();

function update(time) {
  mycelium.setTransform(entity, Math.sin(time), 0, 0, 0, 0, 0, 1, 1, 1, 1);
}
```

Use the wrapper for setup, tools, and readability:

```js
import { Mycelium } from "./mycelium.js";

let cube;

function start() {
  cube = Mycelium.spawnEntity()
    .setMesh("cube")
    .setMaterial("accent");
}

function update(time) {
  cube.setTransform({ px: Math.sin(time), py: 0, pz: 0 });
}
```

The wrapper is intentionally thin. The performance-critical bridge remains the low-level numeric `mycelium.*` API.

## TypeScript Setup

Until MyceliumVR owns the TypeScript compiler flow, projects can reference the declarations directly:

```ts
/// <reference path="./path/to/mycelium.d.ts" />
```

## AssemblyScript Setup

Use the generated AssemblyScript declarations for low-level host imports:

```ts
import { spawnEntity, setTransform } from "./path/to/mycelium.as";
```

String-bearing imports in `mycelium.as.ts` use UTF-8 pointer/length pairs, because they model the WASM boundary directly rather than the JavaScript `mycelium` global.

The SDK is generated from the native registry so C++ remains the source of truth while TypeScript declarations, JavaScript helpers, WASM docs, and API JSON stay in sync.
