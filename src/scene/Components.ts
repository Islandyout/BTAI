export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export const vec3 = (x = 0, y = 0, z = 0): Vec3 => ({ x, y, z });

export interface TransformComponent { position: Vec3; }
export interface RotationComponent { euler: Vec3; }
export interface ScaleComponent { value: Vec3; }
export interface VelocityComponent { value: Vec3; }
export interface AccelerationComponent { value: Vec3; }
export interface RigidBodyComponent { mass: number; inverseMass: number; dynamic: boolean; }
export interface ColliderComponent {
  type: 'AABB' | 'Sphere';
  halfExtents: Vec3;
  radius: number;
}
export interface HealthComponent { current: number; maximum: number; }
export type AIStateName = 'Idle' | 'Walking' | 'Running' | 'Driving' | 'Fleeing' | 'Chasing' | 'Dead';
export interface AIStateComponent { state: AIStateName; }
export interface PedestrianComponent { archetype: number; }
export interface VehicleComponent { archetype: number; }
export interface AnimationStateComponent { clip: number; time: number; looping: boolean; }
export interface RenderableComponent { mesh: number; material: number; visible: boolean; }
export interface NameComponent { value: string; }
export interface ParentComponent { entity: EntityRef; }

export interface EntityRef {
  index: number;
  generation: number;
}
