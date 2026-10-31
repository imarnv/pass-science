// Shared UI helpers: status bar, toasts, number formatting, form validation.

const statusMsg = () => document.getElementById("status-msg");
const statusDot = () => document.getElementById("status-dot");

export function setStatus(message, level = "") {
  statusMsg().textContent = message;
  statusDot().className = "dot" + (level ? " " + level : "");
}

export function toast(title, text = "", kind = "info", timeout = 7000) {
  const el = document.createElement("div");
  el.className = `toast ${kind}`;
  el.setAttribute("role", kind === "error" ? "alert" : "status");
  el.innerHTML = `<div class="bar"></div><div class="body"><div class="title"></div><div class="text"></div></div>
                  <button class="close" aria-label="Dismiss">×</button>`;
  el.querySelector(".title").textContent = title;
  el.querySelector(".text").textContent = text;
  el.querySelector(".close").onclick = () => el.remove();
  document.getElementById("toasts").appendChild(el);
  if (timeout) setTimeout(() => el.remove(), timeout);
}

export function reportError(context, error) {
  const message = error?.message || String(error);
  toast(context, message, "error", 12000);
  setStatus(`${context}: ${message}`, "bad");
}

// ------------------------------------------------------------- formatting

export function fmt(value, digits = 4) {
  if (value === null || value === undefined || Number.isNaN(value)) return "—";
  if (value === 0) return "0";
  const a = Math.abs(value);
  if (a >= 1e6 || a < 1e-3) {
    const [mantissa, exponent] = value.toExponential(digits - 1).split("e");
    return `${mantissa}×10${superscript(Number(exponent))}`;
  }
  return Number(value.toPrecision(digits)).toLocaleString("en-US", { maximumSignificantDigits: digits });
}

export function fixed(value, decimals) {
  return value === null || value === undefined ? "—" : value.toFixed(decimals);
}

export function sci(value, digits = 3) {
  if (value === 0) return "0";
  return fmt(value, digits);
}

const SUP = { "-": "⁻", 0: "⁰", 1: "¹", 2: "²", 3: "³", 4: "⁴", 5: "⁵", 6: "⁶", 7: "⁷", 8: "⁸", 9: "⁹" };
export function superscript(text) {
  return String(text).split("").map((c) => SUP[c] ?? c).join("");
}

export function bytes(n) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  if (n < 1024 ** 3) return `${(n / 1024 / 1024).toFixed(1)} MB`;
  return `${(n / 1024 ** 3).toFixed(1)} GB`;
}

export function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
}

export function kv(el, rows) {
  el.innerHTML = rows.map(([k, v]) => `<dt>${escapeHtml(k)}</dt><dd>${v}</dd>`).join("");
}

// ------------------------------------------------------------- validation

// Reads numeric inputs of a form against simple rules. Marks invalid inputs and
// returns { values, errors }. Rules: { name: { min, max, integer, positive, label } }.
export function readNumbers(form, rules) {
  const values = {};
  const errors = [];
  for (const [name, rule] of Object.entries(rules)) {
    // querySelector, not form.elements[name]: "length" collides with elements.length.
    const input = form.querySelector(`[name="${name}"]`);
    const raw = input.value.trim();
    const value = Number(raw);
    let problem = "";
    if (raw === "" || !Number.isFinite(value)) problem = "must be a number";
    else if (rule.integer && !Number.isInteger(value)) problem = "must be a whole number";
    else if (rule.positive && value <= 0) problem = "must be greater than zero";
    else if (rule.min !== undefined && value < rule.min) problem = `must be at least ${rule.min}`;
    else if (rule.max !== undefined && value > rule.max) problem = `must be at most ${rule.max}`;
    input.classList.toggle("invalid", !!problem);
    input.setAttribute("aria-invalid", problem ? "true" : "false");
    if (problem) errors.push(`${rule.label} ${problem}.`);
    values[name] = value;
  }
  return { values, errors };
}

export function clearInvalid(form) {
  form.querySelectorAll(".invalid").forEach((el) => el.classList.remove("invalid"));
}

// Reads a File as text, rejecting oversize files before upload.
export async function readFileText(file, maxBytes = 64 * 1024 * 1024) {
  if (file.size > maxBytes) {
    throw new Error(`${file.name} is ${bytes(file.size)}, above the 64 MB import limit. Decimate or split the file.`);
  }
  return file.text();
}

// Wires drag-and-drop of a single file onto an element.
export function enableDrop(element, onFile) {
  element.addEventListener("dragover", (e) => { e.preventDefault(); element.classList.add("dragging"); });
  element.addEventListener("dragleave", () => element.classList.remove("dragging"));
  element.addEventListener("drop", (e) => {
    e.preventDefault();
    element.classList.remove("dragging");
    const file = e.dataTransfer.files[0];
    if (file) onFile(file);
  });
}

export function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}
