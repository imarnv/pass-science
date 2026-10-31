// Small dependency-free SVG line chart with linear/log axes, units, legend and
// a hover read-out. Colours come from CSS variables so charts follow the theme.

import { cssVar, escapeHtml, fmt, superscript } from "./ui.js";

const NS = "http://www.w3.org/2000/svg";

function niceStep(span, target) {
  const raw = span / target;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const r = raw / mag;
  return mag * (r > 5 ? 10 : r > 2 ? 5 : r > 1 ? 2 : 1);
}

function makeAxis(values, { log = false, includeZero = false, pad = 0 } = {}) {
  let lo = Infinity, hi = -Infinity;
  for (const v of values) {
    if (!Number.isFinite(v) || (log && v <= 0)) continue;
    lo = Math.min(lo, v);
    hi = Math.max(hi, v);
  }
  if (!Number.isFinite(lo)) { lo = log ? 1 : 0; hi = log ? 10 : 1; }
  if (includeZero && !log) { lo = Math.min(lo, 0); hi = Math.max(hi, 0); }
  if (log) {
    const a = Math.floor(Math.log10(lo)), b = Math.ceil(Math.log10(hi));
    const top = b === a ? a + 1 : b;
    const stride = Math.max(1, Math.ceil((top - a) / 7)); // at most ~8 labelled decades
    const ticks = [];
    for (let e = a; e <= top; e += stride) ticks.push(10 ** e);
    return { lo: 10 ** a, hi: 10 ** top, log, ticks };
  }
  if (hi === lo) { hi += Math.abs(hi) * 0.1 || 1; lo -= Math.abs(lo) * 0.1 || 1; }
  const span = hi - lo;
  const nonNegative = lo >= 0;
  lo -= span * pad; hi += span * pad;
  if (nonNegative) lo = Math.max(lo, 0); // never pad a non-negative quantity below zero
  const step = niceStep(hi - lo, 5);
  lo = Math.floor(lo / step + 1e-9) * step;
  hi = Math.ceil(hi / step - 1e-9) * step;
  const ticks = [];
  for (let t = lo; t <= hi + step * 1e-6; t += step) ticks.push(Math.abs(t) < step * 1e-9 ? 0 : t);
  return { lo, hi, log, ticks };
}

function scale(axis, p0, p1) {
  const f = axis.log ? Math.log10 : (v) => v;
  const a = f(axis.lo), b = f(axis.hi);
  return (v) => p0 + ((f(v) - a) / (b - a)) * (p1 - p0);
}

function el(name, attrs = {}, parent) {
  const node = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
  if (parent) parent.appendChild(node);
  return node;
}

/**
 * Renders a chart into `container`.
 * spec = { xLabel, yLabel, logX, logY, height, series: [{ label, x[], y[], color, dashed, markers, width, points }],
 *          tooltip: (x) => html (optional), yIncludeZero }
 */
export function lineChart(container, spec) {
  container.innerHTML = "";
  const width = Math.max(container.clientWidth, 320);
  const height = spec.height ?? 340;
  const m = { top: 12, right: 18, bottom: 46, left: 64 };
  const svg = el("svg", { viewBox: `0 0 ${width} ${height}`, role: "img", "aria-label": spec.ariaLabel || `${spec.yLabel} versus ${spec.xLabel}` });
  container.appendChild(svg);

  const allX = spec.series.flatMap((s) => s.x);
  const allY = spec.series.flatMap((s) => s.y);
  const ax = makeAxis(allX, { log: spec.logX });
  const ay = makeAxis(allY, { log: spec.logY, includeZero: spec.yIncludeZero, pad: spec.logY ? 0 : 0.04 });
  const sx = scale(ax, m.left, width - m.right);
  const sy = scale(ay, height - m.bottom, m.top);

  const grid = el("g", { class: "grid" }, svg);
  const axes = el("g", { class: "axis" }, svg);
  for (const t of ax.ticks) {
    el("line", { x1: sx(t), x2: sx(t), y1: m.top, y2: height - m.bottom }, grid);
    el("text", { x: sx(t), y: height - m.bottom + 18, "text-anchor": "middle" }, axes).textContent = axisText(t, ax);
  }
  for (const t of ay.ticks) {
    el("line", { x1: m.left, x2: width - m.right, y1: sy(t), y2: sy(t) }, grid);
    el("text", { x: m.left - 8, y: sy(t) + 4, "text-anchor": "end" }, axes).textContent = axisText(t, ay);
  }
  el("rect", { class: "frame", x: m.left, y: m.top, width: width - m.left - m.right, height: height - m.top - m.bottom }, svg);
  el("text", { class: "axis-label", x: (m.left + width - m.right) / 2, y: height - 8, "text-anchor": "middle" }, svg).textContent = spec.xLabel;
  el("text", { class: "axis-label", transform: `translate(14 ${(m.top + height - m.bottom) / 2}) rotate(-90)`, "text-anchor": "middle" }, svg).textContent = spec.yLabel;

  const clipId = `clip-${Math.random().toString(36).slice(2)}`;
  const defs = el("defs", {}, svg);
  el("rect", { x: m.left, y: m.top - 4, width: width - m.left - m.right, height: height - m.top - m.bottom + 8 }, el("clipPath", { id: clipId }, defs));
  const plot = el("g", { "clip-path": `url(#${clipId})` }, svg);

  spec.series.forEach((s, i) => {
    const named = s.color?.match(/^var\((--[\w-]+)\)$/);
    const color = named ? cssVar(named[1]) : s.color || cssVar(`--series-${(i % 5) + 1}`);
    s._color = color;
    const valid = s.x.map((x, k) => [x, s.y[k]]).filter(([x, y]) => Number.isFinite(x) && Number.isFinite(y) && (!spec.logX || x > 0) && (!spec.logY || y > 0));
    if (!s.points) {
      const d = valid.map(([x, y], k) => `${k ? "L" : "M"}${sx(x).toFixed(2)},${sy(y).toFixed(2)}`).join("");
      el("path", { d, fill: "none", stroke: color, "stroke-width": s.width ?? 2, "stroke-dasharray": s.dashed ? "6 5" : "none", "stroke-linejoin": "round" }, plot);
    }
    if (s.markers || s.points) {
      for (const [x, y] of valid) {
        el("circle", { cx: sx(x), cy: sy(y), r: s.points ? 3.5 : 3.5, fill: s.points ? color : cssVar("--surface"), stroke: color, "stroke-width": 2, "fill-opacity": s.points ? 0.75 : 1 }, plot);
      }
    }
  });

  // legend
  if (spec.series.length > 1 || spec.legend) {
    const legend = document.createElement("div");
    legend.className = "chart-legend";
    legend.innerHTML = spec.series
      .filter((s) => !s.hideLegend)
      .map((s) => s.points
        ? `<span><i style="width:8px;height:8px;border:0;border-radius:50%;background:${s._color}"></i>${escapeHtml(s.label)}</span>`
        : `<span><i class="${s.dashed ? "dashed" : ""}" style="border-color:${s._color}"></i>${escapeHtml(s.label)}</span>`)
      .join("");
    container.appendChild(legend);
  }

  // hover read-out: snaps to the nearest x among all series
  const tip = document.createElement("div");
  tip.className = "tooltip";
  tip.hidden = true;
  container.appendChild(tip);
  const cross = el("line", { class: "crosshair", y1: m.top, y2: height - m.bottom, visibility: "hidden" }, svg);
  const xs = [...new Set(allX)].filter(Number.isFinite).sort((a, b) => a - b);
  const invX = (px) => {
    const f = ax.log ? Math.log10 : (v) => v;
    const t = (px - m.left) / (width - m.left - m.right);
    const v = f(ax.lo) + t * (f(ax.hi) - f(ax.lo));
    return ax.log ? 10 ** v : v;
  };
  svg.addEventListener("pointermove", (e) => {
    const rect = svg.getBoundingClientRect();
    const px = ((e.clientX - rect.left) / rect.width) * width;
    if (px < m.left || px > width - m.right || !xs.length) { tip.hidden = true; cross.setAttribute("visibility", "hidden"); return; }
    const target = invX(px);
    let best = xs[0];
    for (const x of xs) if (Math.abs(x - target) < Math.abs(best - target)) best = x;
    cross.setAttribute("x1", sx(best));
    cross.setAttribute("x2", sx(best));
    cross.setAttribute("visibility", "visible");
    const rows = spec.series.map((s) => {
      const k = s.x.indexOf(best);
      return k < 0 ? "" : `<div class="t-row"><i style="background:${s._color}"></i>${escapeHtml(s.label)}: <b>${fmt(s.y[k], 6)}</b></div>`;
    }).join("");
    tip.innerHTML = `<div class="t-head">${escapeHtml(spec.xName || spec.xLabel)} = ${fmt(best, 6)}</div>${rows}`;
    tip.hidden = false;
    const left = (sx(best) / width) * rect.width;
    tip.style.left = `${Math.min(left + 12, rect.width - tip.offsetWidth - 4)}px`;
    tip.style.top = `${8}px`;
  });
  svg.addEventListener("pointerleave", () => { tip.hidden = true; cross.setAttribute("visibility", "hidden"); });
}

function axisText(v, axis) {
  if (axis.log) {
    const e = Math.round(Math.log10(v));
    if (e >= -1 && e <= 3) return String(Number((10 ** e).toPrecision(1)));
    return `10${superscript(e)}`;
  }
  return fmt(v, 4);
}
