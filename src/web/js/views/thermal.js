// Thermal Analysis view: 1-D rod FE mesh study, and nodal CSV point clouds.

import { api, decodeBase64, saveText } from "../api.js";
import { lineChart } from "../chart.js";
import { colormap, cssGradient } from "../colormap.js";
import { bytes, clearInvalid, enableDrop, fmt, kv, readFileText, readNumbers, reportError, setStatus, toast } from "../ui.js";
import { THREE, Viewer3D } from "../viewer3d.js";

const ROD_RULES = {
  length: { label: "Length L", positive: true },
  area: { label: "Area A", positive: true },
  conductivity: { label: "Conductivity k", positive: true },
  heatGeneration: { label: "Heat generation q" },
  tempLeft: { label: "T(0)", min: -273.15 },
  tempRight: { label: "T(L)", min: -273.15 },
};
const DEFAULTS = { length: 1, area: 1e-4, conductivity: 10, heatGeneration: 10000, tempLeft: 100, tempRight: 20 };

export function initThermal() {
  const rod = initRodSolver();
  const nodal = initNodal();
  const modeGroup = document.getElementById("thermal-mode");
  modeGroup.addEventListener("click", (e) => {
    const btn = e.target.closest("button");
    if (!btn) return;
    modeGroup.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", String(b === btn)));
    const isRod = btn.dataset.mode === "rod";
    document.getElementById("thermal-rod").hidden = !isRod;
    document.getElementById("thermal-nodal").hidden = isRod;
    (isRod ? rod : nodal).activate();
  });
  return () => {
    const rodVisible = !document.getElementById("thermal-rod").hidden;
    (rodVisible ? rod : nodal).activate();
  };
}

// ======================================================================== rod

function initRodSolver() {
  const form = document.getElementById("rod-form");
  const errorEl = document.getElementById("rod-error");
  const chartEl = document.getElementById("rod-chart");
  const modeGroup = document.getElementById("rod-chart-mode");
  let result = null, request = null, chartMode = "profile", solvedOnce = false;

  function readForm() {
    clearInvalid(form);
    const { values, errors } = readNumbers(form, ROD_RULES);
    const meshes = [...document.querySelectorAll("#rod-meshes input:checked")].map((i) => Number(i.value));
    if (!meshes.length) errors.push("Select at least one mesh.");
    errorEl.hidden = !errors.length;
    errorEl.textContent = errors.join(" ");
    return errors.length ? null : { ...values, meshes };
  }

  async function solve() {
    const body = readForm();
    if (!body) { setStatus("Fix the highlighted parameters before solving.", "bad"); return; }
    const button = document.getElementById("rod-solve");
    button.disabled = true;
    setStatus("Solving the mesh study in the C++ engine…", "warn pulse");
    const t0 = performance.now();
    try {
      result = await api.post("/api/thermal/solve", body);
      request = body;
      solvedOnce = true;
      draw();
      renderTable();
      document.getElementById("rod-export-profiles").disabled = false;
      document.getElementById("rod-export-conv").disabled = false;
      const finest = result.meshes[result.meshes.length - 1];
      setStatus(`Solved ${result.meshes.length} meshes in ${(performance.now() - t0).toFixed(0)} ms. Finest mesh N = ${finest.elements}: max midpoint error ${fmt(finest.error.maxMidpointError, 4)} °C.`, "ok");
    } catch (e) {
      errorEl.hidden = false;
      errorEl.textContent = e.message;
      reportError("Solve failed", e);
    } finally {
      button.disabled = false;
    }
  }

  function draw() {
    if (!result) return;
    const p = result.problem;
    const exactX = result.exact.map((r) => r[0]);
    const exactY = result.exact.map((r) => r[1]);
    const titles = { profile: "Temperature along the rod", error: "Pointwise error T_FE − T_exact", convergence: "Mesh convergence (log–log)" };
    document.getElementById("rod-chart-title").textContent = titles[chartMode];

    if (chartMode === "profile") {
      lineChart(chartEl, {
        xLabel: "Position x [m]", yLabel: "Temperature T [°C]", xName: "x [m]",
        series: [
          ...result.meshes.map((m) => ({ label: `FE, N = ${m.elements}`, x: m.x, y: m.T, markers: m.elements <= 20 })),
          { label: "Exact", x: exactX, y: exactY, dashed: true, color: "var(--text)" },
        ],
      });
    } else if (chartMode === "error") {
      // Error is evaluated at nodes and midpoints: zero at nodes, peak q h²/(8k) at midpoints.
      lineChart(chartEl, {
        xLabel: "Position x [m]", yLabel: "T_FE − T_exact [°C]", xName: "x [m]", yIncludeZero: true,
        series: result.meshes.map((m) => {
          const xs = [], ys = [];
          m.x.forEach((x, i) => {
            xs.push(x); ys.push(0);
            if (i < m.midpoints.length) { xs.push(m.midpoints[i][0]); ys.push(m.midpoints[i][1] - m.midpoints[i][2]); }
          });
          return { label: `N = ${m.elements}`, x: xs, y: ys };
        }),
      });
    } else {
      const h = result.meshes.map((m) => m.error.h);
      lineChart(chartEl, {
        xLabel: "Element size h [m]", yLabel: "Max midpoint error [°C]", xName: "h [m]", logX: true, logY: true,
        series: [
          { label: "Measured (FE interpolation at midpoints)", x: h, y: result.meshes.map((m) => m.error.maxMidpointError), markers: true },
          { label: "Theory q h²/(8k), slope 2", x: h, y: h.map((v) => (p.heatGeneration * v * v) / (8 * p.conductivity)), dashed: true, color: "var(--text-3)" },
        ],
      });
    }
  }

  function renderTable() {
    const p = result.problem;
    document.querySelector("#rod-table tbody").innerHTML = result.meshes.map((m) => `
      <tr><td>${m.elements}</td><td>${fmt(m.error.h, 4)}</td><td>${fmt(m.error.maxNodalError, 3)}</td>
      <td><b>${fmt(m.error.maxMidpointError, 6)}</b></td><td>${fmt((Math.abs(p.heatGeneration) * m.error.h ** 2) / (8 * p.conductivity), 6)}</td>
      <td>${m.order === null ? "—" : m.order.toFixed(3)}</td></tr>`).join("");
  }

  async function exportCsv(kind) {
    try {
      const text = await api.post("/api/thermal/export", { ...request, kind });
      saveText(text, kind === "convergence" ? "fea_convergence.csv" : "fea_profiles.csv");
      toast("Exported", kind === "convergence" ? "fea_convergence.csv" : "fea_profiles.csv", "success", 3000);
    } catch (e) { reportError("Export failed", e); }
  }

  form.addEventListener("submit", (e) => { e.preventDefault(); solve(); });
  form.addEventListener("input", () => { if (!errorEl.hidden) readForm(); });
  document.getElementById("rod-reset").onclick = () => {
    for (const [k, v] of Object.entries(DEFAULTS)) form.querySelector(`[name="${k}"]`).value = v;
    document.querySelectorAll("#rod-meshes input").forEach((i) => (i.checked = Number(i.value) <= 80));
    readForm();
  };
  modeGroup.addEventListener("click", (e) => {
    const btn = e.target.closest("button");
    if (!btn) return;
    modeGroup.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", String(b === btn)));
    chartMode = btn.dataset.chart;
    draw();
  });
  document.getElementById("rod-export-profiles").onclick = () => exportCsv("profiles");
  document.getElementById("rod-export-conv").onclick = () => exportCsv("convergence");
  let resizeTimer;
  window.addEventListener("resize", () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(draw, 150); });

  return { activate: () => { if (!solvedOnce) solve(); else draw(); } };
}

// ====================================================================== nodal

function initNodal() {
  const container = document.getElementById("nodal-viewer");
  const fileInput = document.getElementById("nodal-file");
  const toolbar = document.getElementById("nodal-toolbar");
  const sizeInput = document.getElementById("nodal-size");
  let viewer = null, points = null, marker = null, data = null;

  async function load(name, text, synthetic) {
    setStatus(`Validating ${name} in the C++ engine…`, "warn pulse");
    try {
      const result = await api.postText("/api/results/nodal-csv", text);
      show(name, result, synthetic);
      setStatus(`Loaded ${name}: ${result.count.toLocaleString()} nodes parsed in ${result.parseMs.toFixed(1)} ms.`, "ok");
    } catch (e) {
      reportError(`Could not import ${name}`, e);
    }
  }

  function show(name, result, synthetic) {
    viewer ??= new Viewer3D(container);
    viewer.clear();
    const positions = decodeBase64(result.positions, Float32Array);
    const temps = decodeBase64(result.temperatures, Float64Array);
    const ids = decodeBase64(result.ids, Float64Array);
    const span = result.maxTemperature - result.minTemperature || 1;
    const colors = new Float32Array(positions.length);
    for (let i = 0; i < temps.length; i++) colors.set(colormap((temps[i] - result.minTemperature) / span), 3 * i);

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute("color", new THREE.BufferAttribute(colors, 3));
    points = new THREE.Points(geometry, new THREE.PointsMaterial({ size: Number(sizeInput.value), sizeAttenuation: false, vertexColors: true }));
    viewer.add(points);
    marker = new THREE.Mesh(new THREE.SphereGeometry(1, 16, 12), new THREE.MeshBasicMaterial({ color: 0xff375f, depthTest: false }));
    marker.renderOrder = 20;
    marker.visible = false;
    viewer.scene.add(marker);
    viewer.view("iso");

    data = { name, result, positions, temps, ids };
    document.getElementById("nodal-empty").hidden = true;
    toolbar.hidden = false;
    document.getElementById("nodal-hint").hidden = false;
    document.getElementById("nodal-legend").hidden = false;
    document.getElementById("nodal-legend-bar").style.background = cssGradient();
    const ticks = [];
    for (let k = 0; k <= 5; k++) ticks.push(`<span>${fmt(result.maxTemperature - (k / 5) * span, 4)}</span>`);
    document.getElementById("nodal-legend-ticks").innerHTML = ticks.join("");
    document.getElementById("nodal-badge").innerHTML = synthetic
      ? '<span class="badge warn" title="Generated analytically; not an ANSYS export">SYNTHETIC</span>'
      : '<span class="badge accent">USER FILE</span>';

    const [mn, mx] = [result.bounds.min, result.bounds.max];
    kv(document.getElementById("nodal-info"), [
      ["File", name],
      ["Nodes", result.count.toLocaleString()],
      ["Temperature", `${fmt(result.minTemperature, 5)} … ${fmt(result.maxTemperature, 5)} °C`],
      ["Extent X", `${fmt(mn[0], 4)} … ${fmt(mx[0], 4)} m`],
      ["Extent Y", `${fmt(mn[1], 4)} … ${fmt(mx[1], 4)} m`],
      ["Extent Z", `${fmt(mn[2], 4)} … ${fmt(mx[2], 4)} m`],
      ["File size", bytes(result.bytes)],
      ["Parse time (C++)", `${result.parseMs.toFixed(2)} ms`],
    ]);
    kv(document.getElementById("nodal-pick"), [["Node", "Click a point"]]);
  }

  function pick(event) {
    if (!data || !points) return;
    const i = viewer.pickPoint(event, points);
    if (i < 0) return;
    const p = data.positions.subarray(3 * i, 3 * i + 3);
    const radius = viewer.bounds.getBoundingSphere(new THREE.Sphere()).radius;
    marker.scale.setScalar(radius * 0.012);
    marker.position.set(p[0], p[1], p[2]);
    marker.visible = true;
    kv(document.getElementById("nodal-pick"), [
      ["node_id", `<b>${data.ids[i]}</b>`],
      ["Temperature", `<b>${data.temps[i].toFixed(4)} °C</b>`],
      ["x", `${fmt(p[0], 6)} m`],
      ["y", `${fmt(p[1], 6)} m`],
      ["z", `${fmt(p[2], 6)} m`],
      ["Row in file", String(i + 2)],
    ]);
  }

  async function openFile(file) {
    try { await load(file.name, await readFileText(file), false); } catch (e) { reportError(`Could not open ${file.name}`, e); }
  }

  document.getElementById("nodal-open").onclick = () => fileInput.click();
  fileInput.onchange = () => { if (fileInput.files[0]) openFile(fileInput.files[0]); fileInput.value = ""; };
  document.getElementById("nodal-example").onclick = async () => {
    try {
      const res = await fetch("examples/synthetic_nodal_temperatures.csv");
      if (!res.ok) throw new Error("The bundled example file is missing from app/examples.");
      await load("synthetic_nodal_temperatures.csv", await res.text(), true);
      toast("Synthetic data", "This field is generated analytically to exercise the importer. It is not an ANSYS export.", "warning", 6000);
    } catch (e) { reportError("Could not load the example", e); }
  };
  enableDrop(container, openFile);

  // A click (not a drag) selects a node.
  let down = null;
  container.addEventListener("pointerdown", (e) => (down = [e.clientX, e.clientY]));
  container.addEventListener("pointerup", (e) => {
    if (down && Math.hypot(e.clientX - down[0], e.clientY - down[1]) < 4 && e.button === 0) pick(e);
    down = null;
  });
  sizeInput.oninput = () => {
    document.getElementById("nodal-size-val").textContent = `${sizeInput.value} px`;
    if (points) points.material.size = Number(sizeInput.value);
  };
  toolbar.addEventListener("click", (e) => {
    const btn = e.target.closest("button");
    if (!btn || !viewer) return;
    const act = btn.dataset.act;
    if (act === "fit") viewer.fit();
    else if (["iso", "top", "front"].includes(act)) viewer.view(act);
    else if (act === "axes") {
      const on = btn.getAttribute("aria-pressed") !== "true";
      btn.setAttribute("aria-pressed", String(on));
      viewer.setAxesVisible(on);
    }
  });

  return { activate: () => viewer?.resize() };
}
