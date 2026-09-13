import * as THREE from 'three';
import type { Engine } from '@core/Engine';
import type { EngineModule } from '@core/EngineModule';

export interface RendererModuleOptions {
  /** Element to mount the canvas into. Defaults to document.body. */
  container?: HTMLElement;
  clearColor?: number;
  antialias?: boolean;
}

/**
 * Thin Three.js wrapper exposed as an EngineModule. Deliberately does almost
 * nothing beyond scene/camera/renderer setup and resize handling — game code
 * should reach in via engine.getModule<RendererModule>('renderer').scene to
 * add meshes, rather than this module growing game-specific logic.
 *
 * Swappable: if you later want WebGPU or a 2D canvas backend, write another
 * class implementing EngineModule with the same name ('renderer') and the
 * rest of the engine doesn't need to change.
 */
export class RendererModule implements EngineModule {
  readonly name = 'renderer';

  readonly scene = new THREE.Scene();
  readonly camera = new THREE.PerspectiveCamera(70, 1, 0.1, 1000);
  private renderer!: THREE.WebGLRenderer;
  private container!: HTMLElement;
  private options: RendererModuleOptions;

  constructor(options: RendererModuleOptions = {}) {
    this.options = options;
  }

  init(_engine: Engine): void {
    this.container = this.options.container ?? document.body;

    this.renderer = new THREE.WebGLRenderer({ antialias: this.options.antialias ?? true });
    this.renderer.setClearColor(this.options.clearColor ?? 0x111318);
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.container.appendChild(this.renderer.domElement);

    this.camera.position.set(0, 2, 6);

    this.resize();
    window.addEventListener('resize', this.resize);
  }

  private resize = (): void => {
    const width = this.container.clientWidth || window.innerWidth;
    const height = this.container.clientHeight || window.innerHeight;
    this.renderer.setSize(width, height);
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
  };

  update(): void {
    this.renderer.render(this.scene, this.camera);
  }

  dispose(): void {
    window.removeEventListener('resize', this.resize);
    this.renderer.dispose();
    this.renderer.domElement.remove();
  }
}
