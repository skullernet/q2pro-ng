Non-exhaustive list of technical changes/fixes from KEX Quake II:

* Game code converted from C++ to C11 with GNU extensions.
* Save games use easy to read/parse plain text format rather than JSON.
* Entity 0 *is* player 0 now. World entity has been moved to slot 8190. Slot
  8191 is special "none" entity that's never spawned.
* All temporary entities and muzzleflashes have been converted to events.
* gi.multicast, gi.unicast, gi.sound, etc are gone. Everything is entity,
  configstring or client command now.
* Delta encoding format is bitstream based and easily extensible. No more
  manual bit packing madness, just add new entries to entity/player state field
  table.
* Coordinates are 32-bit floats with efficient 14-bit encoding of discrete values.
* Efficient LEB encoding of integers.
* Alpha and scale are transmitted as 32-bit floats with no range restrictions.
* Gibs and beam entities with a model inherit alpha/scale from parent entity.
* Entity bbox encoding is scale independent.
* Looped sounds encode volume and attenuation information into sound index
  directly.
* Acceleration of `func_*` entities is calculated at native server framerate.
* Alias models with `EF_ANIM*` flags interpolate their frames correctly.
* Alias model animations are never overridden mid-frame as workaround for
  re-release high tick rate animation bugs.
* `EF_BFG` effect calculates lightramp modulo current frame instead of
  clamping.
* `RF_*` flags are propagated to linked models.
* `g_start_items` cvar and `start_items` worldspawn key can remove compass and
  add grapple.
* Sky surfaces on moving brush models are supported in BSPX maps for some
  unusual effects.
* Multiple skyboxes are supported per map: if sky texture is named
  `sky/<something>` it will be loaded as skybox cubemap (most common cubemap
  face layouts are supported and autodetected).
* Shadow lights properly interact with alpha tested surfaces.
* Shadow light parameters are encoded entirely in entity state.
* Transparent alias models are drawn with hidden faces removed.
* Client game module is no longer a joke: now it handles all client side
  effects, entity processing and 2D screen drawing.
* Client and server game modules are physically separate.
* Smoother client predicted view bobbing, no bobbing in
  midair/noclip/spectator.
* All view pitching effects have been moved to client and are server FPS
  independent. Weapon and damage kicks are processed at 10 FPS like in vanilla.
* Horizontal movement speed on ladders increased by 50%.
* Maximum configstring length increased to 8192 characters.
* Brush models no longer pollute configstring namespace.
* Server checks all entity clusters for visibility rather than falling back to
  topnode check (which can be slow).
* Player state stats are 32-bit signed integers (LEB-encoded).
* Player state has additional array for 10-bit unsigned ammo counters.
* Fog parameters are part of player state.
* Both weapon and powerup wheels can be shown at the same time.
