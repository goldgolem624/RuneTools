# Third-party dependencies

These ship with the repository so the solution builds from a fresh clone with
no extra downloads. Licensing for each is in
[../THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).

| Folder | Component |
|---|---|
| `Ultralight/` | [Ultralight](https://ultralig.ht) 1.4 HTML UI engine SDK. Its own terms are in `Ultralight/license/`. |
| `Detours/` | [Microsoft Detours](https://github.com/microsoft/Detours) 4.0.1 (MIT), used by the in-game companion to hook the client. |
| `cs2sidecar/` | CS2 extractor sidecar (GPL-3.0), run as a separate process. See [cs2sidecar/NOTICE.md](cs2sidecar/NOTICE.md). |

To build against your own copy of Ultralight or Detours instead of the bundled
one, pass `/p:UltralightDir=<path>` or `/p:DetoursDir=<path>` to the build, or
point the folder at it with a directory junction.

The sidecar is built separately with `cs2sidecar\build.cmd`; Node.js is required
and is not redistributed here.
