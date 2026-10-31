// Drives the running application in headless Chromium and saves timestamped
// screenshots plus the browser console log (evidence for docs/walkthrough).
// Usage: node scripts/ui_walkthrough.js http://127.0.0.1:8765/ <output-dir>
const { chromium } = require("playwright");
const fs = require("fs");
const path = require("path");

const base = process.argv[2] || "http://127.0.0.1:8765/";
const out = process.argv[3] || "docs/walkthrough";
const fixtures = path.resolve(__dirname, "..", "tests", "data");

(async () => {
  fs.mkdirSync(out, { recursive: true });
  const log = [];
  const stamp = () => new Date().toISOString();
  const browser = await chromium.launch({ args: ["--use-gl=swiftshader", "--enable-unsafe-swiftshader"] });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 1 });
  page.on("console", (m) => log.push(`${stamp()} console.${m.type()}: ${m.text()}`));
  page.on("pageerror", (e) => log.push(`${stamp()} PAGE ERROR: ${e.message}`));
  let n = 0;
  // Full-page captures: let sticky bars sit in normal flow so they are not
  // painted over the middle of the page.
  const flattenStickyBars = () => page.addStyleTag({ content: ".global-nav,.local-nav,.status-bar{position:static!important}" });
  const clearToasts = () => page.evaluate(() => document.querySelectorAll(".toast").forEach((t) => t.remove()));
  const shot = async (name, note) => {
    await page.waitForTimeout(600);
    await flattenStickyBars();
    const file = `${String(++n).padStart(2, "0")}_${name}.png`;
    await page.screenshot({ path: path.join(out, file), fullPage: true });
    const status = await page.textContent("#status-msg").catch(() => "");
    log.push(`${stamp()} [${file}] ${note} | status: ${status}`);
  };

  await page.goto(base + "#overview");
  await page.waitForSelector("#engine-status .dot.ok");
  await shot("overview", "Application opened; engine connected");

  await page.goto(base + "#geometry");
  await page.click("#geo-example");
  await page.waitForSelector("#geo-toolbar:not([hidden])");
  await shot("geometry_example", "Bundled heat-sink STL loaded");
  await page.setInputFiles("#geo-file", path.join(fixtures, "nan_vertex.stl"));
  await page.waitForSelector(".toast.error");
  await shot("geometry_rejects_nan", "Malformed STL (NaN vertex) rejected with line number");
  await clearToasts();
  await page.setInputFiles("#geo-file", path.join(fixtures, "binary.stl"));
  await page.waitForSelector(".toast.error");
  await shot("geometry_rejects_binary", "Binary STL detected and rejected");
  await clearToasts();

  await page.goto(base + "#thermal");
  await page.waitForSelector("#rod-table tbody tr td b");
  await shot("thermal_profile", "Mesh study N = 10, 20, 40, 80 solved");
  await page.click('[data-chart="error"]');
  await shot("thermal_error", "Pointwise error: zero at nodes, q h^2/8k at midpoints");
  await page.click('[data-chart="convergence"]');
  await shot("thermal_convergence", "Log-log convergence, slope 2");
  await page.fill("#rod-k", "-5");
  await page.click("#rod-solve");
  await shot("thermal_validation", "Invalid conductivity blocked by validation");
  await page.click("#rod-reset");

  await page.click('[data-mode="nodal"]');
  await page.click("#nodal-example");
  await page.waitForSelector("#nodal-legend:not([hidden])");
  const box = await page.locator("#nodal-viewer canvas").boundingBox();
  await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
  await shot("nodal_point_cloud", "Synthetic nodal CSV as coloured point cloud with legend and node inspection");
  await clearToasts();
  await page.setInputFiles("#nodal-file", path.join(fixtures, "nodal_duplicate_id.csv"));
  await page.waitForSelector(".toast.error");
  await shot("nodal_rejects_duplicate", "Duplicate node_id rejected with line number");
  await clearToasts();

  await page.goto(base + "#performance");
  await page.fill("#bench-cases", "2000");
  await page.click("#bench-run");
  await page.waitForTimeout(700);
  await shot("performance_running", "Benchmark running; UI responsive with progress");
  await page.click("#bench-cancel");
  await page.waitForSelector("#bench-run:not([disabled])");
  await shot("performance_cancelled", "Benchmark cancelled cleanly, no partial results");
  await page.fill("#bench-cases", "500");
  await page.click("#bench-run");
  await page.waitForSelector("#bench-metrics .badge", { timeout: 300000 });
  await clearToasts();
  await shot("performance_results", "Benchmark complete with speedup, efficiency and agreement");

  await clearToasts();
  await page.goto(base + "#hardware");
  await page.waitForSelector('[data-id="mcu-0"]');
  for (const id of ["mcu-0", "fpga-0", "jtag-0"]) {
    if (await page.$(`[data-id="${id}"] [data-act="connect"]`)) await page.click(`[data-id="${id}"] [data-act="connect"]`);
    await page.waitForSelector(`[data-id="${id}"] [data-act="read"]`);
    await page.click(`[data-id="${id}"] [data-act="read"]`);
    await page.waitForSelector(`[data-id="${id}"] .telemetry`);
  }
  await shot("hardware_connected", "All three simulated devices connected with telemetry");
  await page.click('[data-id="fpga-0"] [data-act="fault-once"]');
  await page.click('[data-id="fpga-0"] [data-act="read"]');
  await page.waitForSelector('[data-id="fpga-0"] [data-act="connect"]');
  await shot("hardware_timeout", "Injected telemetry timeout: device faulted");
  await page.click('[data-id="fpga-0"] [data-act="connect"]');
  await page.waitForSelector('[data-id="fpga-0"] [data-act="read"]');
  await shot("hardware_recovered", "Device recovered by reconnect");

  await clearToasts();
  await page.goto(base + "#learning");
  await page.waitForSelector("#bp-grad tbody tr");
  await shot("learning", "Backprop gradients pass finite-difference check; loss curve");

  fs.writeFileSync(path.join(out, "walkthrough_log.txt"), log.join("\n") + "\n");
  await browser.close();
  console.log(log.join("\n"));
})().catch((e) => { console.error(e); process.exit(1); });
