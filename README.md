# engine-skeleton

A modular TypeScript game engine foundation with a fixed-timestep loop, a generation-safe
scene model, and a validated JSON authoring surface shared by tools, editor code, and AI agents.

## Quick start

```bash
npm install
npm run dev
npm run typecheck
```

## Architecture

```
src/core/          Engine loop, EventBus, EngineModule interface.
src/scene/         Generation-safe entities/components + JSON scene serialization.
src/authoring/     Validated JSON command interpreter + persistence interface.
src/modules/
  renderer/        Three.js renderer module.
  input/           Keyboard/mouse state module.
  physics/         Fixed-step physics module boundary.
  authoring/       EngineModule exposing the authoring command surface.
examples/
  basic-scene/     Runnable example using the authoring layer to create Player.
```

Modules remain independently registered. Authoring commands operate on the same scene data
used by runtime systems, so a tool does not need a second object model just to edit a scene.

See `docs/AUTHORING.md` for the command contract and `docs/ARCHITECTURE.md` for the loop/module model.
