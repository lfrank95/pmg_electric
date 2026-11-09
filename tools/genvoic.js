// Griff-Generator: für jeden Grundton und Akkordtyp alle spielbaren Griffe suchen und bewerten (wie ein Gitarrist sie kennt:
// offen, E-/A-/D-/C-/G-Form am Hals entlang, Griffe ohne Bass, gedämpfte Saiten). Ergebnis: tools/voicings_gen.json
const OPEN = [4, 9, 2, 7, 11, 4], MIDI_OPEN = [40, 45, 50, 55, 59, 64];
const ROOTS = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'B', 'H'];
// Akkordtypen: Töne (Intervalle) und welche weggelassen werden dürfen (Quinte fast immer, bei Optionsakkorden auch die None)
const Q = {
  '': { iv: [0, 4, 7] }, 'm': { iv: [0, 3, 7] }, '5': { iv: [0, 7] }, 'dim': { iv: [0, 3, 6] }, 'aug': { iv: [0, 4, 8] },
  'sus2': { iv: [0, 2, 7] }, 'sus4': { iv: [0, 5, 7] }, 'add9': { iv: [0, 4, 7, 2], opt: [7] }, 'madd9': { iv: [0, 3, 7, 2], opt: [7] },
  '6': { iv: [0, 4, 7, 9], opt: [7] }, 'm6': { iv: [0, 3, 7, 9], opt: [7] }, '7': { iv: [0, 4, 7, 10], opt: [7] }, 'maj7': { iv: [0, 4, 7, 11], opt: [7] },
  'm7': { iv: [0, 3, 7, 10], opt: [7] }, 'mmaj7': { iv: [0, 3, 7, 11], opt: [7] }, 'm7b5': { iv: [0, 3, 6, 10] }, 'dim7': { iv: [0, 3, 6, 9] },
  '7sus4': { iv: [0, 5, 7, 10], opt: [7] }, '9': { iv: [0, 4, 7, 10, 2], opt: [7] }, 'maj9': { iv: [0, 4, 7, 11, 2], opt: [7] }, 'm9': { iv: [0, 3, 7, 10, 2], opt: [7] },
  '7b9': { iv: [0, 4, 7, 10, 1], opt: [7] }, '7#9': { iv: [0, 4, 7, 10, 3], opt: [7] }, '11': { iv: [0, 4, 7, 10, 2, 5], opt: [7, 2, 4] },
  'm11': { iv: [0, 3, 7, 10, 2, 5], opt: [7, 2] }, '13': { iv: [0, 4, 7, 10, 2, 9], opt: [7, 2] }, '6/9': { iv: [0, 4, 7, 9, 2], opt: [7] }, '7#5': { iv: [0, 4, 8, 10] }
};
function fingersNeeded(fr) {
  const fretted = fr.map((f, s) => [f, s]).filter(([f]) => f > 0); if (!fretted.length) return 0;
  const mn = Math.min(...fretted.map(([f]) => f));
  // Barré am tiefsten Bund, wenn er auf ≥2 Saiten liegt und keine Saite dahinter tiefer/offen ist
  const sMin = fretted.filter(([f]) => f === mn).map(([, s]) => s), s0 = Math.min(...sMin);
  const barre = sMin.length >= 2 && fr.slice(s0).every(f => f < 0 || f >= mn);
  let n = barre ? 1 : 0;
  for (const [f, s] of fretted) { if (barre && f === mn) continue; n++; }
  return n;
}
function gen(root, q) {
  const { iv, opt = [] } = Q[q], pcs = iv.map(i => (root + i) % 12), need = iv.filter(i => !opt.includes(i)).map(i => (root + i) % 12);
  const out = [];
  for (let p = 0; p <= 12; p++) {
    const lo = Math.max(1, p), hi = p + 3;
    const opts = [];
    for (let s = 0; s < 6; s++) {
      const o = [-1];
      if (p <= 5 && pcs.includes(OPEN[s])) o.push(0);
      for (let f = lo; f <= hi; f++) if (pcs.includes((OPEN[s] + f) % 12)) o.push(f);
      opts.push(o);
    }
    const fr = [0, 0, 0, 0, 0, 0];
    (function rec(s) {
      if (s === 6) {
        const played = fr.map((f, i) => [f, i]).filter(([f]) => f >= 0); if (played.length < 3) return;
        const first = played[0][1], last = played[played.length - 1][1];
        let inner = 0; for (let i = first; i <= last; i++) if (fr[i] < 0) inner++; if (inner > 1) return;
        const bassPc = (OPEN[first] + fr[first]) % 12; if (bassPc !== root) return;
        const have = new Set(played.map(([f, i]) => (OPEN[i] + f) % 12)); for (const n of need) if (!have.has(n)) return;
        const fretted = played.filter(([f]) => f > 0).map(([f]) => f); if (fretted.length && Math.min(...fretted) !== Math.max(p, Math.min(...fretted)) ) {}
        if (fretted.length && Math.max(...fretted) - Math.min(...fretted) > 3) return;
        if (fingersNeeded(fr) > 4) return;
        // offene Saiten nur, wenn der Griff unten liegt (höchster gegriffener Bund ≤ 4)
        if (fretted.length && Math.max(...fretted) > 4 && played.some(([f]) => f === 0)) return;
        out.push(fr.slice());
        return;
      }
      for (const f of opts[s]) { fr[s] = f; rec(s + 1); }
    })(0);
  }
  // doppelte entfernen, bewerten
  const seen = new Set(), res = [];
  for (const fr of out) {
    const k = fr.join(','); if (seen.has(k)) continue; seen.add(k);
    const played = fr.filter(f => f >= 0).length, fretted = fr.filter(f => f > 0), mn = fretted.length ? Math.min(...fretted) : 0, mx = fretted.length ? Math.max(...fretted) : 0;
    const first = fr.findIndex(f => f >= 0), last = 5 - [...fr].reverse().findIndex(f => f >= 0);
    let inner = 0; for (let i = first; i <= last; i++) if (fr[i] < 0) inner++;
    const opens = fr.filter(f => f === 0).length, fing = fingersNeeded(fr);
    let sc = 10 * played - 6 * inner - 2 * (mx - mn) - 1.5 * fing + (mn <= 3 ? 2 * opens : 0) - 0.3 * mn - (last < 5 ? 3 : 0) - 4 * first;
    // jede Akkordnote möglichst oben (Melodiestimme sauber), Terz nicht doppelt
    const thirdPc = (root + (Q[q].iv.includes(3) ? 3 : 4)) % 12; const thirds = fr.filter((f, s) => f >= 0 && (OPEN[s] + f) % 12 === thirdPc).length; if (thirds > 1) sc -= 2;
    const name = first === 0 ? 'E-Form' : first === 1 ? 'A-Form' : first === 2 ? 'D-Form' : 'oben';
    const src = (opens && mn <= 3) ? 'offen' : name + (mn ? ' Bund ' + mn : '');
    res.push({ fr, src, sc: Math.round(sc * 10) / 10 });
  }
  res.sort((a, b) => b.sc - a.sc);
  // nicht zu viele ähnliche: pro Lage (min. Bund) und Bass-Saite höchstens 2
  const cnt = {}, pick = [];
  for (const r of res) { const mn = Math.min(...r.fr.filter(f => f > 0).concat([0])), key = r.fr.findIndex(f => f >= 0) + ':' + Math.floor(mn / 2); cnt[key] = (cnt[key] || 0) + 1; if (cnt[key] <= 2) pick.push(r); if (pick.length >= 16) break; }
  pick.sort((a, b) => lage(a.fr) - lage(b.fr) || b.sc - a.sc);
  return pick;
}
function lage(fr) { const f = fr.filter(x => x > 0); return f.length ? Math.min(...f) - (fr.includes(0) ? 0.5 : 0) : -1; }
const OUT = {}; let total = 0;
for (let r = 0; r < 12; r++) for (const q of Object.keys(Q)) { const v = gen(r, q); if (v.length) { OUT[ROOTS[r] + q] = v; total += v.length; } }
require('fs').writeFileSync(__dirname + '/voicings_gen.json', JSON.stringify({ quals: Object.fromEntries(Object.entries(Q).map(([k, v]) => [k, v.iv])), opt: Object.fromEntries(Object.entries(Q).map(([k, v]) => [k, v.opt || []])), voicings: OUT }));
console.log('Akkorde', Object.keys(OUT).length, 'Griffe', total);
for (const n of ['C', 'Am', 'G', 'F', 'E7#9', 'Bm7b5', 'D9', 'Hm7b5', 'C13']) console.log(n, (OUT[n] || []).slice(0, 8).map(v => v.fr.map(f => f < 0 ? 'x' : f).join('') + '(' + v.src + ')').join(' '));
