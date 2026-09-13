import type { Engine } from '@core/Engine';
import type { EngineModule } from '@core/EngineModule';

/**
 * Physics is intentionally a stub here. Rapier (WASM, free, MIT-licensed) is
 * a solid pick when you're ready — it's listed as an optionalDependency in
 * package.json so installing it doesn't bloat the base skeleton for people
 * who don't need physics yet.
 *
 * To wire it in:
 *   1. npm install @dimforge/rapier3d-compat
 *   2. In init(), do `const RAPIER = await import('@dimforge/rapier3d-compat')`
 *      then `await RAPIER.init()` and create `this.world = new RAPIER.World(...)`.
 *   3. In fixedUpdate(dt), call `this.world.step()` — note this runs at the
 *      *fixed* rate, never in update(), so physics stays deterministic
 *      regardless of display refresh rate.
 *   4. Emit engine.events.emit('physics:step', {...}) if other modules
 *      (like a voxel collider) need to react per-step.
 */
export class PhysicsModule implements EngineModule {
  readonly name = 'physics';

  init(_engine: Engine): void {
    // Intentionally empty. Replace with Rapier world setup when needed —
    // see the class doc comment above for the exact steps.
  }

  fixedUpdate(_dt: number): void {
    // this.world?.step();
  }
}
