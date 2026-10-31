// Performance view: launches the benchmark job, polls progress, renders results.

import { api, saveText } from "../api.js";
import { lineChart } from "../chart.js";
import { bytes, clearInvalid, escapeHtml, fmt, kv, readNumbers, reportError, setStatus, toast } from "../ui.js";

const RULES = {
  cases: { label: "Cases", integer: true, min: 1, max: 100000 },
  elements: { label: "Elements per case", integer: true, min: 1, max: 2000000 },
  seed: { label: "Seed", integer: true, min: 0 },
  maxWorkers: { label: "Max workers", integer: true, min: 1, max: 64 },
  warmupRuns: { label: "Warm-up runs", integer: true, min: 0, max: 10 },
  measuredRuns: { label: "Measured runs", integer: true, min: 1, max: 50 },
};

export function initPerformance(getSystem) {
  const form = document.getElementById("bench-form");
  const errorEl = document.getElementById("bench-error");
  const runBtn = document.getElementById("bench-run");
  const cancelBtn = document.getElementById("bench-cancel");
  const progressCard = document.getElementById("bench-progress-card");
  let jobId = null, pollTimer = null, report = null;

  function renderSystem() {
    const s = getSystem();
    if (!s) return;
    kv(document.getElementById("bench-system"), [
      ["CPU", escapeHtml(s.cpu)],
      ["Physical cores", String(s.physicalCores || "unknown")],
      ["Logical CPUs", String(s.logicalCpus)],
      ["RAM", bytes(s.ramBytes)],
      ["OS", escapeHtml(s.os)],
      ["Compiler", escapeHtml(s.compiler)],
      ["Build", `${escapeHtml(s.buildType)} <code>${escapeHtml(s.buildFlags || "(none)")}</code>`],
      ["Launch-to-ready", s.uiReadyMs >= 0 ? `${(s.uiReadyMs / 1000).toFixed(2)} s` : "—"],
    ]);
    if (s.logicalCpus === 1) {
      errorEl.hidden = false;
      errorEl.textContent = "This machine reports one logical CPU: the parallel path still runs and is verified, but speedup cannot be assessed.";
    }
  }

  function setRunning(running) {
    runBtn.disabled = running;
    cancelBtn.hidden = !running;
    form.querySelectorAll("input").forEach((i) => (i.disabled = running));
    progressCard.hidden = !running && !jobId;
  }

  async function start() {
    clearInvalid(form);
    const { values, errors } = readNumbers(form, RULES);
    const nodes = values.cases * (values.elements + 1);
    if (!errors.length && nodes > 50e6) errors.push("Cases × (elements + 1) must not exceed 50,000,000 to keep memory bounded.");
    errorEl.hidden = !errors.length;
    errorEl.textContent = errors.join(" ");
    if (errors.length) return;
    try {
      const { jobId: id } = await api.post("/api/performance/run", values);
      jobId = id;
      setRunning(true);
      progressCard.hidden = false;
      document.getElementById("bench-progress").style.width = "0%";
      setStatus("Benchmark running in the C++ engine. The interface stays responsive; you can switch views.", "warn pulse");
      poll();
    } catch (e) { reportError("Could not start the benchmark", e); }
  }

  async function poll() {
    clearTimeout(pollTimer);
    try {
      const job = await api.get(`/api/jobs/${jobId}`);
      document.getElementById("bench-stage").textContent = job.stage;
      document.getElementById("bench-elapsed").textContent = `${job.elapsedSeconds.toFixed(1)} s`;
      document.getElementById("bench-progress").style.width = `${(job.progress * 100).toFixed(1)}%`;
      if (job.state === "running") { pollTimer = setTimeout(poll, 250); return; }
      setRunning(false);
      if (job.state === "succeeded") {
        report = job.result;
        render();
        document.querySelectorAll("[data-export]").forEach((b) => (b.disabled = false));
        setStatus(`Benchmark finished in ${job.elapsedSeconds.toFixed(1)} s. Agreement ${report.agreementPassed ? "passed" : "FAILED"}.`, report.agreementPassed ? "ok" : "bad");
        toast("Benchmark complete", `${report.rows.length - 1} parallel configurations measured.`, "success", 4000);
      } else if (job.state === "cancelled") {
        setStatus("Benchmark cancelled. No partial results were kept.", "warn");
        toast("Cancelled", "The run stopped cleanly; previous results are unchanged.", "warning", 4000);
      } else {
        reportError("Benchmark failed", new Error(job.error || "Unknown error"));
      }
    } catch (e) {
      setRunning(false);
      reportError("Lost contact with the benchmark", e);
    }
  }

  function render() {
    const rows = report.rows;
    const parallel = rows.filter((r) => r.mode === "parallel");
    const best = parallel.reduce((a, b) => (b.speedup > a.speedup ? b : a), parallel[0]);
    const maxDiff = Math.max(...parallel.map((r) => r.maxAbsDiff));
    const metrics = document.querySelectorAll("#bench-metrics .metric .value");
    metrics[0].innerHTML = `${best.speedup.toFixed(2)}<small>× at p = ${best.workers}</small>`;
    metrics[1].innerHTML = `${(best.efficiency * 100).toFixed(0)}<small>%</small>`;
    metrics[2].innerHTML = `${maxDiff === 0 ? "0" : fmt(maxDiff, 2)}<small>°C</small>`;
    metrics[3].innerHTML = report.agreementPassed ? '<span class="badge ok" style="font-size:15px">Pass</span>' : '<span class="badge bad" style="font-size:15px">Fail</span>';

    document.querySelector("#bench-table tbody").innerHTML = rows.map((r) => `
      <tr><td>${r.mode === "serial" ? "Serial loop" : "Worker pool"}</td><td>${r.workers}</td><td><b>${r.median.toFixed(4)}</b></td>
      <td style="color:var(--text-2)">${r.runs.map((t) => t.toFixed(3)).join(" · ")}</td>
      <td>${r.mode === "serial" ? "—" : r.speedup.toFixed(2) + "×"}</td><td>${r.mode === "serial" ? "—" : (r.efficiency * 100).toFixed(0) + "%"}</td>
      <td>${r.mode === "serial" ? "reference" : fmt(r.maxAbsDiff, 2)}</td></tr>`).join("");
    const c = report.config;
    document.getElementById("bench-note").textContent =
      `${c.cases} cases × ${c.elements.toLocaleString()} elements · seed ${c.seed} · ${c.warmupRuns} warm-up + ${c.measuredRuns} measured runs` + (report.note ? ` · ${report.note}` : "");

    const p = parallel.map((r) => r.workers);
    lineChart(document.getElementById("bench-speedup"), {
      height: 280, xLabel: "Workers p", yLabel: "Speedup T₁/Tₚ", xName: "p", yIncludeZero: true,
      series: [
        { label: "Measured", x: p, y: parallel.map((r) => r.speedup), markers: true },
        { label: "Ideal", x: p, y: p, dashed: true, color: "var(--text-3)" },
      ],
    });
    const xs = [], ys = [];
    for (const r of parallel) for (const t of r.runs) { xs.push(r.workers); ys.push(t); }
    lineChart(document.getElementById("bench-times"), {
      height: 280, xLabel: "Workers p", yLabel: "Wall-clock time [s]", xName: "p", yIncludeZero: true, legend: true,
      series: [
        { label: "Median", x: p, y: parallel.map((r) => r.median) },
        { label: "Individual runs", x: xs, y: ys, points: true, color: "var(--series-3)" },
      ],
    });
  }

  runBtn.onclick = start;
  form.addEventListener("submit", (e) => { e.preventDefault(); start(); });
  cancelBtn.onclick = async () => {
    if (!jobId) return;
    cancelBtn.disabled = true;
    try { await api.post(`/api/jobs/${jobId}/cancel`); } catch (e) { reportError("Cancel failed", e); }
    cancelBtn.disabled = false;
  };
  document.querySelectorAll("[data-export]").forEach((btn) => {
    btn.onclick = async () => {
      try {
        const kind = btn.dataset.export;
        const text = await api.get(`/api/performance/export?kind=${kind}`);
        saveText(text, { summary: "bench_summary.csv", runs: "bench_runs.csv", system: "bench_system.txt" }[kind]);
      } catch (e) { reportError("Export failed", e); }
    };
  });
  let resizeTimer;
  window.addEventListener("resize", () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(() => report && render(), 150); });

  return () => { renderSystem(); if (report) render(); };
}
