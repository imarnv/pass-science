// Application shell: hash routing between views, engine status, heartbeat
// (lets the engine exit when the window is closed) and the Quit action.

import { api } from "./api.js";
import { bytes, escapeHtml, kv, reportError, setStatus, toast } from "./ui.js";
import { initGeometry } from "./views/geometry.js";
import { initThermal } from "./views/thermal.js";
import { initPerformance } from "./views/performance.js";
import { initHardware } from "./views/hardware.js";
import { initLearning } from "./views/learning.js";

const VIEWS = ["overview", "geometry", "thermal", "performance", "hardware", "learning"];
const activators = {};
let system = null;

function route() {
  const name = VIEWS.includes(location.hash.slice(1)) ? location.hash.slice(1) : "overview";
  for (const v of VIEWS) document.getElementById(`view-${v}`).classList.toggle("active", v === name);
  document.querySelectorAll(".nav-links a").forEach((a) => {
    a.toggleAttribute("aria-current", a.getAttribute("href") === `#${name}`);
    if (a.getAttribute("href") === `#${name}`) a.setAttribute("aria-current", "page");
  });
  document.title = `${document.getElementById(`view-${name}`).dataset.title} · Engineering Workbench`;
  window.scrollTo({ top: 0 });
  activators[name]?.();
}

function setEngine(state, text) {
  const el = document.getElementById("engine-status");
  el.querySelector(".dot").className = `dot ${state}`;
  el.querySelector(".text").textContent = text;
}

async function connect() {
  try {
    system = await api.get("/api/system");
    setEngine("ok", `C++ engine · ${system.logicalCpus} CPUs`);
    const launch = system.uiReadyMs >= 0 ? `${(system.uiReadyMs / 1000).toFixed(2)} s` : "—";
    document.getElementById("status-right").textContent = `Launch-to-ready ${launch}`;
    setStatus(`Engine ready. Server up ${system.serverReadyMs.toFixed(0)} ms after launch; UI connected after ${launch}.`, "ok");
    kv(document.getElementById("overview-kv"), [
      ["CPU", escapeHtml(system.cpu)],
      ["Cores", `${system.physicalCores || "?"} physical · ${system.logicalCpus} logical`],
      ["Memory", bytes(system.ramBytes)],
      ["OS", escapeHtml(system.os)],
      ["Launch-to-ready", launch],
    ]);
    return system;
  } catch (e) {
    setEngine("bad", "Engine offline");
    reportError("Cannot reach the C++ engine", e);
    return null;
  }
}

function startHeartbeat() {
  let failures = 0;
  setInterval(async () => {
    try {
      await api.post("/api/heartbeat");
      if (failures) setEngine("ok", `C++ engine · ${system?.logicalCpus ?? "?"} CPUs`);
      failures = 0;
    } catch {
      if (++failures === 2) {
        setEngine("bad", "Engine offline");
        setStatus("The C++ engine stopped. Start Launch.exe again to continue.", "bad");
      }
    }
  }, 10000);
}

document.getElementById("quit-btn").addEventListener("click", async () => {
  if (!confirm("Stop the workbench engine? Unsaved results will be lost.")) return;
  try { await api.post("/api/shutdown"); } catch { /* already stopped */ }
  document.body.innerHTML = `<div class="empty" style="height:100vh"><strong>Workbench closed</strong><span>The engine has stopped. You can close this window.</span></div>`;
  setTimeout(() => window.close(), 300);
});

window.addEventListener("unhandledrejection", (e) => reportError("Unexpected error", e.reason));

(async function boot() {
  const sys = await connect();
  activators.geometry = initGeometry();
  activators.thermal = initThermal();
  activators.performance = initPerformance(() => system);
  activators.hardware = initHardware();
  activators.learning = initLearning();
  window.addEventListener("hashchange", route);
  route();
  if (sys) startHeartbeat();
  else toast("Engine offline", "Start the workbench with Launch.exe so the C++ engine is running.", "error", 0);
})();
