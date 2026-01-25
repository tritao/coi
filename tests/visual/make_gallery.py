#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
from pathlib import Path


def find_scenes(root: Path) -> set[str]:
    if not root.exists():
        return set()
    out: set[str] = set()
    for p in root.iterdir():
        if p.is_dir():
            out.add(p.name)
    return out


def find_frames(scene_dir: Path) -> list[str]:
    if not scene_dir.exists():
        return []
    frames = []
    for p in scene_dir.glob("frame_*.png"):
        frames.append(p.name)
    return sorted(frames)


def relpath(from_dir: Path, to_file: Path) -> str:
    try:
        return to_file.relative_to(from_dir).as_posix()
    except Exception:
        return to_file.as_posix()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--desktop", type=Path, required=True, help="Directory containing desktop scene subdirs")
    ap.add_argument("--web", type=Path, required=True, help="Directory containing web scene subdirs")
    ap.add_argument("--out", type=Path, required=True, help="Output HTML path")
    ap.add_argument("--title", type=str, default="COI Visual Gallery")
    args = ap.parse_args()

    desktop_root = args.desktop
    web_root = args.web
    out_path = args.out
    out_dir = out_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    scenes = sorted(find_scenes(desktop_root) | find_scenes(web_root))

    rows: list[str] = []
    for scene in scenes:
        ddir = desktop_root / scene
        wdir = web_root / scene
        frames = sorted(set(find_frames(ddir)) | set(find_frames(wdir)))
        if not frames:
            continue

        rows.append(f'<h2 id="{html.escape(scene)}">{html.escape(scene)}</h2>')
        rows.append("<div class='scene'>")
        for frame in frames:
            dfile = ddir / frame
            wfile = wdir / frame
            rows.append("<div class='frame'>")
            rows.append(f"<div class='framehdr'>{html.escape(frame)}</div>")
            rows.append("<div class='cols'>")

            if dfile.exists():
                src = relpath(out_dir, dfile)
                rows.append(
                    "<div class='col'>"
                    "<div class='label'>desktop</div>"
                    f"<img loading='lazy' src='{html.escape(src)}' />"
                    "</div>"
                )
            else:
                rows.append("<div class='col missing'><div class='label'>desktop</div><div class='miss'>missing</div></div>")

            if wfile.exists():
                src = relpath(out_dir, wfile)
                rows.append(
                    "<div class='col'>"
                    "<div class='label'>web</div>"
                    f"<img loading='lazy' src='{html.escape(src)}' />"
                    "</div>"
                )
            else:
                rows.append("<div class='col missing'><div class='label'>web</div><div class='miss'>missing</div></div>")

            rows.append("</div>")
            rows.append("</div>")
        rows.append("</div>")

    toc = "\n".join(f"<li><a href='#{html.escape(s)}'>{html.escape(s)}</a></li>" for s in scenes)
    body = "\n".join(rows) if rows else "<p>No scenes found.</p>"

    out_path.write_text(
        f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>{html.escape(args.title)}</title>
  <style>
    :root {{
      --bg: #0b0e14;
      --fg: #e6e6e6;
      --muted: #a8b3cf;
      --card: #121827;
      --border: #27304a;
    }}
    body {{
      margin: 0;
      font-family: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Helvetica, Arial, "Apple Color Emoji", "Segoe UI Emoji";
      background: var(--bg);
      color: var(--fg);
    }}
    header {{
      position: sticky;
      top: 0;
      background: rgba(11, 14, 20, 0.92);
      backdrop-filter: blur(6px);
      border-bottom: 1px solid var(--border);
      padding: 12px 16px;
      z-index: 10;
    }}
    header .paths {{
      color: var(--muted);
      font-size: 12px;
      margin-top: 4px;
    }}
    main {{
      padding: 16px;
      max-width: 1400px;
      margin: 0 auto;
    }}
    nav {{
      border: 1px solid var(--border);
      background: var(--card);
      border-radius: 10px;
      padding: 12px 14px;
      margin-bottom: 18px;
    }}
    nav ul {{
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
      gap: 6px 14px;
      list-style: none;
      padding: 0;
      margin: 0;
    }}
    nav a {{
      color: var(--muted);
      text-decoration: none;
    }}
    nav a:hover {{
      color: var(--fg);
      text-decoration: underline;
    }}
    h2 {{
      margin: 24px 0 10px;
      font-size: 18px;
      font-weight: 600;
    }}
    .scene {{
      border: 1px solid var(--border);
      background: var(--card);
      border-radius: 12px;
      padding: 10px;
    }}
    .frame {{
      border-top: 1px solid var(--border);
      padding-top: 10px;
      margin-top: 10px;
    }}
    .frame:first-child {{
      border-top: 0;
      padding-top: 0;
      margin-top: 0;
    }}
    .framehdr {{
      color: var(--muted);
      font-size: 12px;
      margin-bottom: 8px;
    }}
    .cols {{
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }}
    .col {{
      border: 1px solid var(--border);
      border-radius: 10px;
      overflow: hidden;
      background: #0f1422;
    }}
    .label {{
      font-size: 12px;
      padding: 8px 10px;
      color: var(--muted);
      border-bottom: 1px solid var(--border);
    }}
    img {{
      display: block;
      width: 100%;
      height: auto;
      background: #0b0e14;
    }}
    .missing .miss {{
      padding: 18px 10px;
      color: #ff9da4;
      font-size: 13px;
    }}
  </style>
</head>
<body>
  <header>
    <div><strong>{html.escape(args.title)}</strong></div>
    <div class="paths">desktop: {html.escape(str(desktop_root))} · web: {html.escape(str(web_root))}</div>
  </header>
  <main>
    <nav>
      <div style="margin-bottom:8px;color:var(--muted);font-size:12px;">Scenes</div>
      <ul>{toc}</ul>
    </nav>
    {body}
  </main>
</body>
</html>
""",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

