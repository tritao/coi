#!/usr/bin/env node
import fs from "node:fs/promises";
import path from "node:path";
import process from "node:process";

function parseSize(size) {
  const m = /^(\d+)x(\d+)$/.exec(String(size || "").trim());
  if (!m) throw new Error(`invalid --size '${size}' (expected WxH)`);
  return { width: parseInt(m[1], 10), height: parseInt(m[2], 10) };
}

function getArg(name, def = null) {
  const idx = process.argv.indexOf(name);
  if (idx === -1) return def;
  return process.argv[idx + 1] ?? def;
}

async function main() {
  const url = getArg("--url");
  const out = getArg("--out");
  const size = getArg("--size", "960x540");
  const timeoutMs = parseInt(getArg("--timeout-ms", "12000"), 10);
  const browserPath = getArg("--browser", "");

  if (!url || !out) {
    console.error("usage: web_capture_playwright.mjs --url <url> --out <png> [--size WxH] [--timeout-ms N] [--browser <path>]");
    process.exit(2);
  }

  const { width, height } = parseSize(size);

  const { chromium } = await import("playwright-core");

  const browser = await chromium.launch({
    headless: true,
    executablePath: browserPath || undefined,
    args: ["--no-sandbox", "--disable-dev-shm-usage", "--hide-scrollbars", `--window-size=${width},${height}`],
  });

  const ctx = await browser.newContext({
    viewport: { width, height },
    deviceScaleFactor: 1,
  });

  const page = await ctx.newPage();
  page.setDefaultTimeout(timeoutMs);

  await page.goto(url, { waitUntil: "load" });

  // Wait for COI app to mount (or for the injected script to mark itself done).
  await page.waitForFunction(() => {
    // eslint-disable-next-line no-undef
    if (window.__coi_visual_done === true) return true;
    // eslint-disable-next-line no-undef
    if (window.__coi_visual_ready === true) return true;
    const root = document.querySelector(".root");
    if (root) return true;
    const kids = document.body ? document.body.children : [];
    for (const el of kids) {
      if (el && el.tagName && el.tagName.toUpperCase() !== "SCRIPT") return true;
    }
    return false;
  });

  // If this capture includes scripted input, wait until it finishes.
  const needDone = await page.evaluate(() => {
    // eslint-disable-next-line no-undef
    return typeof window.__coi_visual_capture_index === "number" && window.__coi_visual_capture_index > 0;
  });
  if (needDone) {
    await page.waitForFunction(() => {
      // eslint-disable-next-line no-undef
      return window.__coi_visual_done === true;
    });
  }

  // One extra frame for the final DOM/paint commit.
  await page.evaluate(
    () =>
      new Promise((resolve) => {
        requestAnimationFrame(() => requestAnimationFrame(resolve));
      }),
  );

  await fs.mkdir(path.dirname(out), { recursive: true });
  await page.screenshot({ path: out });

  await page.close();
  await ctx.close();
  await browser.close();
}

await main().catch((err) => {
  console.error(err?.stack || String(err));
  process.exit(1);
});

