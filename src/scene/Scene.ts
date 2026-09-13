import type {
  AIStateComponent,
  AccelerationComponent,
  AnimationStateComponent,
  ColliderComponent,
  EntityRef,
  HealthComponent,
  NameComponent,
  ParentComponent,
  PedestrianComponent,
  RenderableComponent,
  RigidBodyComponent,
  RotationComponent,
  ScaleComponent,
  TransformComponent,
  Vec3,
  VehicleComponent,
  VelocityComponent,
} from './Components';

export type EntityId = number;

export interface SceneComponents {
  Transform: TransformComponent;
  Rotation: RotationComponent;
  Scale: ScaleComponent;
  Velocity: VelocityComponent;
  Acceleration: AccelerationComponent;
  RigidBody: RigidBodyComponent;
  Collider: ColliderComponent;
  Health: HealthComponent;
  AIState: AIStateComponent;
  Pedestrian: PedestrianComponent;
  Vehicle: VehicleComponent;
  AnimationState: AnimationStateComponent;
  Renderable: RenderableComponent;
  Name: NameComponent;
  Parent: ParentComponent;
}

interface Slot {
  generation: number;
  alive: boolean;
}

export interface SceneSnapshot {
  entities: EntityRef[];
}

export class Scene {
  private slots: Slot[] = [];
  private free: number[] = [];
  private components: { [K in keyof SceneComponents]: Map<EntityId, SceneComponents[K]> } = {
    Transform: new Map(),
    Rotation: new Map(),
    Scale: new Map(),
    Velocity: new Map(),
    Acceleration: new Map(),
    RigidBody: new Map(),
    Collider: new Map(),
    Health: new Map(),
    AIState: new Map(),
    Pedestrian: new Map(),
    Vehicle: new Map(),
    AnimationState: new Map(),
    Renderable: new Map(),
    Name: new Map(),
    Parent: new Map(),
  };

  createEntity(): EntityRef {
    const index = this.free.pop() ?? this.slots.length;
    const slot = this.slots[index] ?? { generation: 1, alive: false };
    slot.alive = true;
    this.slots[index] = slot;
    return { index, generation: slot.generation };
  }

  destroyEntity(entity: EntityRef): boolean {
    if (!this.alive(entity)) return false;
    const slot = this.slots[entity.index]!;
    slot.alive = false;
    slot.generation = (slot.generation + 1) >>> 0 || 1;
    for (const store of Object.values(this.components)) store.delete(entity.index);
    this.free.push(entity.index);
    return true;
  }

  alive(entity: EntityRef): boolean {
    const slot = this.slots[entity.index];
    return !!slot?.alive && slot.generation === entity.generation;
  }

  clear(): void {
    for (const index of this.eachAlive()) this.destroyEntity(index);
  }

  add<K extends keyof SceneComponents>(entity: EntityRef, componentType: K, data: SceneComponents[K]): SceneComponents[K] {
    this.requireAlive(entity);
    this.components[componentType].set(entity.index, data);
    return data;
  }

  get<K extends keyof SceneComponents>(entity: EntityRef, componentType: K): SceneComponents[K] | undefined {
    if (!this.alive(entity)) return undefined;
    return this.components[componentType].get(entity.index);
  }

  remove<K extends keyof SceneComponents>(entity: EntityRef, componentType: K): boolean {
    if (!this.alive(entity)) return false;
    return this.components[componentType].delete(entity.index);
  }

  has<K extends keyof SceneComponents>(entity: EntityRef, componentType: K): boolean {
    return this.alive(entity) && this.components[componentType].has(entity.index);
  }

  query<K extends keyof SceneComponents>(componentType: K): Array<[EntityRef, SceneComponents[K]]> {
    const store = this.components[componentType];
    const result: Array<[EntityRef, SceneComponents[K]]> = [];
    for (const [index, data] of store) {
      const slot = this.slots[index];
      if (slot?.alive) result.push([{ index, generation: slot.generation }, data]);
    }
    return result;
  }

  eachAlive(): EntityRef[];
  eachAlive(visitor: (entity: EntityRef) => void): void;
  eachAlive(visitor?: (entity: EntityRef) => void): EntityRef[] | void {
    const snapshot: EntityRef[] = [];
    for (let index = 0; index < this.slots.length; index++) {
      const slot = this.slots[index];
      if (slot?.alive) snapshot.push({ index, generation: slot.generation });
    }
    if (!visitor) return snapshot;
    for (const entity of snapshot) visitor(entity);
  }

  get entityCount(): number {
    let count = 0;
    for (const slot of this.slots) if (slot.alive) count++;
    return count;
  }

  getComponentNames(entity: EntityRef): Array<keyof SceneComponents> {
    if (!this.alive(entity)) return [];
    return (Object.keys(this.components) as Array<keyof SceneComponents>).filter((type) =>
      this.components[type].has(entity.index),
    );
  }

  private requireAlive(entity: EntityRef): void {
    if (!this.alive(entity)) throw new Error(`Invalid or stale entity ${entity.index}:${entity.generation}.`);
  }
}

export type { Vec3 };
