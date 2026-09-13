import type { Engine } from '@core/Engine';
import type { EngineModule } from '@core/EngineModule';
import { CommandInterpreter, LocalStorageSceneStore, type AuthoringStorage, type CommandResult } from '@authoring/CommandInterpreter';
import { Scene } from '@scene/Scene';

export interface AuthoringModuleOptions {
  scene?: Scene;
  storage?: AuthoringStorage;
}

export class AuthoringModule implements EngineModule {
  readonly name = 'authoring';
  readonly scene: Scene;
  readonly commands: CommandInterpreter;

  constructor(options: AuthoringModuleOptions = {}) {
    this.scene = options.scene ?? new Scene();
    this.commands = new CommandInterpreter(this.scene, options.storage ?? new LocalStorageSceneStore());
  }

  init(engine: Engine): void {
    engine.events.emit('authoring:ready', { scene: this.scene, commands: this.commands });
  }

  execute(command: string | unknown): CommandResult {
    return this.commands.execute(command);
  }
}
