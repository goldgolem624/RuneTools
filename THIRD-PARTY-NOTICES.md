# Third-Party Notices

RuneTools is licensed under the MIT License (see [LICENSE](LICENSE)). It
bundles or depends on the third-party components below, each of which remains
the property of its respective authors and is used under its own license.

---

## SQLite

- Bundled at: `src/cache/vendor/sqlite`
- License: **Public Domain**

SQLite is in the public domain. The authors disclaim copyright; see
https://www.sqlite.org/copyright.html. As the SQLite blessing puts it:

> May you do good and not evil. May you find forgiveness for yourself and
> forgive others. May you share freely, never taking more than you give.

---

## zlib

- Bundled at: `src/cache/vendor/zlib`
- Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler
- License: **zlib License**

> This software is provided 'as-is', without any express or implied warranty.
> In no event will the authors be held liable for any damages arising from the
> use of this software.
>
> Permission is granted to anyone to use this software for any purpose,
> including commercial applications, and to alter it and redistribute it
> freely, subject to the following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not claim
>    that you wrote the original software. If you use this software in a
>    product, an acknowledgment in the product documentation would be
>    appreciated but is not required.
> 2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This notice may not be removed or altered from any source distribution.

---

## Ultralight

- Bundled at: `ThirdParty\Ultralight`
- Engine: Ultralight HTML UI engine, (c) 2024 Ultralight, Inc. All rights
  reserved. Ultralight is a trademark of Ultralight, Inc. - https://ultralig.ht
- License: Ultralight Free License Agreement, in the SDK's own `license/` folder
  (`LICENSE.txt`, `EULA.txt`, `NOTICES.md`)
- Includes portions of WebKit and other third-party software; the SDK's
  `NOTICES.md` lists them and their licenses.

> Please see the accompanying NOTICES.txt for full text.

The SDK is bundled so the solution builds from a fresh clone. Built releases
ship the Ultralight runtime DLLs, as the SDK license permits for a product that
embeds the engine. The build stages the SDK's `license/` folder next to the
runtime at `Ultralight\license\`, and the installer presents Ultralight's end-user
agreement for acceptance. Read that license before redistributing the SDK itself
rather than an application built on it.

---

## Microsoft Detours

- Bundled at: `ThirdParty\Detours`
- Project: Microsoft Research Detours Package, Version 4.0.1 -
  https://github.com/microsoft/Detours
- Copyright (c) Microsoft Corporation. All rights reserved.
- License: **MIT License** (reproduced in `ThirdParty\Detours\LICENSE.md`)

The in-game companion uses Detours to hook the game client's render and input
entry points. Headers and the x64 `detours.lib` are bundled; the library is
linked into `rtxscene.dll`.

---

## CS2 extractor sidecar (separate process)

- Based on rsmv (RuneApps model viewer) by skillbert - https://github.com/skillbert/rsmv
- License: GPL-3.0

`ThirdParty\cs2sidecar` is a modified version of that project. The "CS2 Scripts"
panel runs it as a **separate process** to decompile clientscripts from the
user's own cache; it is not linked into RuneToolsX. Its complete corresponding
source is included in `ThirdParty\cs2sidecar\src`. See
`ThirdParty\cs2sidecar\NOTICE.md` for the modifications, build instructions and
dependency licences.

---

## RuneScape Wiki quest + clue data

- Bundled at: `ui-assets/quest_guides.js` (quest quick guides), the per-quest
  `ui-assets/panel_*.js` guides, and the clue data in `ui-assets/panel_lodestones.js`
  and `ui-assets/panel_clueguide.js` (emote, cryptic and talk-to-NPC solutions,
  skill-riddle methods and level requirements)
- Source: RuneScape Wiki "Quick guide" and "Treasure Trails/Guide" pages - https://runescape.wiki
- License: Creative Commons Attribution-NonCommercial-ShareAlike 3.0 (CC BY-NC-SA 3.0)

The quest quick-guide step data and the Treasure Trail clue solutions are derived
from RuneScape Wiki content and used under CC BY-NC-SA 3.0, with attribution. That
license (non-commercial, share-alike) applies to this wiki-sourced data specifically
and is independent of the project's MIT license.
