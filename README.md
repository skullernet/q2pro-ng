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

**This project is work in progress and alpha quality.** Expect frequent game API
breakage, incompatible network protocol changes, savegame format changes, etc.
