import { test } from "node:test";
import assert from "node:assert/strict";
import { actorsNear } from "../src/actors.mjs";

const ACTORS = [
  { c: "CatBarrel", x: -4407, y: 4219, z: -32 },
  { c: "PointLight", x: -4065, y: 4347, z: 88 },
  { c: "MapSpot", x: -4220, y: 4276, z: 40 },
  { c: "Keycard_LV7", x: -4397, y: 4499, z: -32 },
];

// [rc4l] wdiexsec's barrel: 299 units west of the eye and only findable by bearing among 750 actors.
test("actors in a cone ahead, nearest first", () => {
  const found = actorsNear(ACTORS, { x: -4116.6, y: 4289.1, toward: 184.4, cone: 20 });
  assert.deepEqual(found.map((a) => a.c), ["MapSpot", "CatBarrel"]);
  assert.ok(Math.abs(found[1].d - 299) < 1);
});

test("a radius and a class narrow the list", () => {
  assert.deepEqual(actorsNear(ACTORS, { x: -4116.6, y: 4289.1, radius: 120 }).map((a) => a.c), ["PointLight", "MapSpot"]);
  assert.deepEqual(actorsNear(ACTORS, { x: -4116.6, y: 4289.1, cls: /barrel/i }).map((a) => a.c), ["CatBarrel"]);
});

test("a cone straddling east wraps round the circle", () => {
  const ring = [{ c: "north-of-east", x: 100, y: 5, z: 0 }, { c: "south-of-east", x: 100, y: -5, z: 0 }, { c: "west", x: -100, y: 0, z: 0 }];
  assert.deepEqual(actorsNear(ring, { x: 0, y: 0, toward: 359, cone: 10 }).map((a) => a.c).sort(), ["north-of-east", "south-of-east"]);
});
