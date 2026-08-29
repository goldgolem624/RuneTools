# Live item icon capture

Item icons that are missing from `items.pack` are captured from the running client.
The client does not keep item icons as CPU bitmaps: it renders them into one
1536x1536 GPU texture atlas (36x32 cells) and the CPU side only holds the
bookkeeping (graphic key -> atlas cell rectangle). So the capture has two halves:

1. The reader (`rtx::reader::ReadIconAtlasMap`, `src/reader/Reader.cpp`) walks the
   client's bookkeeping and produces `{key, item, flavour, cell, x, y, w, h}` for every
   occupied atlas cell.
2. The companion (`companion/Present.cpp`, inside the `wglSwapBuffers` hook where the
   game's GL context is current) reads the atlas texture back with `glGetTexImage`
   into the `IconAtlasShare` section (`companion/IconAtlasShare.h`).

The launcher (`src/launcher/IconCapture.cpp`) joins the two, slices the rects and writes

    %USERPROFILE%\RuneToolsX\icons\<clientVersion>\<itemId>.png        flavour 0x02, 36x32
    %USERPROFILE%\RuneToolsX\icons\<clientVersion>\<itemId>_f<xx>.png  any other flavour (hex)

Existing files are never overwritten. `IconCache::ItemIconDataUrl` looks in the newest
version directory before `items.pack`; a miss on both schedules a background capture
(at most one every 10 s while a client is attached). The Health panel shows
"Captured this session", the last status and a "Capture icons now" button
(bridge `iconCapture(pid)`, `iconCaptureStatus()`; broker `host.iconCapture`,
`host.iconCaptureStatus`).

## Verified client layout (build 949-5, 2026-08-29)

Proof scripts: scratch `proof1.py` / `proof2.py` (attach with `mem.py`).

    widget item cell node   +0x188  u64 key  = 0x4000000000000000 | flavour<<24 | itemId
                            +0x190  ptr      -> icon cache node
                            +0x1a0  i32 itemId, +0x1a8 i32 amount
    icon cache node (0x48)  +0x20  u64 key
                            +0x28  ptr container
                            +0x38  u32 cellId
    container               +0x08  u32 count
                            +0x20  ptr GPU texture owner (passed to the hook as hintTexOwner)
                            +0x28  u32 atlas width (1536), +0x2c u32 atlas height (1536)
                            +0x34  u32 next cell id
                            +0x38/+0x40/+0x48  free-rect vector (not used)
                            +0x50/+0x58/+0x60  packer tree header; +0x60 = ROOT node.
                                   The root's parent (+0x10) points back at container+0x50
                                   (the sentinel), which is how the header was identified.
                            +0x98/+0xa0/+0xa8  hash table begin/end/cap:
                                   1537 x {u64 key, u32 cellId, u32 tag}; tag 9 = occupied
    packer node             +0x00 childA, +0x08 childB, +0x10 parent
                            +0x20 u64 cellId
                            +0x28 u32 x, +0x2c u32 y, +0x30 u32 w, +0x34 u32 h

Flavour byte (key bits 24..31): 0x02 = 36x32 item icon, 0x00 = plain sprite id sharing
the same cache, 0x12 = 63x56 grid cell (POH rework). Always use the packer node's w/h.

Live proof output (`proof2.py`):

    +60: top 1fa28141840 nodes 1537 leaves-with-cell 1537
       cell 5d8 -> (792, 1296, 36, 32)      item 37773
       cell 33c -> (1224, 368, 36, 32)      item 55488
       cell 3b2 -> (432, 944, 36, 32)       item 31877
    hash slots 1537, used 1519
      key 400000000200938d -> cell 0x5d8
      key 400000000200d8c0 -> cell 0x33c
      key 4000000002007c85 -> cell 0x3b2
    flavours {0: 107, 2: 1410, 4: 2}

The three rects match the ones the earlier heap scan found independently, and every
on-screen widget key (backpack 1473 comp 5, equipment 1464 comp 15) resolves to a rect
(`widgetKeys` / `widgetKeysMapped` in `IconAtlasMapJson`).

## Share protocol (`Local\RuneToolsXIconAtlas_v1_<pid>`)

    magic 'ICAT', version 1, pid
    request       launcher writes 1; the hook clears it after the attempt
    seq           hook bumps it once per attempt (the launcher waits on this, 2 s)
    width/height  1536x1536 on success
    glName        GL texture name used (cached in the hook; size re-validated each capture)
    status        0 ok, 1 no texture, 2 gl error (glError holds the code)
    hintTexOwner  launcher -> hook: container+0x20 object; the hook scans its first 0x200
                  bytes for a u32 that glIsTexture accepts with a 1536x1536 level 0
    hintHit       1 = name came from the hint, 2 = from the 1..65535 probe
    pixels        RGBA8, top-down (the hook flips GL's bottom-up rows)

The hook path is one load (`request`) per present while idle. During a capture it
saves and restores the active texture unit, the 2D texture binding, the pixel-pack
buffer binding and all `GL_PACK_*` state; the only stall is the readback itself.

## Live test

1. Start the launcher with the companion loaded, log in, open the bank (the atlas
   then holds every icon of every item shown; the map needs at least one item cell on
   screen, backpack or equipment is enough).
2. Developer > Health check > Run check > "Capture icons now".
3. Expect "Last capture: ok (N new of M cells)" with M around 1400 to 1540 and
   `%USERPROFILE%\RuneToolsX\icons\<version>\` holding N PNG files (36x32 RGBA).
   A second run reports 0 new (files are skipped).
4. Debug view shows `RuneToolsX: icon capture: N new of M cells (hint 1, tex <name>)`.
   `hint 2` means the texture-owner scan did not find the name and the probe did;
   status "no atlas texture found" means neither did and the atlas format changed.
5. Any panel rendering an item that was missing from the pack now shows the captured
   icon without a restart (`IconCache` drops its memo for the captured ids).
