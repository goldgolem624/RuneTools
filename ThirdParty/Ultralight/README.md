# Ultralight 1.4 (free-tier SDK)

The Ultralight SDK is the in-game UI engine; see
[`docs/adr/0001-ultralight-as-ui.md`](../../../docs/adr/0001-ultralight-as-ui.md)
for the why.

**The SDK itself is not checked in.** It ships out-of-band:

1. Register at https://ultralig.ht/ and download the **1.4.x SDK for
   Windows x64**.
2. Unpack so this directory ends up containing the standard layout:
   ```
   Ultralight/
   ├── bin/      AppCore.dll, Ultralight.dll, UltralightCore.dll,
   │             WebCore.dll, libEGL.dll, libGLESv2.dll, ...
   ├── include/  Ultralight/, AppCore/ headers
   ├── lib/      .lib import libs
   └── resources/ icudt67l.dat, cacert.pem (required at runtime)
   ```
3. Verify by running RuneTools' Ultralight panel as a sanity check
   if you're new to integrating it — the SDK behaves identically
   across consumers.

## Versioning

We pin to **1.4.x**. Newer majors may break the OpenGL render-
interface contract; older majors miss CSS features we depend on.
Re-test before bumping.

## License compliance

Free-tier requires attribution. The UI exposes "Powered by Ultralight"
in the about / settings area. Do not modify the binaries. If we ever
distribute publicly under terms that violate the free-tier limits,
the project owner is responsible for upgrading the license.

## Why this directory is gitignored

* Vendored binaries bloat the repo.
* The download is gated by a free-tier signup; we don't redistribute
  the SDK files.
* Anyone building from source follows the steps above. Anyone running
  a release build uses the binaries we ship in `OSTools.dll` (which
  statically links against `Ultralight.lib`) plus the resources/
  payload from this directory.
