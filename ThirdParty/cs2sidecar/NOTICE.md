# CS2 extractor sidecar

The "CS2 Scripts" panel decompiles the game's clientscripts from your own local
RuneScape cache. That work happens in this sidecar, which RuneToolsX launches as a
**separate process** (`node.exe dist\cs2export.js <outdir>`); it is not linked into
RuneToolsX.

## Upstream project and licence

- Based on **rsmv** (RuneApps model viewer) by **skillbert** —
  https://github.com/skillbert/rsmv
- Licence: **GPL-3.0**

This sidecar is a modified version of that project and remains under the GPL-3.0.
The complete corresponding source for the binaries in `dist\` is bundled in `src\`.

## Modifications by RuneTools

- Added `src/headless/cs2export.ts`: a full-cache extraction entry point that
  decompiles every js5-12 clientscript, applies ground-truth identifier renames
  (quest progress trackers, npc/loc morph vars, clientscript switch tables,
  var-reference params, achievement requirements) and inserts ground-truth
  annotations (typed cast names, enum values, db column types, packed interface
  component references, db query comparison operators). Writes the progress and
  metadata files the panel polls.
- Clientscript decompiler corrections in `src/clientscript/`:
  - Opcode signatures pinned from the game client's own opcode registry, applied
    after the reference calibration and gated on that calibration still matching
    (`generated_opsignatures.ts` and the applier in `callibrator.ts`).
  - Named previously unknown opcodes: the native text-input family, long-integer
    minimum, the 64-bit stockmarket price getter, and the database query filter
    family (`DBQUERY_NOT`, `DBQUERY_AND`, `DBQUERY_FILTER_FIELD_OP`) with its
    comparison-operator enum.
  - Fixed multi-value assignment argument order, constant tracking across opcodes
    with unresolved stack effects, argument capping for interleaved call
    arguments, and the stack effect of the database row iterator.
  - Calibration results are cached to disk so repeat runs skip re-calibration.
- Trimmed the build to the two entry points this sidecar uses; upstream's model
  viewer, avatar, texture and probe entries are not built, and one upstream
  developer probe script is not shipped.

## Building

Requires Node.js (https://nodejs.org).

    ThirdParty\cs2sidecar\build.cmd

It installs dependencies into `src\node_modules` on first run and writes
`dist\cs2export.js`. The launcher finds the sidecar via the `RTX_CS2_SIDECAR`
environment variable, otherwise in a `cs2sidecar` folder next to the executable.

## Other bundled components

- `node.exe` — unmodified Node.js runtime (https://nodejs.org, MIT-style licence),
  version-matched to the prebuilt native modules.
- `src/node_modules/` (created by the build) — the dependency closure, each package
  under its own licence; see each package's own LICENSE file.

## What it reads and writes

Reads the local RuneScape NXT cache (`C:\ProgramData\Jagex\RuneScape`) and writes
its output to `%USERPROFILE%\RuneToolsX\cs2`. It makes no network requests.
