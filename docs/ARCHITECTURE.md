# Architecture

## Why fixed-timestep + modules

Two decisions shape everything else in this codebase:

**Fixed-timestep accumulator loop.** `Engine.tick()` accumulates real elapsed
time and drains it in fixed-size steps (default 1/60s). This means physics
and gameplay logic behave identically regardless of the display's refresh
rate or momentary frame hitches — the same input produces the same result
every time, which matters the moment you want replays, deterministic NPC
behavior, or networked play. Rendering (`update()`) still runs once per
frame and receives an `alpha` interpolation factor for smooth visuals between
fixed steps.

**Modules over inheritance.** Rather than a `GameObject` base class with
virtual methods (the Unity/Unreal pattern), subsystems are independent
classes implementing one small interface (`EngineModule`) and registered
into a flat list. Adding voxel terrain doesn't require touching the renderer;
adding NPC AI doesn't require touching physics. The engine core
(`Engine.ts`) is ~100 lines and should stay that way — new capabilities are
new modules, not new engine features.

## Data flow for a typical frame

1. `Engine.tick(now)` computes elapsed time, clamps it against
   `maxFixedStepsPerFrame` (prevents a "spiral of death" after a long pause).
2. While there's enough accumulated time, run one fixed step: call
   `fixedUpdate(dt)` on every module, in registration order. This is where
   physics stepping and game-logic decisions happen.
3. Compute `alpha` = leftover accumulator / fixedDt.
4. Call `update(frameTime, alpha)` on every module, in registration order.
   `RendererModule.update()` is where the actual `renderer.render(scene,
   camera)` call happens — it's registered first so its render call still
   fires after every other module has had a chance to move things for this
   frame.

## Extending: adding a voxel world module (worked example)

This is the next planned module per the project's design notes — a rough
sketch of how it would fit without changing the core:

```ts
// src/modules/voxel/VoxelWorldModule.ts
export class VoxelWorldModule implements EngineModule {
  readonly name = 'voxel';
  private chunks = new Map<string, ChunkMesh>();

  init(engine: Engine) {
    const renderer = engine.getModule<RendererModule>('renderer')!;
    // load/generate initial chunks, build meshes, add to renderer.scene
  }

  fixedUpdate(dt: number) {
    // chunk loading/unloading based on player position,
    // emit engine.events.emit('voxel:chunkLoaded', {...}) for other modules
  }
}
```

It reads player position (from a component on the `Scene`, or from another
module via `engine.getModule`), adds/removes meshes on
`RendererModule.scene`, and emits events for anything that cares (physics
colliders per chunk, NPC pathfinding grid updates) — no direct imports of
those other modules required.

## Extending: NPC AI calling local Ollama

Per the project's design notes (local Ollama in Docker/WSL2), the important
constraint is: **never block `fixedUpdate` on a network/IPC call**. Pattern:

```ts
export class NpcBrainModule implements EngineModule {
  readonly name = 'npcBrain';
  private pendingDecisions = new Map<EntityId, Promise<Decision>>();
  private latestDecisions = new Map<EntityId, Decision>();

  fixedUpdate(dt: number) {
    for (const [id, npc] of /* query NPCs due for a decision */) {
      if (!this.pendingDecisions.has(id)) {
        const promise = this.requestDecisionFromOllama(npc)
          .then((d) => { this.latestDecisions.set(id, d); this.pendingDecisions.delete(id); });
        this.pendingDecisions.set(id, promise);
      }
      // act on this.latestDecisions.get(id) — last known decision, never awaited here
    }
  }
}
```

This keeps the game loop running at a steady rate even if a model call takes
200ms+, at the cost of NPCs acting on slightly stale decisions — which is
usually the right trade-off for this kind of AI.
