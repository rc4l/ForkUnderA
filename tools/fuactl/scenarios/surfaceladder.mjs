// [rc4l] The surface derivation, scored against the capture, on every map at once.
//
// fua_surface_verify answers for the level that happens to be loaded, and one map is never enough:
// the rules that hold on a small hand-made level are exactly the ones a 1990s megawad breaks. So
// this walks the list, runs the ladder on each, and prints the three scores side by side -- which
// is what turns "84% on dbab04" into "the alignment rule is weaker on the maps with more sidedefs".
//
// It also carries the A/B for fua_surface_pegrule, because a candidate rule is worth exactly the
// difference it makes to the score and nothing else. Two rounds of believing a rule that correlated
// perfectly and halved the number is why the cvar exists.
//
// usage:  node scenarios/surfaceladder.mjs [--pegrule] [map ...]
import fs from 'node:fs';
import { BridgeClient } from 'file:///F:/ForkUnderA/tools/fuactl/src/client.mjs';
import * as cap from 'file:///F:/ForkUnderA/tools/fuactl/src/capture.mjs';

const args = process.argv.slice(2);
const pegrule = args.includes('--pegrule');
const vshift = args.includes('--vshift');
const MAPS = args.filter(a => !a.startsWith('--'));
const maps = MAPS.length ? MAPS : ['dbab01', 'dbab02', 'dbab04'];

// The play session writes a KEY=VALUE file, not JSON -- so it is read as one.
const sess = Object.fromEntries(fs.readFileSync('F:/ForkUnderA/tools/fuactl/.play-session', 'utf8')
  .split(/\r?\n/).filter(Boolean).map(l => l.split('=')).map(([k, ...v]) => [k.trim(), v.join('=').trim()]));
const PORT = Number(process.env.PORT || sess.PORT || 7799);

const num = (re, s) => { const m = re.exec(s); return m ? m.slice(1).map(Number) : null; };

const c = new BridgeClient();
await c.connect(PORT, { token: sess.TOKEN });
// [rc4l] Let the level that launched with the engine finish arriving before changing it.
// A `map` issued while the first level is still baking its mesh takes the process down, and the
// symptom is a closed bridge with nothing in the log -- which reads as a tool bug for a while.
await cap.waitTics(c, 35);
await cap.exec(c, `fua_surface_pegrule ${pegrule ? 1 : 0}`);
await cap.exec(c, `fua_surface_vshift ${vshift ? 1 : 0}`);

const rows = [];
for (const m of maps) {
  await cap.exec(c, `map ${m}`, { quietMs: 900, maxMs: 25000 });
  // The ladder reads the CAPTURE, so there has to be one -- and not a half-built one. Twelve tics
  // was enough for a frame and not enough for the level to settle, and the difference between the
  // two was a dead engine roughly every other run.
  await cap.waitTics(c, 70);
  const out = await cap.exec(c, 'fua_surface_verify', { quietMs: 700, maxMs: 30000 });
  // [rc4l] The ladder names the level it ran on, so the map change is CHECKED rather than assumed.
  // A `map` for a level the loaded wads do not have fails quietly and leaves the old one up, and
  // the first run of this printed three identical rows for three different maps because of it.
  const on = /fua_surface_verify on (\S+):/.exec(out);
  if (!on || on[1].toLowerCase() !== m.toLowerCase()) {
    console.log(`  ${m}: NOT LOADED (still on ${on ? on[1] : '?'}) -- is the wad on the command line?`);
    continue;
  }
  const geom = num(/(\d+) of (\d+) captured pieces agree/, out);
  const uv = num(/alignment: (\d+) of (\d+) agree/, out);
  const pl = num(/planes: (\d+) of (\d+) captured flats/, out);
  const cls = num(/(\d+) off by the peg shift, (\d+) off by the peg shift the other way/, out);
  const cls2 = num(/(\d+) off by a whole texture[^]*?(\d+) off by something else/, out);
  rows.push({ map: m, geom, uv, pl, cls, cls2, out });
}
await c.close();

const pct = (a) => a && a[1] ? (100 * a[0] / a[1]).toFixed(1) + '%' : '--';
console.log(`\nfua_surface_pegrule ${pegrule ? 1 : 0}, fua_surface_vshift ${vshift ? 1 : 0}\n`);
console.log('map          geometry  alignment  planes    of the misses: peg / peg-other / whole-tex / unexplained');
for (const r of rows) {
  const miss = r.cls && r.cls2 ? `${r.cls[0]} / ${r.cls[1]} / ${r.cls2[0]} / ${r.cls2[1]}` : '--';
  console.log(`${r.map.padEnd(12)} ${pct(r.geom).padEnd(9)} ${pct(r.uv).padEnd(10)} ${pct(r.pl).padEnd(9)} ${miss}`);
}
// The unexplained ones are the only lines worth reading in full, so they are the only ones echoed.
for (const r of rows) {
  const lines = r.out.split('\n').filter(l => /seg \d+ |off by .* textures/.test(l));
  if (lines.length) console.log(`\n${r.map}:\n` + lines.map(l => '  ' + l.trim()).join('\n'));
}
