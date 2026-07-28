# RuneTools

A quality-of-life companion overlay for RuneScape 3 (NXT). It shows a live
panel alongside the game (skills, scene, clues, quests, Invention materials,
tile markers and more) and supports third-party panels through an open
plugin SDK.

## How it works

RuneTools is a launcher plus an in-game companion:

- **Launcher** (`RuneToolsXLauncher.exe`) - an Ultralight-based desktop app
  that starts and manages your game clients, hosts the panel UI, reads live
  game state, and keeps itself up to date.
- **Companion** (`rtxscene.dll`) - loaded into the running game; it composites
  the panel into the game's frame and publishes per-frame scene data to the
  launcher over shared memory.
- **Cache reader** - decodes the local RS3 game cache for static data such as
  item names and icons, NPC definitions, map tiles and sprites.
- **Plugin SDK** - third-party panels run in a sandboxed frame with a small,
  stable API. See [PLUGIN_SDK.md](PLUGIN_SDK.md).

## Layout

```
RuneTools/
├── RuneToolsX.sln               Visual Studio solution
├── RuneToolsXLauncher/          Launcher executable project
├── CacheVendor/                 Vendored C dependencies (SQLite, zlib)
├── companion/                   In-game companion module (rtxscene.dll)
├── src/
│   ├── launcher/                Ultralight host, JS<->C++ bridge, account
│   │                            vault, updater, window management
│   ├── reader/                  Live game-state reader
│   ├── cache/                   RS3 cache decoder (items, NPCs, maps, sprites)
│   └── shared/                  Shared helpers (logging, machine key)
├── ui-assets/                   Panel + launcher UI (HTML/CSS/JS) and plugin SDK
├── installer/                   Inno Setup installer script
└── ThirdParty/
    ├── Ultralight/              Ultralight 1.4 HTML UI engine SDK (bundled)
    ├── Detours/                 Microsoft Detours 4.0.1 (bundled)
    └── cs2sidecar/              CS2 extractor sidecar (source included)
```

---

# Building from source

Everything below runs on Windows 11 (or Windows 10) on x64. There is no other
supported target: the companion is injected into the 64-bit NXT client.

## Step 1 - Install Visual Studio and the C++ build tools

1. Download the **Visual Studio 2026** installer (Community edition is enough)
   from <https://visualstudio.microsoft.com/downloads/>.
2. Run it and, on the **Workloads** tab, tick:
   - **Desktop development with C++**
3. If the workload did not already select a **Windows 11 SDK** or **Windows 10
   SDK**, tick one on the **Individual components** tab.
4. Install, then reboot if prompted.

All three projects build with the **v145** toolset, which the C++ workload
installs by default. No extra toolset component is needed.

> On Visual Studio 2022 instead? It does not ship v145. Either install
> Visual Studio 2026, or open the three `.vcxproj` files and set every
> `<PlatformToolset>` to `v143`.

Windows PowerShell is used by a post-build step. It is part of Windows; no
action needed.

## Step 2 - Get the source

```
git clone https://github.com/goldgolem624/RuneTools.git
cd RuneTools
```

Downloading the repository as a ZIP and extracting it works equally well. Avoid
paths with non-ASCII characters.

Every path in the rest of this guide is relative to this folder (the one holding
`RuneToolsX.sln`).

## Step 3 - Build

Both third-party dependencies are already in the clone, so there is nothing
else to download:

| Bundled at | What it is |
|---|---|
| `ThirdParty\Ultralight` | Ultralight 1.4 HTML UI engine (headers, import libs, runtime DLLs, resources) |
| `ThirdParty\Detours` | Microsoft Detours 4.0.1 (headers + `detours.lib`) |

To build against your own copy of either, pass `/p:UltralightDir=<path>` or
`/p:DetoursDir=<path>`, or point the bundled folder at it with a directory
junction.

1. Open **`RuneToolsX.sln`** in Visual Studio.
2. In the toolbar set the configuration to **Release** and the platform to
   **x64**.
3. Choose **Build > Rebuild Solution**. Use *Rebuild Solution*, not *Build*, so
   every project is built in dependency order the first time.

The build takes several minutes. When it succeeds, the Output window reports
that the Ultralight runtime and UI assets were staged, and `x64\Release\`
contains:

| Item | What it is |
|---|---|
| `RuneToolsXLauncher.exe` | the application you run |
| `rtxscene.dll` | companion module, loaded into the game client |
| `Ultralight\` | Ultralight runtime DLLs and their `resources\` |
| `sounds\` | alert sounds |
| `*.html`, `panel_*.js`, `items.pack` | UI assets and data |

Keep this folder intact. Moving `RuneToolsXLauncher.exe` on its own will not
work, because it loads the UI assets and the Ultralight runtime from alongside
itself.

One warning is expected and harmless if you do not have Inno Setup installed:

```
Inno Setup (ISCC.exe) not found -- skipping installer build.
```

That step only produces the redistributable setup executable. The build
succeeds without it. To build the installer as well, install
[Inno Setup 6](https://jrsoftware.org/isdl.php#v6) and rebuild;
`installer\Output\RuneToolsXSetup.exe` will be generated.

## Step 4 - Build the CS2 Scripts panel (optional)

This panel decompiles the game's clientscripts from your own local cache using
a Node.js sidecar. Skip this step if you do not want the panel; everything
else works without it.

1. Install [Node.js](https://nodejs.org) (LTS release) and let the installer
   add it to `PATH`.
2. Open a new terminal in the repository root and run:

```
ThirdParty\cs2sidecar\build.cmd
```

   The first run installs dependencies, which takes a few minutes. It finishes
   with `Done. Output: ...\dist\cs2export.js`.

3. Build the solution again (step 3). The build copies `ThirdParty\cs2sidecar`
   into `x64\Release\cs2sidecar` for you.

Doing this before step 3 means the sidecar is staged by your first build. The
panel invokes `node` from `PATH`, or a `node.exe` placed next to the sidecar.
To keep the sidecar somewhere else, point the `RTX_CS2_SIDECAR` environment
variable at the folder that contains `dist\cs2export.js`.

---

# Running

## Prerequisites

RuneScape 3 must be installed through the **Jagex Launcher**, since RuneTools
reads the local NXT cache and attaches to the `rs2client.exe` it installs.

## Start it

1. Run `x64\Release\RuneToolsXLauncher.exe`.
2. The main view lists your running and saved game clients. Either start a
   client from the launcher, or attach to a client that is already running.
3. The companion loads into that client and the panel appears alongside the
   game.

Because the companion is injected when a client starts, changes to
`rtxscene.dll` take effect on the **next client launch**, not on a launcher
restart.

Logs are written to `%USERPROFILE%\RuneToolsX\logs\` (one launcher log plus one
per game client) and can be opened from the launcher footer. Panel data and
settings live under `%USERPROFILE%\RuneToolsX\`.

The launcher updates itself from the project's public release endpoints.

## If something goes wrong

| Symptom | Cause and fix |
|---|---|
| `cannot open include file: 'Ultralight/Ultralight.h'` | The bundled SDK did not come through. Confirm `ThirdParty/Ultralight/include/Ultralight/Ultralight.h` exists in your clone, then **Rebuild**. |
| `The build tools for v145 cannot be found` | The C++ workload is missing or you are on Visual Studio 2022. Return to step 1. |
| `cannot open include file: 'detours.h'` | The bundled Detours copy is missing. Confirm `ThirdParty/Detours/include/detours.h` exists, or pass `/p:DetoursDir=<path>` to your own build. |
| `LNK1104: cannot open file 'Ultralight.lib'` or `'detours.lib'` | The matching `lib` folder is missing from the clone. A partial download or an aggressive antivirus can strip these binaries. |
| `LNK1181: cannot open input file 'CacheVendor.lib'` | Build the whole solution rather than a single project: **Build > Rebuild Solution** builds CacheVendor first. |
| Installer step: `Could not read license file` | The SDK's `license\` folder did not stage. Confirm `ThirdParty\Ultralight\license\EULA.txt` exists, then **Rebuild** so it is copied to `x64\Release\Ultralight\license\`. |
| Launcher starts but the window is blank | The Ultralight runtime did not stage. Rebuild `Release\|x64` and confirm `x64\Release\Ultralight\` holds the four DLLs plus `resources\`. |
| Panel does not appear in game | Confirm the client was started or attached from the launcher, then check the per-client log. |
| CS2 panel reports `sidecar not found` | Step 4 was skipped, or the folder is not next to the executable and `RTX_CS2_SIDECAR` is unset. |

---

## Plugins

RuneTools supports third-party panels. The SDK, manifest format and review
process are documented in [PLUGIN_SDK.md](PLUGIN_SDK.md).

## License

RuneTools is released under the Apache License 2.0 (see [LICENSE](LICENSE) and
[NOTICE](NOTICE)). Bundled
and build-time third-party components are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
