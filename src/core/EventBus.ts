/**
 * Minimal typed event bus. Modules should never reach into each other directly —
 * they publish/subscribe through the engine's EventBus. This is what keeps
 * "renderer" and "physics" and "voxel world" from becoming tangled together.
 *
 * Usage:
 *   type Events = { 'player:jump': { force: number }; 'block:break': { x: number; y: number; z: number } };
 *   const bus = new EventBus<Events>();
 *   bus.on('player:jump', (payload) => { ... });
 *   bus.emit('player:jump', { force: 5 });
 */
export type EventMap = Record<string, unknown>;

type Listener<T> = (payload: T) => void;

export class EventBus<TEvents extends EventMap = EventMap> {
  // Keyed by string (not `keyof TEvents`) so EventBus<A> and EventBus<B>
  // don't become structurally incompatible at the type level — only the
  // public on/off/emit signatures below are generic over TEvents.
  private listeners = new Map<string, Set<Listener<any>>>();

  on<K extends keyof TEvents & string>(event: K, listener: Listener<TEvents[K]>): () => void {
    if (!this.listeners.has(event)) this.listeners.set(event, new Set());
    this.listeners.get(event)!.add(listener);
    return () => this.off(event, listener);
  }

  off<K extends keyof TEvents & string>(event: K, listener: Listener<TEvents[K]>): void {
    this.listeners.get(event)?.delete(listener);
  }

  emit<K extends keyof TEvents & string>(event: K, payload: TEvents[K]): void {
    this.listeners.get(event)?.forEach((listener) => listener(payload));
  }

  clear(): void {
    this.listeners.clear();
  }
}
