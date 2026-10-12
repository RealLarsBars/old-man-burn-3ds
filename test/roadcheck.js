// Builds a map with the road code lifted straight out of web/game.js and prints it, so the 3DS port can be diffed against it.
// usage: node roadcheck.js <seed> [distance px]
const fs = require('fs'), path = require('path');
// GAME_JS points at the web game's web/game.js; by default a checkout of old-man-burn next to this repo.
const src = fs.readFileSync(process.env.GAME_JS || path.join(__dirname, '../../old-man-burn/web/game.js'), 'utf8');
const cut = (a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i); if (i < 0 || j < 0) throw new Error('marker ' + a); return src.slice(i, j); };
const code = [
  "const LW = 480, LH = 270, HOR = 104, M_PX = 6; const clamp = (v, a, b) => Math.max(a, Math.min(b, v)); const rnd = Math.round;",
  cut('function hash(a, b)', '// Online (Discord)'),
  "const V2_FROM = 20737; let V2 = false, BIKE_SCALE = 1, BODY_MID = 16, PROBE = 20;",
  cut('function setVersion', 'const NAMES_A'),
  cut('const pace = m =>', '/* ---------- storage'),
].join('\n');
const seed = +process.argv[2], dist = +(process.argv[3] || 120000);
const run = new Function('SEED', 'dist', code + `
  setVersion(SEED >= 1000000 || SEED >= V2_FROM);
  genTo(dist); const out = [];
  for (let x = 0; x < dist - 3000; x += 37) { roadAt(x); const g = gapAt(x); out.push('r ' + x + ' ' + RC.toFixed(6) + ' ' + RW.toFixed(6) + ' ' + (g ? g.x : -1).toFixed(6)); }
  for (const r of rails.filter(r => r.x < dist - 3000)) out.push('b ' + r.x.toFixed(6) + ' ' + r.len.toFixed(6) + ' ' + r.side);
  for (const e of edges.filter(r => r.x < dist - 3000)) out.push('e ' + e.x.toFixed(6) + ' ' + e.len.toFixed(6));
  for (let y = 112; y < 266; y += 3) for (let x = 2000; x < dist - 3000; x += 211) out.push('h ' + x + ' ' + y + ' ' + (hitRail(x, y, y - 2) ? 1 : 0) + (hitEdge(x, y, y + 2) ? 1 : 0));
  return out.join('\\n');`);
console.log(run(seed, dist));
