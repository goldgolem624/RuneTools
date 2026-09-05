// CS2 script comparison: two decompiled-script folders in, a browsable HTML diff out.
//
//   node tools\cs2_diff.js <oldDir> <newDir> <outDir> [label]
//
// Each input dir holds clientscript-<id>.ts files, either directly or in a cs2/ or
// scripts/ subfolder (both layouts the extractor has produced) -- auto-detected.
// Output: <outDir>/index.html (churn-sorted overview), changed/<name>.html (inline
// unified diff, full file as context), added/<name>.html, removed/<name>.html, and
// cs2_diff.json (machine-readable summary). Matches the report style of the
// cs2_changes_* folders in Downloads.

const fs = require('fs');
const path = require('path');

function scriptsDirOf(dir) {
  for (const sub of ['', 'cs2', 'scripts']) {
    const d = path.join(dir, sub);
    try {
      if (fs.readdirSync(d).some(f => /^clientscript-\d+\.ts$/.test(f))) return d;
    } catch (e) {}
  }
  return null;
}

// Myers O(ND) diff over line arrays -> array of [op, line], op: ' ' | '+' | '-'.
function diffLines(a, b) {
  const N = a.length, M = b.length, MAX = N + M;
  if (!MAX) return [];
  const vArr = new Int32Array(2 * MAX + 1);
  const trace = [];
  let found = -1;
  for (let d = 0; d <= MAX && found < 0; d++) {
    trace.push(vArr.slice());
    for (let k = -d; k <= d; k += 2) {
      let x;
      if (k === -d || (k !== d && vArr[MAX + k - 1] < vArr[MAX + k + 1])) x = vArr[MAX + k + 1];
      else x = vArr[MAX + k - 1] + 1;
      let y = x - k;
      while (x < N && y < M && a[x] === b[y]) { x++; y++; }
      vArr[MAX + k] = x;
      if (x >= N && y >= M) { found = d; break; }
    }
  }
  // Backtrack.
  const out = [];
  let x = N, y = M;
  for (let d = found; d > 0; d--) {
    const v = trace[d];
    const k = x - y;
    let prevK;
    if (k === -d || (k !== d && v[MAX + k - 1] < v[MAX + k + 1])) prevK = k + 1;
    else prevK = k - 1;
    const prevX = v[MAX + prevK], prevY = prevX - prevK;
    while (x > prevX && y > prevY) { out.push([' ', a[--x]]); y--; }
    if (x === prevX) out.push(['+', b[--y]]);
    else out.push(['-', a[--x]]);
  }
  while (x > 0 && y > 0) { out.push([' ', a[--x]]); y--; }
  while (x > 0) out.push(['-', a[--x]]);
  while (y > 0) out.push(['+', b[--y]]);
  return out.reverse();
}

const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const STYLE = '<style>body{background:#0d1017;color:#d7dce6;font:13px/1.5 Consolas,monospace;margin:0}' +
  'h1{font-size:15px;color:#8b7bf7;padding:10px 14px;margin:0;border-bottom:1px solid #262d3d}' +
  'pre{margin:0;padding:6px 0}div{padding:0 12px;white-space:pre-wrap;word-break:break-all}' +
  '.a{background:#1d3324;color:#7ee08a}.d{background:#3a1d22;color:#ff8896}' +
  '.h{background:#101522;color:#8b7bf7;padding:3px 12px}a{color:#8b7bf7;text-decoration:none}' +
  'a:hover{text-decoration:underline}li{margin:2px 0}.n{color:#8a93a6}</style>';
const page = (title, h1, body) =>
  '<!DOCTYPE html><meta charset="utf-8"><title>' + esc(title) + '</title>' + STYLE +
  '<h1>' + h1 + '</h1><pre>' + body + '</pre>';
const pill = (add, del) => '<span class="a">+' + add + '</span> <span class="d">-' + del + '</span>';

function main() {
  const [oldArg, newArg, outArg, label] = process.argv.slice(2);
  if (!oldArg || !newArg || !outArg) {
    console.error('usage: node cs2_diff.js <oldDir> <newDir> <outDir> [label]');
    process.exit(2);
  }
  const oldDir = scriptsDirOf(oldArg), newDir = scriptsDirOf(newArg);
  if (!oldDir) { console.error('no clientscript-*.ts under ' + oldArg); process.exit(1); }
  if (!newDir) { console.error('no clientscript-*.ts under ' + newArg); process.exit(1); }
  const tag = label || new Date().toISOString().slice(0, 10);

  const listIds = d => new Set(fs.readdirSync(d).filter(f => /^clientscript-\d+\.ts$/.test(f)).map(f => f.slice(0, -3)));
  const oldIds = listIds(oldDir), newIds = listIds(newDir);
  const read = (d, n) => fs.readFileSync(path.join(d, n + '.ts'), 'utf8').split(/\r?\n/);

  fs.mkdirSync(path.join(outArg, 'changed'), { recursive: true });
  fs.mkdirSync(path.join(outArg, 'added'), { recursive: true });
  fs.mkdirSync(path.join(outArg, 'removed'), { recursive: true });

  const changed = [], added = [], removed = [];
  const byNum = (a, b) => parseInt(a.slice(13), 10) - parseInt(b.slice(13), 10);

  for (const n of [...newIds].sort(byNum)) {
    if (!oldIds.has(n)) {
      const lines = read(newDir, n);
      added.push({ n, add: lines.length });
      fs.writeFileSync(path.join(outArg, 'added', n + '.html'),
        page(n + ' (added)', esc(n) + ' &middot; <span class="a">added, ' + lines.length + ' lines</span>',
             lines.map(l => '<div class="a">+ ' + esc(l) + '</div>').join('')));
      continue;
    }
    const a = read(oldDir, n), b = read(newDir, n);
    if (a.length === b.length && a.every((l, i) => l === b[i])) continue;
    const ops = diffLines(a, b);
    let add = 0, del = 0;
    const body = ops.map(([op, l]) => {
      if (op === '+') { add++; return '<div class="a">+ ' + esc(l) + '</div>'; }
      if (op === '-') { del++; return '<div class="d">- ' + esc(l) + '</div>'; }
      return '<div>  ' + esc(l) + '</div>';
    }).join('');
    if (!add && !del) continue;
    changed.push({ n, add, del });
    fs.writeFileSync(path.join(outArg, 'changed', n + '.html'),
      page(n + ' diff', esc(n) + ' &middot; ' + pill(add, del), body));
  }
  for (const n of [...oldIds].sort(byNum)) {
    if (newIds.has(n)) continue;
    const lines = read(oldDir, n);
    removed.push({ n, del: lines.length });
    fs.writeFileSync(path.join(outArg, 'removed', n + '.html'),
      page(n + ' (removed)', esc(n) + ' &middot; <span class="d">removed, ' + lines.length + ' lines</span>',
           lines.map(l => '<div class="d">- ' + esc(l) + '</div>').join('')));
  }

  changed.sort((x, y) => (y.add + y.del) - (x.add + x.del));
  let idx = '<ul>';
  for (const c of changed)
    idx += '<li><a href="changed/' + c.n + '.html">' + c.n + '</a> ' + pill(c.add, c.del) + '</li>';
  if (added.length) {
    idx += '</ul><div class="h">Added</div><ul>';
    for (const c of added)
      idx += '<li><a href="added/' + c.n + '.html">' + c.n + '</a> <span class="a">+' + c.add + '</span></li>';
  }
  if (removed.length) {
    idx += '</ul><div class="h">Removed</div><ul>';
    for (const c of removed)
      idx += '<li><a href="removed/' + c.n + '.html">' + c.n + '</a> <span class="d">-' + c.del + '</span></li>';
  }
  idx += '</ul>';
  fs.writeFileSync(path.join(outArg, 'index.html'),
    page('CS2 changes',
         'CS2 changes &middot; ' + esc(tag) + ' &middot; ' + changed.length + ' changed, ' +
         added.length + ' added, ' + removed.length + ' removed', idx));
  fs.writeFileSync(path.join(outArg, 'cs2_diff.json'),
    JSON.stringify({ label: tag, old: oldArg, new: newArg, changed, added, removed }, null, 1));
  console.log('CS2 diff: ' + changed.length + ' changed, ' + added.length + ' added, ' +
              removed.length + ' removed -> ' + path.join(outArg, 'index.html'));
}
main();
