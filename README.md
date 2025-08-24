Q2PRO-ng
========

Experimental Q2PRO fork aimed at cleaning up codebase and modernizing engine
architecture. My interpretation of what should constitute truly _remastered_
Quake II. Compatible with 2023 remaster assets. Not backward compatible with
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
* Support most Quake II remaster features.

Provides the following advantages over Quake II remaster:

* No C++, JSON and other stuff that doesn't belong in Quake II engine.
* Smoother player movement and entity animation.
* Vanilla-like server FPS independent view pitching.
* Full-featured client game module for greater modding possibilities.
* Server and client game code fully cross-platform and sandboxed.
* Lower system requirements: OpenGL 3.1 and SSE2 capable CPU.

**This project is work in progress.** Game API, network protocol, savegame
format are not stable yet and may change anytime. You have been warned.
