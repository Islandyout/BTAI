import type { EntityRef } from './Components';
import { Scene, type SceneComponents } from './Scene';
import type { AIStateName } from './Components';

export interface SceneDocument {
  format: 1;
  name?: string;
  entities: Array<{
    name?: string;
    parent?: EntityRef;
    components: Partial<{ [K in keyof SceneComponents]: SceneComponents[K] }>;
  }>;
}

function jsonValue(value: unknown): unknown {
  return JSON.parse(JSON.stringify(value));
}

export function serializeScene(scene: Scene, name?: string): SceneDocument {
  const indexByEntity = new Map<number, number>();
  scene.eachAlive().forEach((entity, index) => indexByEntity.set(entity.index, index));
  return {
    format: 1,
    ...(name ? { name } : {}),
    entities: scene.eachAlive().map((entity) => {
      const components = {} as Partial<{ [K in keyof SceneComponents]: SceneComponents[K] }>;
      for (const type of scene.getComponentNames(entity)) {
        const component = scene.get(entity, type);
        if (component) (components as Record<string, unknown>)[type] = jsonValue(component);
      }
      const parent = scene.get(entity, 'Parent');
      return {
        ...(scene.get(entity, 'Name')?.value ? { name: scene.get(entity, 'Name')!.value } : {}),
        ...(parent && indexByEntity.has(parent.entity.index)
          ? { parent: { index: indexByEntity.get(parent.entity.index)!, generation: 1 } }
          : {}),
        components,
      };
    }),
  };
}

export function serializeSceneText(scene: Scene, name?: string): string {
  return JSON.stringify(serializeScene(scene, name), null, 2);
}

export function deserializeScene(scene: Scene, document: unknown): void {
  if (!isSceneDocument(document)) throw new Error('Invalid scene document: expected format 1 and an entities array.');
  scene.clear();
  const entities = document.entities.map(() => scene.createEntity());
  document.entities.forEach((entry, sourceIndex) => {
    const target = entities[sourceIndex]!;
    for (const [type, raw] of Object.entries(entry.components)) {
      if (type === 'Parent') continue;
      if (!isComponentName(type)) throw new Error(`Unsupported component in scene: ${type}`);
      scene.add(target, type, normalizeComponent(type, raw));
    }
    if (entry.name && !scene.has(target, 'Name')) scene.add(target, 'Name', { value: entry.name });
  });
  document.entities.forEach((entry, sourceIndex) => {
    if (entry.parent) {
      const parent = entities[entry.parent.index];
      if (!parent) throw new Error(`Invalid parent index ${entry.parent.index}.`);
      scene.add(entities[sourceIndex]!, 'Parent', { entity: parent });
    }
  });
}

export function parseSceneText(text: string): SceneDocument {
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch (error) {
    throw new Error(`Invalid scene JSON: ${error instanceof Error ? error.message : String(error)}`);
  }
  if (!isSceneDocument(value)) throw new Error('Invalid scene document: expected format 1 and an entities array.');
  return value;
}

function isSceneDocument(value: unknown): value is SceneDocument {
  if (!value || typeof value !== 'object') return false;
  const record = value as Record<string, unknown>;
  return record.format === 1 && Array.isArray(record.entities) && record.entities.every((entry) => {
    if (!entry || typeof entry !== 'object') return false;
    const item = entry as Record<string, unknown>;
    return (!('name' in item) || typeof item.name === 'string') && typeof item.components === 'object' && item.components !== null;
  });
}

const componentNames = [
  'Transform', 'Rotation', 'Scale', 'Velocity', 'Acceleration', 'RigidBody', 'Collider',
  'Health', 'AIState', 'Pedestrian', 'Vehicle', 'AnimationState', 'Renderable', 'Name', 'Parent',
] as const;
type ComponentName = typeof componentNames[number];
function isComponentName(value: string): value is ComponentName {
  return (componentNames as readonly string[]).includes(value);
}

function isVec3(value: unknown): value is { x: number; y: number; z: number } {
  if (!value || typeof value !== 'object') return false;
  const v = value as Record<string, unknown>;
  return typeof v.x === 'number' && typeof v.y === 'number' && typeof v.z === 'number';
}

function normalizeComponent(type: ComponentName, raw: unknown): SceneComponents[ComponentName] {
  if (!raw || typeof raw !== 'object') throw new Error(`${type} must be an object.`);
  const value = raw as Record<string, unknown>;
  switch (type) {
    case 'Transform': return { position: requiredVec3(value.position, 'Transform.position') };
    case 'Rotation': return { euler: requiredVec3(value.euler, 'Rotation.euler') };
    case 'Scale': return { value: requiredVec3(value.value, 'Scale.value') };
    case 'Velocity': return { value: requiredVec3(value.value, 'Velocity.value') };
    case 'Acceleration': return { value: requiredVec3(value.value, 'Acceleration.value') };
    case 'RigidBody': {
      const mass = number(value.mass, 'RigidBody.mass');
      const dynamic = boolean(value.dynamic, 'RigidBody.dynamic');
      if (mass <= 0) throw new Error('RigidBody.mass must be positive.');
      return { mass, inverseMass: dynamic ? 1 / mass : 0, dynamic };
    }
    case 'Collider': {
      const colliderType = value.type;
      if (colliderType !== 'AABB' && colliderType !== 'Sphere') throw new Error('Collider.type must be AABB or Sphere.');
      return {
        type: colliderType,
        halfExtents: value.halfExtents === undefined ? { x: 0.5, y: 0.5, z: 0.5 } : requiredVec3(value.halfExtents, 'Collider.halfExtents'),
        radius: value.radius === undefined ? 0.5 : number(value.radius, 'Collider.radius'),
      };
    }
    case 'Health': return { current: number(value.current, 'Health.current'), maximum: number(value.maximum, 'Health.maximum') };
    case 'AIState': {
      const state = value.state;
      const states: readonly AIStateName[] = ['Idle', 'Walking', 'Running', 'Driving', 'Fleeing', 'Chasing', 'Dead'];
      if (typeof state !== 'string' || !states.includes(state as AIStateName)) throw new Error('AIState.state is invalid.');
      return { state: state as AIStateName };
    }
    case 'Pedestrian': return { archetype: unsigned(value.archetype, 'Pedestrian.archetype', 0) };
    case 'Vehicle': return { archetype: unsigned(value.archetype, 'Vehicle.archetype', 0) };
    case 'AnimationState': return { clip: unsigned(value.clip, 'AnimationState.clip', 0), time: number(value.time, 'AnimationState.time'), looping: boolean(value.looping, 'AnimationState.looping') };
    case 'Renderable': return { mesh: unsigned(value.mesh, 'Renderable.mesh', 0), material: unsigned(value.material, 'Renderable.material', 0), visible: boolean(value.visible, 'Renderable.visible') };
    case 'Name': return { value: string(value.value, 'Name.value') };
    case 'Parent': throw new Error('Parent is encoded separately.');
  }
}

function requiredVec3(value: unknown, label: string): { x: number; y: number; z: number } {
  if (!isVec3(value)) throw new Error(`${label} must be {x,y,z}.`);
  return { x: value.x, y: value.y, z: value.z };
}
function number(value: unknown, label: string): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) throw new Error(`${label} must be a finite number.`);
  return value;
}
function unsigned(value: unknown, label: string, fallback: number): number {
  const result = value === undefined ? fallback : value;
  if (typeof result !== 'number' || !Number.isInteger(result) || !Number.isSafeInteger(result) || result < 0) throw new Error(`${label} must be a non-negative integer.`);
  return result;
}
function boolean(value: unknown, label: string): boolean {
  if (typeof value !== 'boolean') throw new Error(`${label} must be boolean.`);
  return value;
}
function string(value: unknown, label: string): string {
  if (typeof value !== 'string') throw new Error(`${label} must be a string.`);
  return value;
}
