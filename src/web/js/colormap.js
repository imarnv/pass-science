// Perceptually uniform colour map (viridis, sampled at 11 stops) used for the
// temperature legend and point colours: cool = dark blue, hot = yellow.

const STOPS = [
  [0.267, 0.005, 0.329], [0.283, 0.141, 0.458], [0.254, 0.265, 0.530],
  [0.207, 0.372, 0.553], [0.164, 0.471, 0.558], [0.128, 0.567, 0.551],
  [0.135, 0.659, 0.518], [0.267, 0.749, 0.441], [0.478, 0.821, 0.318],
  [0.741, 0.873, 0.150], [0.993, 0.906, 0.144],
];

export function colormap(t) {
  const x = Math.min(1, Math.max(0, t)) * (STOPS.length - 1);
  const i = Math.min(STOPS.length - 2, Math.floor(x));
  const f = x - i;
  const a = STOPS[i], b = STOPS[i + 1];
  return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];
}

export function cssGradient() {
  const parts = [];
  for (let k = 0; k <= 10; k++) {
    const [r, g, b] = colormap(1 - k / 10);
    parts.push(`rgb(${Math.round(r * 255)},${Math.round(g * 255)},${Math.round(b * 255)}) ${k * 10}%`);
  }
  return `linear-gradient(to bottom, ${parts.join(", ")})`;
}
