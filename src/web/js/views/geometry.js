// Geometry view: ASCII STL import (parsed in C++) and interactive display.

import { api, decodeBase64 } from "../api.js";
import { bytes, enableDrop, escapeHtml, fmt, kv, readFileText, reportError, setStatus, toast } from "../ui.js";
import { THREE, Viewer3D } from "../viewer3d.js";

export function initGeometry() {
  const container = document.getElementById("geo-viewer");
  const fileInput = document.getElementById("geo-file");
  const toolbar = document.getElementById("geo-toolbar");
  let viewer = null;
  let mesh = null, edges = null, current = null;

  const ensureViewer = () => (viewer ??= new Viewer3D(container));

  async function load(name, text) {
    setStatus(`Parsing ${name} in the C++ engine…`, "warn pulse");
    try {
      const result = await api.postText("/api/geometry/stl", text);
      show(name, result);
      setStatus(`Loaded ${name}: ${result.triangles.toLocaleString()} triangles parsed in ${result.parseMs.toFixed(1)} ms.`, "ok");
      if (result.degenerateTriangles) {
        toast("Degenerate triangles", `${result.degenerateTriangles} zero-area facets were kept but have no normal.`, "warning");
      }
    } catch (e) {
      reportError(`Could not import ${name}`, e);
    }
  }

  function show(name, data) {
    const v = ensureViewer();
    v.clear();
    const positions = decodeBase64(data.positions, Float32Array);
    const faceNormals = decodeBase64(data.normals, Float32Array);
    const normals = new Float32Array(positions.length);
    for (let t = 0; t < faceNormals.length / 3; t++) {
      for (let k = 0; k < 3; k++) normals.set(faceNormals.subarray(3 * t, 3 * t + 3), 9 * t + 3 * k);
    }
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute("normal", new THREE.BufferAttribute(normals, 3));
    mesh = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({
      color: 0xb8bec8, metalness: 0.15, roughness: 0.55, side: THREE.DoubleSide,
      polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1,
    }));
    edges = new THREE.LineSegments(new THREE.EdgesGeometry(geometry, 30), new THREE.LineBasicMaterial({ color: 0x3a3a3c, transparent: true, opacity: 0.55 }));
    edges.visible = toolbar.querySelector('[data-act="edges"]').getAttribute("aria-pressed") === "true";
    mesh.material.wireframe = toolbar.querySelector('[data-act="wire"]').getAttribute("aria-pressed") === "true";
    v.add(mesh);
    v.add(edges);
    v.setAxesVisible(toolbar.querySelector('[data-act="axes"]').getAttribute("aria-pressed") === "true");
    v.view("iso");
    current = { name, data };
    document.getElementById("geo-empty").hidden = true;
    toolbar.hidden = false;
    document.getElementById("geo-hint").hidden = false;
    renderInfo();
  }

  function renderInfo() {
    if (!current) return;
    const { name, data } = current;
    // STL has no units: the selection only labels the numbers, it never rescales them.
    const u = document.getElementById("geo-units").value;
    const [min, max] = [data.bounds.min, data.bounds.max];
    const size = max.map((m, i) => m - min[i]);
    document.getElementById("geo-filename").textContent = name;
    kv(document.getElementById("geo-info"), [
      ["Solid name", escapeHtml(data.name || "—")],
      ["Triangles", data.triangles.toLocaleString()],
      ["Degenerate", String(data.degenerateTriangles)],
      ["Size (X × Y × Z)", `${size.map((s) => fmt(s, 4)).join(" × ")} ${u}`],
      ["Min corner", `${min.map((s) => fmt(s, 4)).join(", ")} ${u}`],
      ["Surface area", `${fmt(data.surfaceArea, 5)} ${u}²`],
      ["File size", bytes(data.bytes)],
      ["Parse time (C++)", `${data.parseMs.toFixed(2)} ms`],
    ]);
  }

  async function openFile(file) {
    try {
      await load(file.name, await readFileText(file));
    } catch (e) {
      reportError(`Could not open ${file.name}`, e);
    }
  }

  document.getElementById("geo-open").onclick = () => fileInput.click();
  fileInput.onchange = () => { if (fileInput.files[0]) openFile(fileInput.files[0]); fileInput.value = ""; };
  document.getElementById("geo-example").onclick = async () => {
    try {
      const res = await fetch("examples/heatsink.stl");
      if (!res.ok) throw new Error("The bundled example file is missing from app/examples.");
      await load("heatsink.stl (example)", await res.text());
    } catch (e) { reportError("Could not load the example", e); }
  };
  document.getElementById("geo-units").onchange = renderInfo;
  enableDrop(container, openFile);

  toolbar.addEventListener("click", (e) => {
    const btn = e.target.closest("button");
    if (!btn || !viewer) return;
    const act = btn.dataset.act;
    if (act === "fit") viewer.fit();
    else if (["iso", "top", "front"].includes(act)) viewer.view(act);
    else {
      const on = btn.getAttribute("aria-pressed") !== "true";
      btn.setAttribute("aria-pressed", String(on));
      if (act === "wire" && mesh) mesh.material.wireframe = on;
      if (act === "edges" && edges) edges.visible = on;
      if (act === "axes") viewer.setAxesVisible(on);
    }
  });

  return () => viewer?.resize();
}
