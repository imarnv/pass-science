// Hardware view: discovery, connect/disconnect, telemetry polling and fault
// injection against the engine's SIMULATED device adapters.

import { api } from "../api.js";
import { escapeHtml, fmt, reportError, setStatus, toast } from "../ui.js";

const KIND_LABEL = { microcontroller: "Microcontroller board", fpga: "FPGA board", "jtag-adapter": "JTAG debug adapter" };
const KIND_ROLE = {
  microcontroller: "Runs firmware and streams sensor telemetry over a USB virtual COM port.",
  fpga: "Runs a bitstream; on-chip monitors report temperature and supply rails.",
  "jtag-adapter": "Debug probe only: drives TCK/TMS/TDI and reads TDO of another target. It does not run user workloads.",
};

export function initHardware() {
  const grid = document.getElementById("hw-devices");
  const devices = new Map();   // id -> { info, state, telemetry, streaming, timer }
  let discovered = false;

  async function discover() {
    try {
      const result = await api.get("/api/hardware/devices");
      for (const d of result.devices) {
        const existing = devices.get(d.id);
        devices.set(d.id, { ...(existing || { telemetry: null, streaming: false, timer: null }), info: d, state: d.state });
      }
      const p = result.policy;
      document.getElementById("hw-attempts").textContent = p.maxAttempts;
      document.getElementById("hw-timeout").textContent = `${p.timeoutMs} ms`;
      document.getElementById("hw-backoff").textContent = `${p.initialBackoffMs} ms`;
      discovered = true;
      render();
      refreshLog();
      setStatus(`Discovered ${result.devices.length} simulated devices.`, "ok");
    } catch (e) { reportError("Discovery failed", e); }
  }

  function render() {
    grid.innerHTML = "";
    for (const [id, d] of devices) {
      const card = document.createElement("div");
      card.className = "card device-card";
      card.dataset.id = id;
      card.innerHTML = cardHtml(d);
      grid.appendChild(card);
    }
  }

  function cardHtml(d) {
    const i = d.info;
    const dot = d.state === "connected" ? "ok" : d.state === "faulted" ? "bad" : "";
    const telemetry = d.telemetry
      ? `<div class="telemetry">${Object.entries(d.telemetry.values).map(([name, v]) => `
          <div class="t"><div class="n">${escapeHtml(name)}</div><div class="v">${formatValue(name, v)}</div></div>`).join("")}</div>
         <div style="font-size:11px;color:var(--text-3)">Message #${d.telemetry.sequence} · ${new Date(d.telemetry.timestampMs).toLocaleTimeString()} · simulated</div>`
      : `<div style="font-size:13px;color:var(--text-3)">${d.state === "connected" ? "Request telemetry to see a message." : "Connect to read telemetry."}</div>`;
    return `
      <div class="device-head">
        <div><h3>${KIND_LABEL[i.kind]}</h3><div class="model">${escapeHtml(i.model)}</div></div>
        <span class="badge warn">SIMULATED</span>
      </div>
      <div class="device-state"><span class="dot ${dot}"></span>${d.state}${d.fault && d.fault !== "none" ? ` · fault armed: ${d.fault}` : ""}</div>
      <p class="note" style="font-size:13px;color:var(--text-2)">${KIND_ROLE[i.kind]}</p>
      <dl class="kv">
        <dt>ID</dt><dd class="mono">${escapeHtml(i.id)}</dd>
        <dt>Port</dt><dd class="mono">${escapeHtml(i.port)}</dd>
        <dt>Host link</dt><dd>${escapeHtml(i.transport)}</dd>
        <dt>I/O level</dt><dd>${i.ioVoltage.toFixed(1)} V</dd>
        <dt>Firmware</dt><dd>${escapeHtml(i.firmware)}</dd>
      </dl>
      <div class="tags">${i.capabilities.map((c) => `<span class="tag">${escapeHtml(c)}</span>`).join("")}</div>
      ${telemetry}
      <div class="device-actions">
        ${d.state === "connected"
          ? `<button class="btn small" data-act="disconnect">Disconnect</button>
             <button class="btn small primary" data-act="read">Read telemetry</button>
             <button class="btn small ${d.streaming ? "primary" : ""}" data-act="stream">${d.streaming ? "Stop stream" : "Stream 1 Hz"}</button>`
          : `<button class="btn small primary" data-act="connect">${d.state === "faulted" ? "Recover" : "Connect"}</button>`}
      </div>
      <div class="device-actions">
        <button class="btn small ghost" data-act="fault-once" title="The next request times out; retry recovers">Inject timeout</button>
        <button class="btn small ghost" data-act="fault-always" title="Every request times out until cleared">Unplug</button>
        <button class="btn small ghost" data-act="fault-none">Clear fault</button>
      </div>`;
  }

  function formatValue(name, v) {
    if (name === "idcode") return `<span class="mono">0x${Math.round(v.value).toString(16).toUpperCase().padStart(8, "0")}</span>`;
    if (v.unit === "bool") return v.value ? "yes" : "no";
    return `${fmt(v.value, 4)}<small>${escapeHtml(v.unit)}</small>`;
  }

  function update(id) {
    const card = grid.querySelector(`[data-id="${id}"]`);
    if (card) card.innerHTML = cardHtml(devices.get(id));
  }

  async function action(id, act) {
    const d = devices.get(id);
    try {
      if (act === "connect") {
        setStatus(`Connecting to ${id}…`, "warn pulse");
        const r = await api.post(`/api/hardware/${id}/connect`);
        d.state = r.state;
        d.fault = d.fault === "timeout-once" ? "none" : d.fault;
        setStatus(`${id} connected after ${r.attempts} attempt${r.attempts > 1 ? "s" : ""}.`, "ok");
        if (r.attempts > 1) toast("Recovered", `${id} timed out, then connected on retry ${r.attempts}.`, "success");
      } else if (act === "disconnect") {
        stopStream(d);
        await api.post(`/api/hardware/${id}/disconnect`);
        d.state = "disconnected";
        d.telemetry = null;
      } else if (act === "read") {
        await readTelemetry(id);
      } else if (act === "stream") {
        if (d.streaming) stopStream(d);
        else { d.streaming = true; d.timer = setInterval(() => readTelemetry(id), 1000); readTelemetry(id); }
      } else if (act.startsWith("fault-")) {
        const mode = { "fault-once": "timeout-once", "fault-always": "timeout-always", "fault-none": "none" }[act];
        await api.post(`/api/hardware/${id}/fault`, { mode });
        d.fault = mode;
        setStatus(mode === "none" ? `Fault cleared on ${id}.` : `Fault armed on ${id} (${mode}). The next request will time out.`, mode === "none" ? "ok" : "warn");
      }
    } catch (e) {
      if (e.status === 504) {
        d.state = "faulted";
        stopStream(d);
        toast("Device timeout (simulated)", `${e.message} Use Recover to reconnect${d.fault === "timeout-always" ? " after clearing the fault" : ""}.`, "error", 9000);
        setStatus(e.message, "bad");
      } else reportError(`${id}: ${act} failed`, e);
    }
    update(id);
    refreshLog();
  }

  async function readTelemetry(id) {
    const d = devices.get(id);
    try {
      d.telemetry = await api.get(`/api/hardware/telemetry/${id}`);
      if (d.fault === "timeout-once") d.fault = "none";
    } catch (e) {
      stopStream(d);
      if (e.status === 504 || e.status === 409) {
        d.state = "faulted";
        toast("Telemetry failed (simulated)", `${e.message} Press Recover to reconnect.`, "error", 9000);
        setStatus(e.message, "bad");
      } else reportError("Telemetry failed", e);
      refreshLog();
    }
    update(id);
  }

  function stopStream(d) {
    if (d.timer) clearInterval(d.timer);
    d.timer = null;
    d.streaming = false;
  }

  async function refreshLog() {
    try {
      const { events } = await api.get("/api/hardware/events");
      const log = document.getElementById("hw-log");
      log.innerHTML = events.length
        ? events.slice().reverse().map((e) => `<div class="e ${e.level}"><span class="ts">${new Date(e.timestampMs).toLocaleTimeString()}</span><span class="lvl">${e.level}</span><span>${escapeHtml(e.deviceId)}: ${escapeHtml(e.message)}</span></div>`).join("")
        : '<span style="color:var(--text-3)">No events.</span>';
    } catch { /* log refresh is best-effort */ }
  }

  grid.addEventListener("click", (e) => {
    const btn = e.target.closest("button[data-act]");
    const card = e.target.closest("[data-id]");
    if (btn && card) action(card.dataset.id, btn.dataset.act);
  });
  document.getElementById("hw-discover").onclick = discover;
  document.getElementById("hw-refresh-log").onclick = refreshLog;
  document.getElementById("hw-diagram").innerHTML =
    '<img src="assets/block_diagram.svg" alt="Block diagram: host PC connected over USB to a microcontroller board, an FPGA board and a JTAG adapter; the JTAG adapter connects to the FPGA JTAG header through a level-translated 6-pin cable" style="width:100%;border-radius:12px;background:#fff">';

  return () => { if (!discovered) discover(); };
}
