// [rc4l] Which actors stand near a point, or in a direction from it, nearest first: the question state.actors leaves to whoever reads its list.
export function actorsNear(actors, { x, y, radius = Infinity, toward = null, cone = 20, cls = null } = {}) {
  const out = [];
  for (const a of actors) {
    const dx = a.x - x, dy = a.y - y;
    const d = Math.hypot(dx, dy);
    if (d > radius) continue;
    if (cls && !cls.test(a.c)) continue;
    let bearing = (Math.atan2(dy, dx) * 180) / Math.PI;
    if (bearing < 0) bearing += 360;
    if (toward !== null) {
      // [rc4l] The smaller way round the circle, so a heading of 359 is next to 1.
      const off = Math.abs(((bearing - toward + 540) % 360) - 180);
      if (off > cone) continue;
    }
    out.push({ ...a, d, bearing });
  }
  return out.sort((p, q) => p.d - q.d);
}
