import {
  deserializeScene,
  parseSceneText,
  serializeSceneText,
  type SceneDocument,
} from '@scene/SceneSerializer';
import type {
  AIStateName,
  ColliderComponent,
  EntityRef,
  Vec3,
} from '@scene/Components';
import { Scene, type SceneComponents } from '@scene/Scene';

export interface CommandResult {
  ok: boolean;
  entity?: EntityRef;
  error?: string;
  data?: unknown;
}

export interface AuthoringStorage {
  read(path: string): string | null;
  write(path: string, text: string): void;
}

export class LocalStorageSceneStore implements AuthoringStorage {
  constructor(private readonly namespace = 'btai:scene:') {}

  read(path: string): string | null {
    return window.localStorage.getItem(this.namespace + path);
  }

  write(path: string, text: string): void {
    window.localStorage.setItem(this.namespace + path, text);
  }
}

const aiStates: readonly AIStateName[] = ['Idle', 'Walking', 'Running', 'Driving', 'Fleeing', 'Chasing', 'Dead'];
const componentNames = [
  'Transform', 'Rotation', 'Scale', 'Velocity', 'Acceleration', 'RigidBody', 'Collider',
  'Health', 'AIState', 'Pedestrian', 'Vehicle', 'AnimationState', 'Renderable', 'Name', 'Parent',
] as const;
type ComponentName = typeof componentNames[number];

export class CommandInterpreter {
  constructor(
    private readonly scene: Scene,
    private readonly storage?: AuthoringStorage,
  ) {}

  execute(input: string | unknown): CommandResult {
    try {
      const command = typeof input === 'string' ? JSON.parse(input) : input;
      return this.executeParsed(command);
    } catch (error) {
      return this.fail(`invalid JSON: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  executeParsed(command: unknown): CommandResult {
    if (!isRecord(command) || typeof command.command !== 'string') return this.fail('command must be a string');
    try {
      switch (command.command) {
        case 'spawn_entity': return this.spawn(command);
        case 'destroy_entity': return this.destroy(command);
        case 'set_transform': return this.setTransform(command);
        case 'set_velocity': return this.setVelocity(command);
        case 'set_physics': return this.setPhysics(command);
        case 'set_ai_state': return this.setAiState(command);
        case 'trigger_animation': return this.triggerAnimation(command);
        case 'attach_component': return this.attachComponent(command);
        case 'remove_component': return this.removeComponent(command);
        case 'list_entities': return { ok: true, data: this.listEntities() };
        case 'describe_entity': return this.describe(command);
        case 'save_scene': return this.saveScene(command);
        case 'load_scene': return this.loadScene(command);
        default: return this.fail(`unknown command: ${command.command}`);
      }
    } catch (error) {
      return this.fail(`command failed: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  private spawn(command: Record<string, unknown>): CommandResult {
    const entity = this.scene.createEntity();
    try {
      if (command.transform !== undefined) this.scene.add(entity, 'Transform', { position: readVec3(command.transform, 'transform') });
      if (command.velocity !== undefined) this.scene.add(entity, 'Velocity', { value: readVec3(command.velocity, 'velocity') });
      if (command.physics !== undefined) {
        const enabled = readBoolean(command.physics, 'physics');
        if (enabled) {
          const mass = command.mass === undefined ? 1 : readPositive(command.mass, 'mass');
          this.scene.add(entity, 'RigidBody', { mass, inverseMass: 1 / mass, dynamic: true });
          this.scene.add(entity, 'Collider', defaultCollider());
        }
      }
      if (command.name !== undefined) this.scene.add(entity, 'Name', { value: readString(command.name, 'name') });
      return { ok: true, entity };
    } catch (error) {
      this.scene.destroyEntity(entity);
      throw error;
    }
  }

  private destroy(command: Record<string, unknown>): CommandResult {
    const entity = requireEntity(this.scene, command.entity);
    return this.scene.destroyEntity(entity)
      ? { ok: true, entity }
      : this.fail('destroy failed');
  }

  private setTransform(command: Record<string, unknown>): CommandResult {
    const entity = requireEntity(this.scene, command.entity);
    const position = readVec3(command.position, 'position');
    const transform = this.scene.get(entity, 'Transform');
    if (transform) transform.position = position;
    else this.scene.add(entity, 'Transform', { position });
    return { ok: true, entity };
  }

  private setVelocity(command: Record<string, unknown>): CommandResult {
    const entity = requireEntity(this.scene, command.entity);
    const value = readVec3(command.velocity, 'velocity');
    const velocity = this.scene.get(entity, 'Velocity');
    if (velocity) velocity.value = value;
    else this.scene.add(entity, 'Velocity', { value });
    return { ok: true, entity };
  }

  private setPhysics(command: Record<string, unknown>): CommandResult {
    const entity = requireEntity(this.scene, command.entity);
    const dynamic = readBoolean(command.dynamic, 'dynamic');
    const mass = command.mass === undefined ? this.scene.get(entity, 'RigidBody')?.mass ?? 1 : readPositive(command.mass, 'mass');
    const body = this.scene.get(entity, 'RigidBody');
    const next = body ?? this.scene.add(entity, 'RigidBody', { mass, inverseMass: dynamic ? 1 / mass : 0, dynamic });
    next.mass = mass;
    next.dynamic = dynamic;
    next.inverseMass = dynamic ? 1 / mass : 0;
    if (!this.scene.has(entity, 'Collider')) this.scene.add(entity, 'Collider', defaultCollider());
    return { ok: true, entity };
  }

  private setAiState(command: Record<string, unknown>): CommandResult {
    const entity = requireEntity(this.scene, command.entity);
    const state = readEnum(command.state, aiStates, 'state');
    const ai = this.scene.get(entity, 'AIState');
    if (ai) ai.state = state;
    else this.scene.add(entity, 'AIState', { state });
    return { ok: true, entity };
  }

  private triggerAnimation(command: Record<string, unknown>): CommandResult {
    const entity = requireEntity(this.scene, command.entity);
    const clip = readUnsigned(command.clip, 'clip');
    const looping = command.looping === undefined ? true : readBoolean(command.looping, 'looping');
    const animation = this.scene.get(entity, 'AnimationState');
    const next = animation ?? this.scene.add(entity, 'AnimationState', { clip, time: 0, looping });
    next.clip = clip;
    next.time = 0;
    next.looping = looping;
    return { ok: true, entity };
  }

  private attachComponent(command: Record<string, unknown>): CommandResult {
    const entity = requireEntity(this.scene, command.entity);
    const type = readComponentName(command.type);
    if (type === 'Parent') return this.fail('Parent requires an entity reference and is not attachable without one');
    if (this.scene.has(entity, type)) return { ok: true, entity };
    this.scene.add(entity, type, defaultComponent(type));
    return { ok: true, entity };
  }

  private removeComponent(command: Record<string, unknown>): CommandResult {
    const entity = requireEntity(this.scene, command.entity);
    const type = readComponentName(command.type);
    const removed = this.scene.remove(entity, type);
    return removed ? { ok: true, entity } : this.fail(`component not present: ${type}`);
  }

  private listEntities(): unknown[] {
    return this.scene.eachAlive().map((entity) => ({
      index: entity.index,
      generation: entity.generation,
      ...(this.scene.get(entity, 'Name') ? { name: this.scene.get(entity, 'Name')!.value } : {}),
    }));
  }

  private describe(command: Record<string, unknown>): CommandResult {
    const entity = requireEntity(this.scene, command.entity);
    const components: Record<string, unknown> = {};
    for (const type of this.scene.getComponentNames(entity)) {
      if (type === 'Parent') {
        const parent = this.scene.get(entity, 'Parent');
        if (parent) components[type] = { entity: parent.entity };
      } else {
        components[type] = this.scene.get(entity, type);
      }
    }
    return { ok: true, entity, data: components };
  }

  private saveScene(command: Record<string, unknown>): CommandResult {
    const path = readString(command.path, 'path');
    if (!this.storage) return this.fail('save_scene requires authoring storage');
    this.storage.write(path, serializeSceneText(this.scene, path));
    return { ok: true };
  }

  private loadScene(command: Record<string, unknown>): CommandResult {
    const path = readString(command.path, 'path');
    if (!this.storage) return this.fail('load_scene requires authoring storage');
    const text = this.storage.read(path);
    if (text === null) return this.fail(`scene not found: ${path}`);
    const document = parseSceneText(text);
    deserializeScene(this.scene, document);
    return { ok: true, data: document };
  }

  private fail(error: string): CommandResult {
    return { ok: false, error };
  }
}

function requireEntity(scene: Scene, value: unknown): EntityRef {
  if (!isRecord(value) || !Number.isInteger(value.index) || !Number.isInteger(value.generation)) {
    throw new Error('entity must be {index,generation}');
  }
  const entity = { index: value.index as number, generation: value.generation as number };
  if (!scene.alive(entity)) throw new Error(`invalid or stale entity ${entity.index}:${entity.generation}`);
  return entity;
}

function readVec3(value: unknown, label: string): Vec3 {
  if (Array.isArray(value) && value.length === 3 && value.every((v) => typeof v === 'number' && Number.isFinite(v))) {
    return { x: value[0]!, y: value[1]!, z: value[2]! };
  }
  if (isRecord(value) && typeof value.x === 'number' && typeof value.y === 'number' && typeof value.z === 'number') {
    return { x: value.x, y: value.y, z: value.z };
  }
  throw new Error(`${label} must be [x,y,z] or {x,y,z}`);
}

function readBoolean(value: unknown, label: string): boolean {
  if (typeof value !== 'boolean') throw new Error(`${label} must be boolean`);
  return value;
}
function readString(value: unknown, label: string): string {
  if (typeof value !== 'string' || value.length === 0) throw new Error(`${label} must be a non-empty string`);
  return value;
}
function readPositive(value: unknown, label: string): number {
  if (typeof value !== 'number' || !Number.isFinite(value) || value <= 0) throw new Error(`${label} must be positive`);
  return value;
}
function readUnsigned(value: unknown, label: string): number {
  if (!Number.isSafeInteger(value) || (value as number) < 0) throw new Error(`${label} must be a non-negative integer`);
  return value as number;
}
function readEnum<T extends string>(value: unknown, values: readonly T[], label: string): T {
  if (typeof value !== 'string' || !values.includes(value as T)) throw new Error(`${label} is invalid`);
  return value as T;
}
function readComponentName(value: unknown): ComponentName {
  if (typeof value !== 'string' || !componentNames.includes(value as ComponentName)) throw new Error(`unsupported component: ${String(value)}`);
  return value as ComponentName;
}
function defaultCollider(): ColliderComponent {
  return { type: 'AABB', halfExtents: { x: 0.5, y: 0.5, z: 0.5 }, radius: 0.5 };
}
function defaultComponent(type: Exclude<ComponentName, 'Parent'>): SceneComponents[typeof type] {
  switch (type) {
    case 'Transform': return { position: { x: 0, y: 0, z: 0 } };
    case 'Rotation': return { euler: { x: 0, y: 0, z: 0 } };
    case 'Scale': return { value: { x: 1, y: 1, z: 1 } };
    case 'Velocity': return { value: { x: 0, y: 0, z: 0 } };
    case 'Acceleration': return { value: { x: 0, y: 0, z: 0 } };
    case 'RigidBody': return { mass: 1, inverseMass: 1, dynamic: true };
    case 'Collider': return defaultCollider();
    case 'Health': return { current: 100, maximum: 100 };
    case 'AIState': return { state: 'Idle' };
    case 'Pedestrian': return { archetype: 0 };
    case 'Vehicle': return { archetype: 0 };
    case 'AnimationState': return { clip: 0, time: 0, looping: true };
    case 'Renderable': return { mesh: 0, material: 0, visible: true };
    case 'Name': return { value: 'Entity' };
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

export type { SceneDocument };
