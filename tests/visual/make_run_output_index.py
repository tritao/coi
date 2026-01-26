#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
from pathlib import Path


def find_frames(dir_path: Path) -> list[Path]:
    if not dir_path.exists() or not dir_path.is_dir():
        return []
    return sorted(dir_path.glob("frame_*.png"))


def find_scene_dirs(root: Path) -> list[Path]:
    if not root.exists() or not root.is_dir():
        return []
    return sorted([p for p in root.iterdir() if p.is_dir()])


def rel(from_dir: Path, to_file: Path) -> str:
    try:
        return to_file.relative_to(from_dir).as_posix()
    except Exception:
        return to_file.as_posix()


def render_scene(out_dir: Path, scene_name: str, frames: list[Path]) -> str:
    rows: list[str] = []
    rows.append(f"<h2 id='{html.escape(scene_name)}'>{html.escape(scene_name)}</h2>")
    if not frames:
        rows.append("<div class='empty'>no frames</div>")
        return "\n".join(rows)
    rows.append("<div class='grid'>")
    for f in frames:
        rows.append("<div class='card'>")
        rows.append(f"<div class='hdr'>{html.escape(f.name)}</div>")
        rows.append(f"<img loading='lazy' src='{html.escape(rel(out_dir, f))}' />")
        rows.append("</div>")
    rows.append("</div>")
    return "\n".join(rows)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True, help="Capture root (scene dir or parent dir)")
    ap.add_argument("--out", type=Path, default=None, help="Output HTML path (default: <root>/index.html)")
    ap.add_argument("--title", type=str, default="COI Visual Output")
    args = ap.parse_args()

    root = args.root
    out_path = args.out or (root / "index.html")
    out_dir = out_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    # If root contains frame_*.png, treat it as a single scene dir.
    frames_here = find_frames(root)
    sections: list[str] = []
    toc: list[str] = []

    if frames_here:
        sections.append(render_scene(out_dir, root.name, frames_here))
        toc.append(f"<li><a href='#{html.escape(root.name)}'>{html.escape(root.name)}</a></li>")
    else:
        for scene_dir in find_scene_dirs(root):
            frames = find_frames(scene_dir)
            if not frames:
                continue
            sections.append(render_scene(out_dir, scene_dir.name, frames))
            toc.append(f"<li><a href='#{html.escape(scene_dir.name)}'>{html.escape(scene_dir.name)}</a></li>")

    toc_html = "<ul>" + "\n".join(toc) + "</ul>" if toc else ""
    body_html = "\n".join(sections) if sections else "<p>No frames found.</p>"

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
      font-family: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Helvetica, Arial;
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
    header .path {{
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
    .grid {{
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(420px, 1fr));
      gap: 10px;
      border: 1px solid var(--border);
      background: var(--card);
      border-radius: 12px;
      padding: 10px;
    }}
    .card {{
      border: 1px solid var(--border);
      border-radius: 10px;
      overflow: hidden;
      background: #0f1422;
    }}
    .hdr {{
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
    .empty {{
      color: var(--muted);
      padding: 12px;
    }}
  </style>
</head>
<body>
  <header>
    <div><strong>{html.escape(args.title)}</strong></div>
    <div class="path">{html.escape(str(root))}</div>
  </header>
  <main>
    {"<nav><div style='margin-bottom:8px;color:var(--muted);font-size:12px;'>Scenes</div>" + toc_html + "</nav>" if toc_html else ""}
    {body_html}
  </main>
</body>
</html>
""",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

