# Instructions for Claude Code in this repo

This is a from-scratch, modular game engine skeleton. Read this before making
changes — it exists so any AI coding agent (Claude Code, Codex, etc.) picking
this up mid-project follows the same conventions as whatever came before. This
file and AGENTS.md are kept identical in content; update both together.

## Architecture in one paragraph

`src/core/Engine.ts` runs a fixed-timestep loop and owns nothing except a list
of `EngineModule`s (`src/core/EngineModule.ts`). Every subsystem — renderer,
input, physics, a future voxel world, NPC AI — is a class implementing
`EngineModule` and registered via `engine.use(new SomeModule())`. Modules
never import each other directly; they communicate through `engine.events`
(`src/core/EventBus.ts`) or by looking each other up via
`engine.getModule('name')`. `src/scene/Scene.ts` is a minimal
entity-component store, backend-agnostic (no Three.js import).

## Rules when adding a feature

1. **New subsystem = new module.** Put it in `src/modules/<name>/`, implement
   `EngineModule`, register it in `examples/basic-scene/src/main.ts` (or a new
   example). Do not add subsystem logic to `Engine.ts` itself.
2. **Fixed vs variable update.** Anything that must be deterministic
   (physics, game state, NPC decisions) goes in `fixedUpdate(dt)`. Anything
   purely visual (camera smoothing, particle rendering) goes in
   `update(dt, alpha)`. Never call `Math.random()` or read wall-clock time
   inside `fixedUpdate` if you want replay/determinism later.
3. **No cross-module imports.** If module A needs to react to module B, use
   `engine.events.emit(...)` / `.on(...)`, not a direct import. Exception:
   `RendererModule.scene` is treated as a shared resource other modules may
   add meshes to directly — that's intentional, not a violation.
4. **Keep the core dependency-free.** `src/core/` and `src/scene/` must not
   import Three.js, Rapier, or any other third-party library. Those belong in
   `src/modules/*` only. This is what lets the renderer or physics backend be
   swapped later without touching the core.
5. **Run `npm run typecheck` before considering a change done.** No `any`
   without a comment explaining why.

## Where things likely go next (not yet built)

- Voxel world module: chunk storage + meshing, lives in `src/modules/voxel/`,
  reads/writes to a `Scene` via components, adds meshes to
  `RendererModule.scene`.
- NPC AI module: per the project notes, intended to call a local Ollama
  instance. Keep the HTTP/IPC call async and out of `fixedUpdate` directly —
  have `fixedUpdate` read cached decisions and kick off the next async
  request, so a slow model call never blocks the game loop.
- Physics: see the doc comment in `src/modules/physics/PhysicsModule.ts` for
  the exact Rapier wiring steps already scoped out.

## Commands

- `npm install`
- `npm run dev` — Vite dev server for the basic-scene example
- `npm run typecheck` — TypeScript check, no emit
- `npm run build` — typecheck + production build of the example
