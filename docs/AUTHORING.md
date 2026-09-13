# JSON authoring interface

The authoring layer is the engine's data-driven command surface. Tools, editor panels,
AI agents, tests, and gameplay bootstrap code can issue the same validated commands
without reaching into scene storage directly.

```ts
const authoring = engine.getModule<AuthoringModule>('authoring')!;
const result = authoring.execute({
  command: 'spawn_entity',
  name: 'Player',
  transform: [0, 1, 0],
  physics: true,
});
```

Supported commands:

- `spawn_entity` — optional `name`, `transform`, `velocity`, `physics`, `mass`
- `destroy_entity` — `entity`
- `set_transform` — `entity`, `position`
- `set_velocity` — `entity`, `velocity`
- `set_physics` — `entity`, `dynamic`, optional `mass`
- `set_ai_state` — `entity`, `state`
- `trigger_animation` — `entity`, `clip`, optional `looping`
- `attach_component` — `entity`, `type`
- `remove_component` — `entity`, `type`
- `list_entities` — returns entity handles and names
- `describe_entity` — returns the entity's current components
- `save_scene` — `path`
- `load_scene` — `path`

Entity handles are generation-checked `{ "index": number, "generation": number }`
objects, so stale commands fail instead of mutating a recycled entity slot.

Scene documents use `format: 1` and preserve component data plus parent/child hierarchy.
The browser implementation stores authored scenes in `localStorage`; the command
interpreter depends only on the small `AuthoringStorage` interface, so a native/editor
backend can provide filesystem or database persistence without changing the command API.
