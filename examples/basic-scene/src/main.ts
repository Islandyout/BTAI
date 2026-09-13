import * as THREE from 'three';
import { Engine } from '@core/Engine';
import { RendererModule } from '@modules/renderer/RendererModule';
import { InputModule } from '@modules/input/InputModule';
import { PhysicsModule } from '@modules/physics/PhysicsModule';
import { AuthoringModule } from '@modules/authoring/AuthoringModule';
import type { EngineModule } from '@core/EngineModule';

class ScenePresentationModule implements EngineModule {
  readonly name = 'scenePresentation';
  private cube?: THREE.Mesh;
  private entity = { index: 0, generation: 0 };

  constructor(private readonly authoring: AuthoringModule) {}

  init(engine: Engine): void {
    const renderer = engine.getModule<RendererModule>('renderer')!;
    const result = this.authoring.execute({
      command: 'spawn_entity',
      name: 'Player',
      transform: [0, 1, 0],
      physics: true,
    });
    if (!result.ok || !result.entity) throw new Error(result.error ?? 'Failed to create Player');
    this.entity = result.entity;

    this.cube = new THREE.Mesh(
      new THREE.BoxGeometry(1, 1, 1),
      new THREE.MeshStandardMaterial({ color: 0x4f8cff }),
    );
    renderer.scene.add(this.cube);
    renderer.scene.add(new THREE.DirectionalLight(0xffffff, 2));
    renderer.scene.add(new THREE.AmbientLight(0x404040, 1.5));
  }

  fixedUpdate(_dt: number): void {
    const transform = this.authoring.scene.get(this.entity, 'Transform');
    if (transform) transform.position.y = 1;
  }

  update(): void {
    if (this.cube) this.cube.rotation.y += 0.01;
  }
}

const authoring = new AuthoringModule();
const engine = new Engine();

engine
  .use(new RendererModule({ clearColor: 0x111318 }))
  .use(new InputModule())
  .use(new PhysicsModule())
  .use(authoring)
  .use(new ScenePresentationModule(authoring));

void engine.start();
