# Desktop Target (Experimental)

Coi currently ships as a web-first compiler (WASM + WebCC + DOM). This repo now has an **experimental native desktop target** that compiles the generated C++ into a native executable.

## Build

From a Coi project directory:

```bash
coi build --target desktop
./dist/app
```

To see a simple “render” dump of the UI tree on stdout:

```bash
COI_DESKTOP_DUMP=1 ./dist/app
```

## What “desktop” means right now

- Uses the same `view {}` syntax (no separate desktop-only view block).
- Codegen calls a backend-neutral API (`coi::ui::*`) and picks an implementation per target.
- The current desktop backend is a **stub runtime** that builds a retained UI tree in memory; it does not open a window yet.

## Current limitations

- **Web platform APIs are not supported** on `--target desktop` yet (e.g. `System.*`, `Input.*`, `Canvas*`, `Fetch*`, `WebSocket*`, DOM types). The compiler will error if they’re used.
- **Router is not supported** (it depends on browser history/popstate).
- Styling output is still CSS-oriented; the desktop target does not consume `app.css` yet.

## Recommended direction (2B backend abstraction)

The intended architecture is to keep a single `view {}` tree and implement multiple backends:

- **Web backend**: DOM + CSS (current)
- **Desktop backend**: retained tree + layout + renderer + input/event loop

For desktop layout + text:

- **Clay** can be used as a renderer-agnostic layout engine (flexbox-like), given a text-measure callback.
- **Skribidi** can provide shaping + bidi + wrapping + editing primitives and can also serve as Clay’s text measurement source.

The missing pieces to become a real desktop UI are:

1. Window + main loop (SDL2/GLFW/etc.)
2. Input events mapped into Coi’s event dispatch
3. Layout engine (e.g. Clay)
4. Text system (e.g. Skribidi)
5. Renderer (Skia/wgpu/OpenGL/etc.)

