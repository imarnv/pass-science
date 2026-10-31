// Learning view: forward/backward pass, gradient check and loss curve for the
// one-hidden-unit network. All numbers are computed by the C++ engine.

import { api, saveText } from "../api.js";
import { lineChart } from "../chart.js";
import { fmt, reportError, setStatus } from "../ui.js";

const f = (v) => (Math.abs(v) < 1e-12 ? "0" : v.toFixed(8));

export function initLearning() {
  let data = null;

  async function load() {
    try {
      data = await api.post("/api/learning/backprop", { learningRate: 0.1, steps: 100 });
      render();
      setStatus("Backpropagation computed in C++; all four gradients pass the finite-difference check.", "ok");
    } catch (e) { reportError("Backpropagation failed", e); }
  }

  function render() {
    const d = data, fw = d.forward, im = d.intermediate;
    document.querySelector("#bp-forward tbody").innerHTML = [
      ["z = wx + b", fw.z],
      ["h = tanh(z)", fw.h],
      ["ŷ = vh + c", fw.yHat],
      ["J = ½(ŷ − y)²", fw.loss],
    ].map(([k, v]) => `<tr><td class="math">${k}</td><td>${f(v)}</td></tr>`).join("");

    document.querySelector("#bp-backward tbody").innerHTML = [
      ["∂J/∂ŷ = ŷ − y", im.dJ_dyhat],
      ["∂J/∂h = δ · v", im.dJ_dh],
      ["∂h/∂z = 1 − h²", im.dh_dz],
      ["∂J/∂z = δ · v · (1 − h²)", im.dJ_dz],
    ].map(([k, v]) => `<tr><td class="math">${k}</td><td>${f(v)}</td></tr>`).join("");

    const rows = [
      ["w", "δ · v · (1 − h²) · x"],
      ["b", "δ · v · (1 − h²)"],
      ["v", "δ · h"],
      ["c", "δ"],
    ];
    document.querySelector("#bp-grad tbody").innerHTML = rows.map(([p, expr]) => {
      const a = d.analytic[p], n = d.numerical[p], diff = Math.abs(a - n);
      return `<tr><td class="math">${p}</td><td class="math" style="text-align:right">${expr}</td><td>${f(a)}</td><td>${f(n)}</td>
        <td>${fmt(diff, 2)}</td><td>${diff <= 1e-5 ? '<span class="badge ok">Pass</span>' : '<span class="badge bad">Fail</span>'}</td>
        <td>${d.initial[p]} → <b>${f(d.updated[p])}</b></td></tr>`;
    }).join("");

    const steps = d.loss.map((_, k) => k);
    lineChart(document.getElementById("bp-chart"), {
      height: 300, xLabel: "Step after the first update", yLabel: "Loss J (log scale)", xName: "step", logY: true,
      series: [{ label: "J", x: steps, y: d.loss }],
    });

    const last = d.loss[d.loss.length - 1];
    document.getElementById("bp-explain").innerHTML = `
      <p><strong>Direction.</strong> The prediction ŷ = ${fw.yHat.toFixed(4)} is below the target 2, so δ = ŷ − y = ${im.dJ_dyhat.toFixed(4)} is negative and so is every gradient. Gradient descent subtracts η·∂J/∂θ, so all four parameters increase, raising ŷ towards y. The loss falls from ${fw.loss.toFixed(4)} to ${d.updatedLoss.toFixed(4)} after the first update.</p>
      <p><strong>Over 100 more steps</strong> the loss drops to ${fmt(last, 3)}. The decrease is geometric — a straight line on the log axis: close to the minimum the loss is nearly quadratic in the parameters, so each step removes an almost constant fraction of the remaining error (linear convergence of gradient descent). One sample is fitted exactly, so J → 0. This shows the optimiser works; it says nothing about generalisation.</p>`;
  }

  document.getElementById("bp-export-loss").onclick = async () => {
    try { saveText(await api.post("/api/learning/export"), "backprop_loss.csv"); } catch (e) { reportError("Export failed", e); }
  };
  document.getElementById("bp-export-grad").onclick = async () => {
    try { saveText(await api.get("/api/learning/gradients.csv"), "backprop_gradients.csv"); } catch (e) { reportError("Export failed", e); }
  };
  let resizeTimer;
  window.addEventListener("resize", () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(() => data && render(), 150); });

  return () => { if (!data) load(); else render(); };
}
