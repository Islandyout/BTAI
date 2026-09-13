import { EventBus, type EventMap } from './EventBus';
import type { EngineModule } from './EngineModule';

export interface EngineConfig {
  /** Fixed update rate in Hz. 60 is a sane default for physics/gameplay. */
  fixedTimestepHz?: number;
  /** Safety cap: max fixed steps run per frame, to avoid a "spiral of death"
   *  after a tab is backgrounded or a long GC pause. */
  maxFixedStepsPerFrame?: number;
}

/**
 * The engine core. Deliberately small: it owns the loop and the module
 * registry, nothing else. Rendering, physics, input, voxels, NPCs — all of
 * that lives in modules (see src/modules/) so the core never has to change
 * when you add a new subsystem.
 *
 * Loop pattern: fixed-timestep accumulator (the same pattern Box2D/most
 * engines use) — gameplay/physics step at a constant rate regardless of
 * display refresh rate, rendering runs every frame and interpolates.
 */
export class Engine<TEvents extends EventMap = EventMap> {
  readonly events = new EventBus<TEvents>();

  private modules: EngineModule[] = [];
  private moduleByName = new Map<string, EngineModule>();

  private readonly fixedDt: number;
  private readonly maxFixedSteps: number;

  private accumulator = 0;
  private lastTime = 0;
  private running = false;
  private rafHandle = 0;

  constructor(config: EngineConfig = {}) {
    const hz = config.fixedTimestepHz ?? 60;
    this.fixedDt = 1 / hz;
    this.maxFixedSteps = config.maxFixedStepsPerFrame ?? 5;
  }

  /** Register a module. Order matters: init() and update() run in this order. */
  use(module: EngineModule): this {
    if (this.moduleByName.has(module.name)) {
      throw new Error(`Engine: a module named "${module.name}" is already registered.`);
    }
    this.modules.push(module);
    this.moduleByName.set(module.name, module);
    return this;
  }

  /** Look up a registered module by name, typed via a generic cast at the call site. */
  getModule<T extends EngineModule = EngineModule>(name: string): T | undefined {
    return this.moduleByName.get(name) as T | undefined;
  }

  /** Runs init() on every module in registration order, then starts the loop. */
  async start(): Promise<void> {
    for (const module of this.modules) {
      await module.init?.(this);
    }
    this.running = true;
    this.lastTime = performance.now();
    this.rafHandle = requestAnimationFrame(this.tick);
  }

  stop(): void {
    this.running = false;
    cancelAnimationFrame(this.rafHandle);
    for (const module of [...this.modules].reverse()) {
      module.dispose?.();
    }
  }

  private tick = (now: number): void => {
    if (!this.running) return;

    let frameTime = (now - this.lastTime) / 1000;
    this.lastTime = now;
    // Clamp huge frame gaps (tab backgrounded, breakpoint hit) so we don't
    // try to "catch up" with hundreds of fixed steps at once.
    frameTime = Math.min(frameTime, this.fixedDt * this.maxFixedSteps);

    this.accumulator += frameTime;

    let steps = 0;
    while (this.accumulator >= this.fixedDt && steps < this.maxFixedSteps) {
      for (const module of this.modules) module.fixedUpdate?.(this.fixedDt);
      this.accumulator -= this.fixedDt;
      steps++;
    }

    const alpha = this.accumulator / this.fixedDt;
    for (const module of this.modules) module.update?.(frameTime, alpha);

    this.rafHandle = requestAnimationFrame(this.tick);
  };
}
