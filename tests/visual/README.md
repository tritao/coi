# Visual tests (native + web)

This folder contains shared tooling for COI visual testing:

- **Web visual gallery** (Playwright screenshot capture for manual review)
- **Web integration tests** (Playwright scripted interactions + assertions)
- **Cross-backend visual regression** (native + web PNG capture + dHash compare)

## Layout

- `scenes_manifest.txt`: list of named scenes and which backends they run on.
- `baseline/native/`: native PNG + `.dhash` baselines.
- `baseline/web/`: web PNG + `.dhash` baselines.
- `dhash_png.py`: helper to compute / compare dHash from a PNG.
- `gen_web_visual_js.py`: generates `coi_visual.js` to replay `.native_script` input on the web backend.

## Setup (web-only)

Only required for Playwright-based web runners (`run_web_visual.sh`, `run_web_integration.sh`, and `run_visual.sh --backend web`):

```bash
cd tests/visual
npm install
```

`playwright-core` is used with your system Chrome by default.
Set `WEB_BROWSER=/path/to/chrome` if needed.

## Web gallery (manual inspection)

```bash
./tests/run_web_visual.sh --open
./tests/run_web_visual.sh --scene input_* --out /tmp/coi-web-visual --open
```

## Web integration tests (Playwright)

Per-scene tests live next to the `.coi` file as `*.web_test.mjs` (ESM):

```js
export async function run({ page, expect }) { /* ... */ }
```

Run:

```bash
./tests/run_web_integration.sh --scene input_*
./tests/run_web_integration.sh --list
```

## Cross-backend visual regression (native + web)

Native:

```bash
./tests/run_visual.sh --backend native
./tests/run_visual.sh --backend native --update
```

Web (headless Chrome):

```bash
./tests/run_visual.sh --backend web --scene paint_rects --update
./tests/run_visual.sh --backend web --scene paint_rects
```

Notes:
- Web tests default to `--web-driver playwright` (using `playwright-core` + the system `google-chrome`).
- Override browser path with `--browser /path/to/chrome` or `WEB_BROWSER=/path/to/chrome`.
- Fallback driver (legacy): `./tests/run_visual.sh --backend web --web-driver chrome ...`
- Add `--open` to serve and open an HTML view of the capture output.

## Native vs web gallery (manual inspection)

Generate per-scene outputs for both backends into a scratch folder and write an `index.html`
that shows native/web images side-by-side:

```bash
./tests/visual/gallery.sh --scene paint_rects
./tests/visual/gallery.sh --scene layout_* --out /tmp/coi-gallery
./tests/visual/gallery.sh --set golden --out /tmp/coi-golden
./tests/visual/gallery.sh --set golden --out /tmp/coi-golden --open
```

