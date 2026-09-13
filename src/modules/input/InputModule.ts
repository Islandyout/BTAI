import type { Engine } from '@core/Engine';
import type { EngineModule } from '@core/EngineModule';

/**
 * Normalizes keyboard + mouse into a poll-able snapshot. Other modules ask
 * "is this key down right now" instead of each attaching their own DOM
 * listeners — keeps input handling in one place, and makes it trivial to
 * later swap in gamepad/touch by extending this same module's surface.
 */
export class InputModule implements EngineModule {
  readonly name = 'input';

  private keysDown = new Set<string>();
  private mouseDelta = { x: 0, y: 0 };
  private mouseButtonsDown = new Set<number>();

  init(_engine: Engine): void {
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
    window.addEventListener('mousemove', this.onMouseMove);
    window.addEventListener('mousedown', this.onMouseDown);
    window.addEventListener('mouseup', this.onMouseUp);
  }

  isKeyDown(code: string): boolean {
    return this.keysDown.has(code);
  }

  isMouseButtonDown(button: number): boolean {
    return this.mouseButtonsDown.has(button);
  }

  /** Accumulated mouse movement since the last call; call once per frame and it resets. */
  consumeMouseDelta(): { x: number; y: number } {
    const delta = this.mouseDelta;
    this.mouseDelta = { x: 0, y: 0 };
    return delta;
  }

  private onKeyDown = (e: KeyboardEvent) => this.keysDown.add(e.code);
  private onKeyUp = (e: KeyboardEvent) => this.keysDown.delete(e.code);
  private onMouseDown = (e: MouseEvent) => this.mouseButtonsDown.add(e.button);
  private onMouseUp = (e: MouseEvent) => this.mouseButtonsDown.delete(e.button);
  private onMouseMove = (e: MouseEvent) => {
    this.mouseDelta.x += e.movementX;
    this.mouseDelta.y += e.movementY;
  };

  dispose(): void {
    window.removeEventListener('keydown', this.onKeyDown);
    window.removeEventListener('keyup', this.onKeyUp);
    window.removeEventListener('mousemove', this.onMouseMove);
    window.removeEventListener('mousedown', this.onMouseDown);
    window.removeEventListener('mouseup', this.onMouseUp);
  }
}
