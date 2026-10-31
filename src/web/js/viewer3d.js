// three.js wrapper used by the Geometry and Nodal Results views: one scene,
// orbit controls (rotate / pan / zoom), fit-to-view, preset views and picking.

import * as THREE from "three";
import { OrbitControls } from "../vendor/three/OrbitControls.js";

export class Viewer3D {
  constructor(container) {
    this.container = container;
    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    container.prepend(this.renderer.domElement);
    this.renderer.domElement.tabIndex = 0;

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(35, 1, 0.01, 1e6);
    this.camera.up.set(0, 0, 1); // engineering convention: Z up
    this.camera.position.set(1, -1.4, 1);

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.12;
    this.controls.screenSpacePanning = true;
    // Shift + left drag pans (in addition to right drag).
    this.renderer.domElement.addEventListener("pointerdown", (e) => {
      this.controls.mouseButtons.LEFT = e.shiftKey ? THREE.MOUSE.PAN : THREE.MOUSE.ROTATE;
    }, true);

    this.scene.add(new THREE.HemisphereLight(0xffffff, 0x8a8f99, 1.6));
    const key = new THREE.DirectionalLight(0xffffff, 1.6);
    key.position.set(1, -1.5, 2);
    this.camera.add(key); // light follows the camera so faces never go black
    this.scene.add(this.camera);

    this.content = new THREE.Group();
    this.scene.add(this.content);
    this.axes = null;
    this.bounds = null;

    this.raycaster = new THREE.Raycaster();
    this._resize = () => this.resize();
    new ResizeObserver(this._resize).observe(container);
    this.resize();
    this.renderer.setAnimationLoop(() => {
      this.controls.update();
      this.renderer.render(this.scene, this.camera);
    });
    this.renderer.domElement.addEventListener("keydown", (e) => {
      if (e.key === "f" || e.key === "F") this.fit();
    });
  }

  resize() {
    const w = this.container.clientWidth, h = this.container.clientHeight;
    if (!w || !h) return;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  clear() {
    for (const child of [...this.content.children]) {
      this.content.remove(child);
      child.traverse?.((o) => { o.geometry?.dispose(); o.material?.dispose?.(); });
    }
    if (this.axes) { this.scene.remove(this.axes); this.axes = null; }
    this.bounds = null;
  }

  add(object) {
    this.content.add(object);
    this.bounds = new THREE.Box3().setFromObject(this.content);
    this._makeAxes();
  }

  _makeAxes() {
    if (this.axes) this.scene.remove(this.axes);
    const size = this.bounds.getSize(new THREE.Vector3());
    const len = Math.max(size.x, size.y, size.z) * 0.25 || 1;
    this.axes = new THREE.AxesHelper(len);
    this.axes.position.copy(this.bounds.min);
    this.axes.material.depthTest = false;
    this.axes.renderOrder = 10;
    this.scene.add(this.axes);
  }

  setAxesVisible(visible) { if (this.axes) this.axes.visible = visible; }

  // Frames the whole model; `direction` is the camera offset direction.
  fit(direction) {
    if (!this.bounds || this.bounds.isEmpty()) return;
    const center = this.bounds.getCenter(new THREE.Vector3());
    const radius = this.bounds.getBoundingSphere(new THREE.Sphere()).radius || 1;
    const dir = (direction || this.camera.position.clone().sub(this.controls.target)).clone().normalize();
    if (dir.lengthSq() === 0) dir.set(1, -1.4, 1).normalize();
    const fov = THREE.MathUtils.degToRad(this.camera.fov);
    const distance = radius / Math.sin(Math.min(fov, fov * this.camera.aspect) / 2) * 1.08;
    this.controls.target.copy(center);
    this.camera.position.copy(center).addScaledVector(dir, distance);
    this.camera.near = distance / 1000;
    this.camera.far = distance * 100;
    this.camera.updateProjectionMatrix();
    this.controls.update();
  }

  view(name) {
    const dirs = { iso: new THREE.Vector3(1, -1.4, 1), top: new THREE.Vector3(0, -0.0001, 1), front: new THREE.Vector3(0, -1, 0.0001) };
    this.fit(dirs[name] || dirs.iso);
  }

  // Returns the index of the nearest point of `points` under the pointer, or -1.
  pickPoint(event, points, pixelRadius = 8) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const ndc = new THREE.Vector2(((event.clientX - rect.left) / rect.width) * 2 - 1, -((event.clientY - rect.top) / rect.height) * 2 + 1);
    const radius = this.bounds.getBoundingSphere(new THREE.Sphere()).radius;
    const distance = this.camera.position.distanceTo(this.controls.target);
    // Convert the pixel tolerance into a world-space threshold at the target depth.
    const worldPerPixel = (2 * distance * Math.tan(THREE.MathUtils.degToRad(this.camera.fov) / 2)) / rect.height;
    this.raycaster.params.Points.threshold = Math.max(worldPerPixel * pixelRadius, radius * 1e-4);
    this.raycaster.setFromCamera(ndc, this.camera);
    const hits = this.raycaster.intersectObject(points);
    if (!hits.length) return -1;
    hits.sort((a, b) => a.distanceToRay - b.distanceToRay || a.distance - b.distance);
    return hits[0].index;
  }
}

export { THREE };
