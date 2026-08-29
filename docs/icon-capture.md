# Icon capture from the GPU atlas: rejected

## What was tried

The client keeps no CPU bitmaps for item icons: it renders them into a GPU texture
atlas and the CPU side only holds the bookkeeping (graphic key -> cell rectangle).
The plan was to read that atlas back in the companion (`glGetTexImage`, with an FBO
`glReadPixels` fallback), pair the pixels with the reader's cell map, and slice one PNG
per item id. All of it is now deleted.

## Why it cannot work as built

- Exactly one 1536x1536 `GL_TEXTURE_2D` exists in the process: GL name 34. No other
  texture in the process has the atlas size on either the 2D or the rectangle target.
- Sliced against the rectangles the bank container itself maps, name 34 scores 0.412
  similarity to the matching `items.pack` icons. That is far below a match.
- An exhaustive search over name 34 does find the pack's icons inside it, but at
  positions unrelated to the container's rectangles, with mean similarity 0.845. Every
  rectangle the container maps is empty in that texture.
- The container's texture-owner object graph does not contain GL name 34 at all.

Taken together: name 34 belongs to a different cache instance than the container the
reader walks. There is no atlas in the process whose contents line up with the cell map,
so the readback has nothing correct to slice.

## What replaced it

Icons are rendered offline from the game cache into
`%USERPROFILE%\RuneToolsX\icons\<clientVersion>\<id>.png`. `src/launcher/IconCache.cpp`
serves that directory before `items.pack`, and only once the directory holds a
`.validated` marker file. The Health panel's "Item icons" card reports how many are
rendered and which ids are still missing.
