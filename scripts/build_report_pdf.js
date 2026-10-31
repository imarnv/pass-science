// Renders docs/REPORT.md to docs/REPORT.pdf (A4) with pandoc + headless Chromium.
// Development-only helper; requires Playwright and pandoc on PATH.
// Usage: node scripts/build_report_pdf.js
const { chromium } = require("playwright");
const { execFileSync } = require("child_process");
const path = require("path");

const root = path.resolve(__dirname, "..");
const body = execFileSync("pandoc", [path.join(root, "docs", "REPORT.md"), "-f", "gfm", "-t", "html"], { encoding: "utf8" });

const css = `
  @page { size: A4; margin: 15mm 16mm; }
  body { font: 9.6pt/1.4 "Segoe UI", "Helvetica Neue", Helvetica, Arial, sans-serif; color: #1d1d1f; }
  h1 { font-size: 17pt; margin: 0 0 4pt; letter-spacing: -0.02em; }
  h2 { font-size: 12pt; margin: 12pt 0 5pt; padding-top: 6pt; border-top: 1px solid #d2d2d7; }
  p { margin: 0 0 5pt; } ul, ol { margin: 0 0 5pt; padding-left: 16pt; } li { margin: 1pt 0; }
  table { border-collapse: collapse; margin: 4pt 0 7pt; font-size: 8.6pt; width: 100%; }
  th, td { border-bottom: 1px solid #e5e5ea; padding: 2.5pt 5pt; text-align: left; }
  th { color: #6e6e73; font-weight: 600; border-bottom-color: #c7c7cc; }
  code { font: 8.4pt Consolas, Menlo, monospace; background: #f2f2f5; padding: 0 2pt; border-radius: 3px; }
  em { color: #6e6e73; } a { color: #0066cc; text-decoration: none; }
`;

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.setContent(`<!doctype html><meta charset="utf-8"><style>${css}</style>${body}`);
  const out = path.join(root, "docs", "REPORT.pdf");
  await page.pdf({
    path: out, format: "A4", printBackground: true, displayHeaderFooter: true,
    headerTemplate: "<span></span>",
    footerTemplate: '<div style="font-size:8px;width:100%;text-align:center;color:#888">Engineering Workbench — Report · <span class="pageNumber"></span> / <span class="totalPages"></span></div>',
  });
  await browser.close();
  console.log("wrote", out);
})();
