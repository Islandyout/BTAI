import type { Engine } from './Engine';

/**
 * Everything that plugs into the engine — renderer, input, physics, a voxel
 * world, your NPC AI layer — implements this. The engine doesn't know or care
 * what a module does internally; it only calls these lifecycle hooks in order.
 *
 * Design rule: a module should be removable without breaking other modules.
 * If two modules need to talk, they do it through Engine.events, not by
 * importing each other directly.
 */
export interface EngineModule {
  /** Unique name, used for lookup via engine.getModule(name) and in logs. */
  readonly name: string;

  /**
   * Called once, in registration order, after all modules are registered.
   * Do setup here (create renderer, connect to physics world, load config).
   * May be async — the engine awaits all init() calls before starting the loop.
   */
  init?(engine: Engine): void | Promise<void>;

  /**
   * Fixed-timestep update (default 60Hz), for anything that needs
   * deterministic, frame-rate-independent stepping: physics, game logic,
   * NPC decision ticks. dt is constant every call.
   */
  fixedUpdate?(dt: number): void;

  /**
   * Variable-timestep update, called once per animation frame, for rendering
   * and anything purely visual. `alpha` (0..1) is the interpolation factor
   * between the last two fixedUpdate steps, for smooth rendering at any
   * refresh rate — pass it to your renderer to lerp positions if needed.
   */
  update?(dt: number, alpha: number): void;

  /** Called on engine shutdown, in reverse registration order. Clean up here. */
  dispose?(): void;
}
