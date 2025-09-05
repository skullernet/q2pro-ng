Q2PRO-ng
========

Experimental Q2PRO fork aimed at cleaning up codebase and modernizing engine
architecture. My interpretation of what should constitute truly _remastered_
Quake II. Compatible with 2023 re-release assets. Not backward compatible with
vanilla Quake II.

Project goals:

* Unified network model: everything is entity.
* Escape flags hell: parse delta updates at bitstream level.
* Rich cgame module: move all high level client logic into it.
* Predict/derive more player state values on the client.
* Remove raw network message access from game/cgame.
* Remove server access to client console.
* Remove UDP downloading: server pushes compressed packfiles over HTTP.
* Run portable game/cgame/ui modules in WASM-based QVM sandbox.
* Support most Quake II re-release features.

Provides the following advantages over Quake II re-release:

* No C++, JSON and other stuff that doesn't belong in Quake II engine.
* Smoother player movement and entity animation.
* Vanilla-like server FPS independent view pitching.
* Full-featured client game module for greater modding possibilities.
* Server and client game code fully cross-platform and sandboxed.
* Lower system requirements: OpenGL 3.1 and SSE2 capable CPU.
* Runs natively on Linux, BSD and similar OSes.
* More stable and more secure.

Non-exhaustive list of technical changes from Quake II re-release:

* Gibs and beam entities with a model inherit alpha/scale from parent entity.
* Alias models with `EF_ANIM*` flags interpolate their frames correctly.
* Alias model animations are never overridden mid-frame as workaround for
re-release high tick rate animation bugs.
* `EF_BFG` effect calculates lightramp modulo current frame instead of clamping.
* `RF_*` flags are propagated to linked models.
* `g_start_items` cvar and `start_items` worldspawn key can remove compass and add grapple.
* Sky surfaces on moving brush models are supported in BSPX maps for some unusual effects.
* Multiple skyboxes are supported per map: if sky texture is named `sky/<something>` it will be
loaded as skybox texture (most common cubemap face layouts are supported and autodetected).
* Smoother client predicted view bobbing, no bobbing in midair/noclip/spectator.
* Horizontal movement speed on ladders increased by 50%.
* Maximum configstring length increased to 8192 characters.
* Brush models no longer pollute configstring namespace.
* Player state stats extended to 32-bit with additional array for 10-bit ammo counters.
* Both weapon and powerup wheels can be shown at the same time.

**This project is work in progress.** Game API, network protocol, savegame
format are not stable yet and may change anytime. You have been warned.
