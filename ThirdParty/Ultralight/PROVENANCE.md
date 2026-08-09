# Ultralight binaries - provenance

These four DLLs (~44 MB) are prebuilt and enter the launcher's process, so "trust us" is not
good enough. This file records exactly what is committed here so anyone can check the bytes
against the upstream SDK themselves, without taking our word for it.

Upstream ships these binaries **unsigned** (Authenticode `NotSigned`, no version metadata). That
is normal for the Ultralight SDK and is not by itself a red flag, but it does mean a signature
cannot tell you where a given copy came from. A hash can.

## What is committed

| File | Bytes | SHA-256 |
|---|---:|---|
| `bin/AppCore.dll` | 333,312 | `3e3cac95297470699811070fc0248712aeb3dc3cc3788d8347bab42ce76ea239` |
| `bin/Ultralight.dll` | 557,568 | `9c319e18b2f69cf12e2bf992b076079a6e4ffae482634dff3f61343d2c3919a7` |
| `bin/UltralightCore.dll` | 2,554,880 | `ac782df832b528e030a1e8ae25ce6c66abc52888a0fbeff6191e0d76c21edfff` |
| `bin/WebCore.dll` | 42,222,592 | `d62ecca4226f5ecbeb5dc13fa095b48be7336304fc24376da9329b906448436e` |
| `lib/AppCore.lib` | 53,258 | `712311d7897625f1c759c859068a0399289dd3c758720d8e59fb90d1dac5559f` |
| `lib/Ultralight.lib` | 115,358 | `98a53fea99ced5a59159e02bade7c5141dae81f9445fd85750b2c8d479671e3c` |
| `lib/UltralightCore.lib` | 128,578 | `0681ad49caaf5595839c10d4b6488e841a5318333e024c0213222a26c8208eb8` |
| `lib/WebCore.lib` | 5,022,386 | `cb4afeed5b3efa80f80bbc899bd233b6d98a2a950c44f2fb29346a520c717e8c` |

Claimed origin: Ultralight 1.4 SDK, Windows x64.

## Verifying your copy matches this file

```powershell
Get-ChildItem -Recurse -Include *.dll,*.lib |
    ForEach-Object { '{0}  {1}' -f (Get-FileHash $_ -Algorithm SHA256).Hash.ToLower(), $_.Name }
```

## Verifying this file matches upstream

The table above proves only that your checkout is unmodified. To confirm these are genuinely the
upstream binaries, download the Ultralight 1.4 Windows x64 SDK from
<https://ultralig.ht> (or the GitHub release for that tag), hash the `bin/` and `lib/` contents
the same way, and compare. A mismatch on any row means this checkout is NOT stock upstream and
should be treated as unexplained until someone explains it.

That comparison is deliberately left to the reader: a hash we generated and a hash we verified
against ourselves would be circular.

## Why they are not signed by us

`installer/sign-and-build.ps1 -IncludeUltralight` will Authenticode-sign these with our own
code-signing certificate when the build is run that way. That makes the shipped copies tamper
evident *after* our build, which is worth doing, but it says nothing about their origin, since
we would be attesting to bytes we did not produce. The hash comparison above is the part that
actually establishes provenance.
