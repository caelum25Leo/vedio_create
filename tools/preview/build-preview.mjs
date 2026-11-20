#!/usr/bin/env node
/*
 * 把分镜 JS（含小程序 canvas API）打包成一个独立的 preview.html：
 *   node tools/preview/build-preview.mjs films/earth-to-fukushima
 *   ... --verify        打完用 msedge 无头开一次，确认没报错、每格都画出了东西
 *   ... --stills        打完把 storyboard/ 里每个 JS 各渲一张 png 到 out/preview/，
 *                       给输入界面的「预览」对话框看（Qt 里没地方跑 JS，只能看图）
 *
 * 为什么自己写打包：仓库里既没有 esbuild 也没有 rollup，而且是零网络策略
 * （TECHNIQUE.md §1：模式 D 全程不需要装任何东西）。这里的模块语法面很窄，
 * 支持的形式列在 SUPPORTED 里；**碰到没把握的写法直接报错退出**，
 * 不打一个"看着像、其实错了"的包 —— 那种包在预览里画出来的东西不能信。
 *
 * 出来的 preview.html 是普通 <script>，不用 module，所以：
 *   双击就能开（file://，不需要起服务）；
 *   UI 那边 QDesktopServices::openUrl 能直接指向它；
 *   --verify 用 playwright 也只需要 goto 一个 file:// 路径。
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const toolDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(toolDir, '../..');

/* ---------------- 参数 ---------------- */
const argv = process.argv.slice(2);
const filmDir = path.resolve(argv[0] && !argv[0].startsWith('--') ? argv[0] : process.cwd());
const opt = (name, dflt) => {
  const hit = argv.find((a) => a.startsWith(`--${name}=`));
  return hit ? hit.slice(name.length + 3) : dflt;
};
const has = (flag) => argv.includes(`--${flag}`);

const mainRel = opt('main', fs.existsSync(path.join(filmDir, 'film.js')) ? 'film.js' : null);
const outRel = opt('out', 'preview.html');
const boardRel = opt('board', 'storyboard');

function rel(...parts) { return path.relative(filmDir, path.join(filmDir, ...parts)).replace(/\\/g, '/'); }
function abs(id) { return path.join(filmDir, id); }
function exists(id) { return fs.existsSync(abs(id)); }

/* ---------------- 找分镜模块 ---------------- */
// 认法：默认导出里有 id 和 draw( —— 那就是"一格一屏"的那个模块，小程序页面调的就是它
function discoverShots() {
  const dir = path.join(filmDir, boardRel);
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir)
    .filter((f) => /\.m?js$/i.test(f))
    .map((f) => rel(boardRel, f))
    .filter((id) => {
      const src = fs.readFileSync(abs(id), 'utf8');
      return /export\s+default\s*\{[\s\S]*\bdraw\s*\(/.test(src);
    });
}
const shots = (opt('shots', '') || '').split(',').filter(Boolean).map((s) => s.replace(/\\/g, '/'));
const shotIds = shots.length ? shots : discoverShots();

/* ---------------- 模块转换 ---------------- */
const SUPPORTED = '支持：import {a, b as c} / import * as ns / import def / import "x" / export function|class|const|let|var|default|{a,b}|* from';

/** 把 specifier 解析成包内 id（相对片子目录，正斜杠）。 */
function resolveId(fromId, spec) {
  if (!spec.startsWith('.') && !spec.startsWith('/')) throw new Error(`${fromId}：import 了包外模块 "${spec}"。预览只带片子自己的文件；第三方要么去掉，要么拷进 ${boardRel}/。`);
  const base = path.posix.dirname(fromId);
  let id = path.posix.normalize(path.posix.join(base, spec));
  if (exists(id)) return id;
  if (exists(id + '.js')) return id + '.js';
  if (exists(id + '.mjs')) return id + '.mjs';
  if (exists(path.posix.join(id, 'index.js'))) return path.posix.join(id, 'index.js');
  throw new Error(`${fromId}：找不到 "${spec}"（按 ${id} 找过，带 .js/.mjs/index.js 也找过）`);
}

/** 只用在行首的 import/export 上；跨行的 import {} 也能吃到。 */
function transform(id, source) {
  const named = [];
  const bad = [];
  let out = source;

  // ---- import：先副作用形式，再带绑定的（绑定部分不含引号和分号，所以能跨行）----
  out = out.replace(/^[ \t]*import\s*['"]([^'"]+)['"]\s*;?[ \t]*$/gm,
    (m, spec) => `__req(${JSON.stringify(resolveId(id, spec))});`);
  out = out.replace(/^[ \t]*import\s+([^'";]*?)\s*from\s*['"]([^'"]+)['"]\s*;?[ \t]*$/gm, (m, binding, spec) => {
    const req = `__req(${JSON.stringify(resolveId(id, spec))})`;
    const b = binding.trim();
    const lines = [];
    const star = b.match(/^\*\s+as\s+([A-Za-z_$][\w$]*)$/);
    const braces = b.match(/\{([^}]*)\}/);
    const head = b.replace(/\{[^]*\}/, '').replace(/,\s*$/, '').trim();
    if (star) lines.push(`const ${star[1]} = ${req};`);
    if (braces) {
      const pairs = braces[1].split(',').map((s) => s.trim()).filter(Boolean).map((s) => {
        const as = s.split(/\s+as\s+/);
        return as.length === 2 ? `${as[0].trim()}: ${as[1].trim()}` : s;
      });
      lines.push(`const { ${pairs.join(', ')} } = ${req};`);
    }
    const plain = head && !star ? head.split(',')[0].trim() : '';
    if (plain && /^[A-Za-z_$][\w$]*$/.test(plain)) lines.push(`const ${plain} = ${req}.default;`);
    else if (head && !braces && !star) bad.push(`${id}：import 绑定看不懂 → ${head}`);
    if (!lines.length) { bad.push(`${id}：import 绑定看不懂 → ${b}`); return m; }
    return lines.join(' ');
  });

  // ---- export ----
  out = out.replace(/^[ \t]*export\s+default\s+/gm, '__exp.default = ');
  out = out.replace(/^[ \t]*export\s*\*\s*from\s*['"]([^'"]+)['"]\s*;?[ \t]*$/gm,
    (m, spec) => `Object.assign(__exp, __req(${JSON.stringify(resolveId(id, spec))}));`);
  out = out.replace(/^[ \t]*export\s*\{([^}]*)\}\s*;?[ \t]*$/gm, (m, list) => {
    list.split(',').map((s) => s.trim()).filter(Boolean).forEach((s) => {
      const as = s.split(/\s+as\s+/).map((x) => x.trim());
      named.push(as.length === 2 ? { local: as[0], exported: as[1] } : { local: as[0], exported: as[0] });
    });
    return '';
  });
  out = out.replace(/^[ \t]*export\s+(async\s+)?(function|class|const|let|var)\s+([A-Za-z_$][\w$]*)/gm,
    (m, as, kw, name) => { named.push({ local: name, exported: name }); return `${as || ''}${kw} ${name}`; });

  // ---- 转换后还剩行首的 export/import，就是碰到了没支持的写法：报错退出，别硬包 ----
  out.split('\n').forEach((line, i) => {
    if (/^[ \t]*(export|import)\b/.test(line)) {
      bad.push(`${id}:${i + 1} 这行的 export/import 语法没认出来：${line.trim().slice(0, 90)}`);
    }
  });
  if (bad.length) throw new Error(bad.join('\n') + '\n' + SUPPORTED);

  const register = named.length
    ? `\nObject.assign(__exp, { ${named.map((n) => (n.local === n.exported ? n.local : `${JSON.stringify(n.exported)}: ${n.local}`)).join(', ')} });`
    : '';
  return { source: out, deps: collectDeps(out), register };
}

function collectDeps(source) {
  const deps = [];
  const re = /__req\((".*?")\)/g;
  let m;
  while ((m = re.exec(source))) deps.push(JSON.parse(m[1]));
  return [...new Set(deps)];
}

/**
 * DFS 后序，叶子先定义；环靠运行时那层挡（先给空壳，跑完再填）。
 * opts.boot：末尾先 __req 谁（默认 film.js；给 null 就谁也不预热）。
 * opts.tail：换掉末尾那句 window.__WXBUNDLE__ —— --stills 用它把**这一个入口**的默认导出交回页面，
 *   于是每个分镜 JS 各打各的包：一个 JS 缺依赖、语法错、draw() 抛，只砸它自己那一格，
 *   不连累别人 —— 分镜 JS 本来就是独立的，不能因为我们自己的合成层引用了谁，就把它们绑成一命。
 */
function bundle(entryIds, opts) {
  const o = Object.assign({ boot: mainRel, tail: null }, opts || {});
  const seen = new Set();
  const order = [];
  const modules = new Map();
  const dataFiles = new Set();

  function visit(id) {
    if (seen.has(id)) return;
    seen.add(id);
    if (!exists(id)) throw new Error(`预览入口找不到：${id}`);
    const src = fs.readFileSync(abs(id), 'utf8');
    const t = transform(id, src);
    modules.set(id, t);
    t.deps.forEach(visit);
    // 页面 fetch 的数据：file:// 下取不到本地 json，整份内联，页面上再假装有 fetch
    [...src.matchAll(/fetch\(\s*['"]([^'"]+)['"]/g)].forEach((m) => dataFiles.add(m[1]));
    order.push(id);
  }
  entryIds.forEach(visit);

  // 数据文件：file:// 下 fetch 不到本地 json，所以整份内联，页面上再假装有 fetch
  const inlined = {};
  for (const spec of dataFiles) {
    const key = spec.replace(/^\.?\//, '').replace(/[?#].*$/, '');
    if (!exists(key)) throw new Error(`页面 fetch 了 "${spec}"，但 ${filmDir} 下没有 ${key}`);
    inlined[key] = fs.readFileSync(abs(key), 'utf8');
  }

  const body = order.map((id) => {
    const t = modules.get(id);
    return `__define(${JSON.stringify(id)}, function (__exp, __req, __fetch) {\n${t.source}\n${t.register}\n});`;
  }).join('\n\n');

  const run = (o.boot ? `__req(${JSON.stringify(o.boot)});\n` : '')
    + (o.tail || `window.__WXBUNDLE__ = { req: __req, ids: ${JSON.stringify(order)} };\n`);

  return {
    code: `(function () {\nvar __DEF = {}, __EXP = {};\nfunction __define(id, f) { __DEF[id] = f; }\nfunction __req(id) {\n  if (__EXP[id]) return __EXP[id];\n  if (!__DEF[id]) throw new Error('包里没这个模块：' + id + '（可能它只在片子里被 node 端引用）');\n  var exp = __EXP[id] = {};\n  __DEF[id](exp, __req, __fetch);\n  return exp;\n}\nfunction __fetch(spec) {\n  var key = String(spec).replace(/^\\.?\\//, '');\n  var text = window.__WXDATA__[key];\n  if (text === undefined) throw new Error('预览包里没有这份数据：' + key);\n  return Promise.resolve({ ok: true, status: 200, json: function () { return Promise.resolve(JSON.parse(text)); }, text: function () { return Promise.resolve(text); } });\n}\n${body}\n${run}})();`,
    inlined,
    ids: order,
  };
}

/* ---------------- 语法红线 ---------------- */
// film.js 那一层用了 ** 和扩展运算符，Chromium 45（Qt 5.7 自带 WebEngine）会直接语法错。
// 提出来写进页面，免得有人在老内嵌控件里试半天。
function syntaxNotes(ids) {
  const hits = new Set();
  ids.forEach((id) => {
    if (!/\.(m?js)$/.test(id) || !exists(id)) return;
    const src = fs.readFileSync(abs(id), 'utf8');
    if (/[\w)\]]\s*\*\*\s*[\w(]/.test(src)) hits.add('a ** b 幂运算符（Chrome 52+）');
    if (/\.\.\./.test(src)) hits.add('... 扩展/剩余（Chrome 46+）');
    if (/\?\./.test(src)) hits.add('?. 可选链（Chrome 80+）');
    if (/\?\?/.test(src)) hits.add('?? 空值合并（Chrome 80+）');
    if (/(^|[^\w.$])class\s+[A-Z]/.test(src)) hits.add('class（Chrome 49+）');
  });
  return hits.size ? `需要新版浏览器：这份包里有 ${[...hits].join('、')}，Qt 5.7 内嵌的 Chromium 45 打不开，请用 Edge/Chrome（UI 里的「预览」窗口只放静帧，要看动效点那个窗口里的「在浏览器打开这一格」）。` : '';
}

/* ---------------- 页面 ---------------- */
function html(bundleResult, meta) {
  const esc = (s) => s.replace(/<\/(script)/gi, '<\\/$1');
  const css = `
:root { --bg:#0d1017; --fg:#dfe6ef; --dim:#8b98ab; --line:#242c38; --bad:#ff6b7d; --ok:#7bd88f; }
* { box-sizing:border-box; }
body { margin:0; background:var(--bg); color:var(--fg); font:400 14px/1.5 system-ui,"Microsoft YaHei",sans-serif; }
header { display:flex; gap:8px; align-items:center; padding:10px 14px; border-bottom:1px solid var(--line); flex-wrap:wrap; }
button, input { font:inherit; color:inherit; background:#1a212b; border:1px solid #2f3948; border-radius:6px; padding:5px 11px; }
button { cursor:pointer; } button:hover { background:#222c3a; } button.on { background:#2f6fb3; border-color:#4d8fd4; color:#fff; }
input[type=range] { padding:0; flex:1 1 260px; min-width:180px; accent-color:#4d8fd4; }
input.num { width:78px; }
main { display:flex; gap:14px; padding:14px; align-items:flex-start; flex-wrap:wrap; }
#stageWrap { flex:1 1 560px; min-width:320px; }
#stage { border:1px solid var(--line); border-radius:8px; background:#05070c; display:flex; justify-content:center; align-items:center; min-height:520px; overflow:hidden; }
canvas { display:block; }
#miniWrap { display:none; }
#miniGrid { display:grid; grid-template-columns:repeat(auto-fill,minmax(170px,1fr)); gap:10px; padding:10px; }
#side { flex:0 0 300px; display:flex; flex-direction:column; gap:8px; }
#side h2 { font-size:13px; color:var(--dim); margin:6px 0 0; font-weight:400; letter-spacing:.04em; }
figure { margin:0 0 12px; } figure img { width:100%; border:1px solid var(--line); border-radius:6px; background:#000; }
figcaption { color:var(--dim); font-size:12px; word-break:break-all; }
#report { padding:10px 14px; border-top:1px solid var(--line); }
.bad { color:var(--bad); } .ok { color:var(--ok); } .dim { color:var(--dim); }
.hint { color:var(--dim); font-size:12px; }
kbd { background:#1a212b; border:1px solid #2f3948; border-radius:4px; padding:0 5px; font:inherit; }
`;

  const page = fs.readFileSync(path.join(toolDir, 'preview-page.js'), 'utf8');
  const shim = fs.readFileSync(path.join(toolDir, 'wx-shim.js'), 'utf8');

  return `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>${meta.title} · 预览</title>
<style>${css}</style>
<script>
// 最前面就装错误收集：bundle 里的语法错也要看得见，不能留一张黑画布让人猜。
window.__PREVIEW_ERRORS__ = [];
window.addEventListener('error', function (e) { window.__PREVIEW_ERRORS__.push(String((e && e.message) || e)); });
window.addEventListener('unhandledrejection', function (e) { window.__PREVIEW_ERRORS__.push('Promise：' + String((e && e.reason && e.reason.message) || e.reason)); });
</script>
</head>
<body>
<header>
  <span id="tabs"><button data-view="film">成片</button><button data-view="mini">小程序</button></span>
  <span id="filmCtl" style="display:flex;gap:8px;align-items:center;flex:1">
    <button id="play">▶ 放</button>
    <button id="stepB">◀一帧</button>
    <input id="seek" type="range" min="0" max="100" value="0">
    <button id="stepF">一帧▶</button>
    <span id="time">0.00 / 0.00 s</span>
  </span>
  <span id="meta" class="hint"></span>
</header>
<main>
  <div id="stageWrap">
    <div id="stage">
      <canvas id="c"></canvas>
      <div id="miniWrap">
        <div id="miniTabs" style="padding:8px 0"></div>
        <canvas id="mini"></canvas>
        <div id="miniGrid" style="display:none"></div>
      </div>
    </div>
    <p class="hint">「成片」走 film.js 的 renderAt，和导出的 mp4 同一帧；「小程序」直接调模块的 draw(ctx, stage)，等于手机上那一屏。空格 放/停，← → 走一帧，M 切视图。</p>
    <div id="where" class="hint"></div>
  </div>
  <div id="side">
    <h2>小程序画布</h2>
    <div style="display:flex;gap:6px;align-items:center">
      宽 <input class="num" id="miniW" type="number" value="750">
      高 <input class="num" id="miniH" type="number" value="1334">
    </div>
    <div id="preset" style="display:flex;gap:6px;flex-wrap:wrap">
      <button data-p="750x1334">750×1334</button>
      <button data-p="375x667">375×667</button>
      <button data-p="${meta.size[0]}x${meta.size[1]}">成片 ${meta.size.join('×')}</button>
    </div>
    <h2>体检</h2>
    <button id="btnSample">逐格样帧</button>
    <button id="btnStrip">接片条 + 回环数字</button>
    <button id="btnSave">存当前视图 PNG</button>
    <div id="out"></div>
  </div>
</main>
<div id="report"></div>

<script>${esc(shim)}</script>
<script>window.__WISHIM_PRELUDE__ = true;
window.__WXDATA__ = ${JSON.stringify(bundleResult.inlined).replace(/</g, '\\u003c')};
(function () {
  var real = typeof fetch === 'function' ? fetch.bind(window) : null;
  window.fetch = function (input, init) {
    var spec = typeof input === 'string' ? input : (input && input.url) || '';
    var key = String(spec).replace(/^\\.?\\//, '').replace(/[?#].*$/, '');
    if (window.__WXDATA__[key] !== undefined) return Promise.resolve({ ok: true, status: 200,
      json: function () { return Promise.resolve(JSON.parse(window.__WXDATA__[key])); },
      text: function () { return Promise.resolve(window.__WXDATA__[key]); } });
    if (real) return real(input, init);
    return Promise.reject(new Error('预览包里没有这份数据：' + key));
  };
})();
</script>
<script>
/* 小程序模块经常在文件顶层就读 getSystemInfoSync 定坐标，而顶层求值早于页面脚本，
   所以“这块屏多大”必须在加载包**之前**就好。默认 750×1334，深链 ?w=&h= 可改。 */
window.__WXSCREEN__ = (function () {
  var q = new URLSearchParams(location.search);
  var w = Number(q.get('w')), h = Number(q.get('h'));
  return { w: w > 0 ? w : 750, h: h > 0 ? h : 1334 };
})();
</script>
${(Array.isArray(bundleResult.code) ? bundleResult.code : [bundleResult.code])
  .map((c) => '<script>' + esc(c) + '</script>').join('\n')}
<script>
window.__PREVIEW__ = {
  plan: (function () { try { return JSON.parse(window.__WXDATA__[${JSON.stringify(meta.dataKey)}]); } catch (e) { return {}; } })(),
  shots: ${JSON.stringify(meta.shotIds)}.map(function (id) {
    var m = window.__WXBUNDLE__.req(id), d = (m && m.default) || {};
    var short = id.split('/').pop().split('.').shift();
    return { path: id, id: d.id || short, name: d.name || '', mod: d };
  }),
  space: (function () { try { var s = window.__WXBUNDLE__.req(${JSON.stringify(meta.shimId)}).SPACE; return s ? 'rgb(' + s.join(',') + ')' : ''; } catch (e) { return ''; } })(),
  syntax: ${JSON.stringify(meta.syntax)},
  ready: false
};
</script>
<script>${esc(page)}</script>
</body>
</html>
`;
}

/* ---------------- 跑 ---------------- */
const entries = [mainRel, ...shotIds].filter(Boolean);
if (!entries.length) {
  console.error(`既没有 ${mainRel || 'film.js'} 也没在 ${boardRel}/ 里找到带默认导出 draw() 的模块。`);
  console.error('手动指定：node tools/preview/build-preview.mjs <片子目录> --main film.js --shots storyboard/earth.js,...');
  process.exit(1);
}

let result = null;
let filmError = '';
try {
  result = bundle(entries);
} catch (e) {
  filmError = e.message;
  console.error('成片那一层打包不过：\n' + filmError);
  // --stills 不看这一层：每格 JS 自己打自己的包，合成器坏了不该把六格全清空
  if (!has('stills')) process.exit(1);
}

const outFile = path.join(filmDir, outRel);

if (result) {
  const planFile = exists('shotplan.json') ? 'shotplan.json' : Object.keys(result.inlined)[0];
  const planJson = planFile && exists(planFile) ? JSON.parse(fs.readFileSync(abs(planFile), 'utf8')) : {};
  const meta = {
    title: planJson.name || path.basename(filmDir),
    size: planJson.size || [1080, 1440],
    dataKey: planFile,
    shotIds,
    shimId: exists('shim.mjs') ? 'shim.mjs' : '__none__',
    syntax: syntaxNotes(result.ids)
  };
  const doc = html(result, meta);
  fs.writeFileSync(outFile, doc);

  console.log(`== preview.html ==`);
  console.log(`  ${rel(outRel)}  ${(fs.statSync(outFile).size / 1024).toFixed(0)} KB  模块 ${result.ids.length} 个  内联数据 ${Object.keys(result.inlined).length} 份`);
  console.log(`  模块：${result.ids.join('、')}`);
  console.log(`  分镜：${shotIds.length ? shotIds.join('、') : '（没找到带 draw() 的模块）'}`);
  if (meta.syntax) console.log(`  注意：${meta.syntax}`);
  console.log(`  打开：${pathToFileURL(outFile).href}`);
} else {
  console.log('== preview.html ==（没打：成片那一层挂了）');
}

/* ---------------- 无头开一次页面（--verify 和 --stills 共用） ---------------- */
const FILM_READY = () => window.__PREVIEW__ && window.__PREVIEW__.ready === true;
// 只有分镜 JS、还没写 film.js 的片子（小程序验证阶段就是这态）：包在位就能画单格，
// 不必修等 window.renderAt —— 拿单格图本来就不经合成器。
const BUNDLE_READY = () => !!window.__PREVIEW__;

async function openPreviewPage(wait, query, target) {
  const { createRequire } = await import('node:module');
  const targetFile = target || outFile;
  const req = createRequire(targetFile);
  const cand = [
    path.join(filmDir, 'node_modules', 'playwright'),
    path.join(repoRoot, 'films', 'fat-fish', 'node_modules', 'playwright'),
    path.join(repoRoot, 'tools', 'trailer', 'node_modules', 'playwright')
  ];
  const found = cand.find((p) => fs.existsSync(p));
  if (!found) throw new Error('需要 playwright，仓库里三份候选都没找到');
  const { chromium } = req(found);
  const browser = await chromium.launch({ channel: 'msedge', headless: true, args: ['--disable-gpu', '--hide-scrollbars'] });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 1 });
  const logs = [];
  page.on('pageerror', (e) => logs.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') logs.push(m.text()); });
  await page.goto(pathToFileURL(targetFile).href + (query ? '?' + query : ''), { waitUntil: 'load' });
  let ready = true;
  try { await page.waitForFunction(wait || FILM_READY, null, { timeout: 20000 }); }
  catch (e) { ready = false; }
  return { browser, page, logs, ready };
}

/* ---------------- --stills：每个分镜 JS 一张图，给 Qt 对话框看 ---------------- */
// 规矩：storyboard/ 下每个 .js 都必须留下一句话的交代 —— 画出来了写 png + 像素统计，
// 画不出来就写 .txt 说明原因。UI 那边宁可看到原因，也不能对着一张没刷新的旧图或者空白点头像。
//
// 设计：每个 JS 各打各的包。一个 JS 缺依赖、语法错、draw() 抛，只砸它自己那一格，
// 不连累别的 —— 分镜 JS 本来就是独立的，不能因为我们自己的合成层引用了谁，就把它们绑成一命。
if (has('stills')) {
  const dir = path.join(filmDir, 'out', 'preview');
  const note = (name, text) => fs.writeFileSync(path.join(dir, `${name}.txt`), `${text}\n`);
  const shortOf = (id) => id.split('/').pop().replace(/\.m?js$/i, '');
  const p = opt('size', '750x1334').split('x');
  const stillSize = { w: Math.max(64, Number(p[0]) || 750), h: Math.max(64, Number(p[1]) || 1334) };
  fs.mkdirSync(dir, { recursive: true });
  const boardDir = path.join(filmDir, boardRel);
  // --only=<name>：Qt 那边按需出图，只传当前点的这一格名字（带 .js 或不带都认），
  // 不把所有 JS 都过一遍 —— 用户点一下出一张，比"每次都全量渲"轻得多。
  // 不带 --only 就是老行为：storyboard/ 下所有 .js 都渲。
  const onlyName = opt('only', '');
  const allBoardFiles = fs.existsSync(boardDir)
    ? fs.readdirSync(boardDir).filter((f) => /\.m?js$/i.test(f)).map((f) => `${boardRel}/${f}`)
    : [];
  const boardFiles = onlyName
    ? allBoardFiles.filter((id) => {
        const s = shortOf(id);
        return s === onlyName || s + '.js' === onlyName || s + '.mjs' === onlyName || id === onlyName;
      })
    : allBoardFiles;
  console.log(`\n== --stills ==`);
  console.log(onlyName
    ? `  只渲一格：${boardFiles[0] || onlyName}（${boardFiles.length ? '找到' : '没匹配到'}）`
    : `  每个 JS 单独打一个包（${boardFiles.length} 个），互不牵连`);
  if (!boardFiles.length) {
    note('_status', `--only 没匹配到：${onlyName}（storyboard/ 下共 ${allBoardFiles.length} 个 js）`);
    console.error(`  --only=${onlyName} 没匹配到任何 storyboard JS`);
    process.exit(1);
  }

  // ① 逐份打包：一份坏了只记在它自己名下，不拖别人下水
  const boards = [];
  const inlined = {};
  const packErrs = [];
  boardFiles.forEach((id) => {
    const name = shortOf(id);
    const tail = `try { var __e = __req(${JSON.stringify(id)}); window.__BOARDS__.push({ name: ${JSON.stringify(name)}, id: ${JSON.stringify(id)}, mod: (__e && __e.default) || null }); } catch (err) { window.__BOARDS__.push({ name: ${JSON.stringify(name)}, id: ${JSON.stringify(id)}, error: String((err && err.message) || err) }); }\n`;
    try {
      const b = bundle([id], { boot: null, tail });
      Object.assign(inlined, b.inlined);
      boards.push({ name, id, code: b.code });
    } catch (e) {
      const why = String(e.message || e).split('\n')[0];
      boards.push({ name, id, code: '', error: '它自己打包不过：' + why });
      packErrs.push(`${name}：${why}`);
    }
  });

  // ② 一个页面装下所有独立包，无头开一次
  const stillsFile = path.join(dir, '_stills.html');
  const stillsDoc = html(
    { code: ['window.__BOARDS__ = [];'].concat(boards.map((b) => b.code)), inlined },
    { title: path.basename(filmDir) + ' · 静帧', size: [stillSize.w, stillSize.h],
      dataKey: '__none__', shotIds: [], shimId: '__none__',
      syntax: '这一页只是 --stills 的渲染现场：每个 JS 一个独立的包，一格坏了不连累别格。要看能点的预览请开 ../preview.html' }
  );
  fs.writeFileSync(stillsFile, stillsDoc);

  let session;
  try {
    // 把单格尺寸一并写进深链：页面顶层声明的 __WXSCREEN__ 必须跟这里渲的尺寸同一个数，
    // 否则顶层读屏幕尺寸的模块会拿到另一套坐标，渲出来歪掉却看不出来为什么。
    session = await openPreviewPage(BUNDLE_READY, `w=${stillSize.w}&h=${stillSize.h}`, stillsFile);
  }
  catch (e) {
    note('_status', `开不了页面：${e.message}`);
    console.error(`  ${e.message}`);
    console.error('  成片那一层打包失败不影响这一层；但连无头页都开不了就真没图了。');
    process.exit(1);
  }
  const { browser, page, logs, ready } = session;
  await new Promise((r) => setTimeout(r, 300));   // 模块里现加载的精灵（data: 图）落一落再抽帧
  const drawn = await page.evaluate(async (sz) => {
    const list = (window.__BOARDS__ || []).slice();
    const space = '#0b0f16';
    const out = [];
    for (const b of list) {
      const rec = { path: b.id, id: b.name, png: null, error: b.error || null, mean: 0, sd: 0, inkPct: 0 };
      if (!rec.error) {
        try {
          if (!b.mod || typeof b.mod.draw !== 'function') {
            // 依赖模块（没有默认导出的 draw）：不是错误，是它本来就不成一屏
            rec.noDraw = true;
            rec.error = '没有默认导出的 draw(ctx, stage)：它多半是被别的格 import 的依赖，自己不成一屏';
          } else {
            const cv = document.createElement('canvas');
            cv.width = sz.w;
            cv.height = sz.h;
            const x = cv.getContext('2d');
            if (window.__WISHIM__) window.__WISHIM__.setTarget(cv);
            const frame = () => {
              x.fillStyle = space;
              x.fillRect(0, 0, cv.width, cv.height);
              b.mod.draw(x, { x: 0, y: 0, w: sz.w, h: sz.h, dpr: 1 });
            };
            frame();
            await new Promise((r) => setTimeout(r, 40));
            frame();
            if (window.__WISHIM__) window.__WISHIM__.setTarget(null);
            const d = x.getImageData(0, 0, cv.width, cv.height).data;
            let sum = 0, sum2 = 0, n = 0, ink = 0;
            for (let i = 0; i < d.length; i += 4) {
              const v = (d[i] + d[i + 1] + d[i + 2]) / 3;
              sum += v; sum2 += v * v; n += 1;
              if (v > 24) ink += 1;
            }
            const mean = sum / n;
            rec.mean = +mean.toFixed(1);
            rec.sd = +Math.sqrt(sum2 / n - mean * mean).toFixed(1);
            rec.inkPct = +((100 * ink) / n).toFixed(1);
            rec.png = cv.toDataURL('image/png');
          }
        } catch (e) { rec.error = String((e && e.message) || e); }
      }
      out.push(rec);
    }
    return out;
  }, stillSize);
  const byPath = new Map(drawn.map((d) => [d.path, d]));
  const errs = logs.slice();
  packErrs.forEach((e) => errs.push(e));
  let wrote = 0;
  boardFiles.forEach((id) => {
    const name = shortOf(id);
    const d = byPath.get(id);
    if (!d) {
      note(name, '连脚本都没跑起来（多半是打包器生成的代码有语法错）');
      errs.push(`${name}：脚本没跑起来`);
      return;
    }
    if (d.error) {
      if (d.noDraw) {
        // 依赖模块：不是错误，是它本来就不成一屏，写原因但不算报错
        note(name, d.error);
      } else {
        note(name, `draw() 抛了：${d.error}`);
        errs.push(`${name}：${d.error}`);
      }
      return;
    }
    fs.writeFileSync(path.join(dir, `${name}.png`), Buffer.from(String(d.png).split(',')[1] || '', 'base64'));
    note(name, `${stillSize.w}×${stillSize.h}　均值 ${d.mean}　标准差 ${d.sd}　有图像素 ${d.inkPct}%`);
    wrote += 1;
  });
  await browser.close();
  note('_status', `${wrote} 格出了图，${boardFiles.length - wrote} 个文件没图；报错 ${errs.length} 条${errs.length ? '：' + errs.join(' / ') : ''}`);
  console.log(`  ${rel('out/preview')}/ 写了 ${wrote} 张图（共 ${boardFiles.length} 个 js）· 尺寸 ${stillSize.w}×${stillSize.h}`);
  if (errs.length) console.log(`  报错：\n    ${errs.join('\n    ')}`);
  const pass = !errs.length && wrote > 0;
  console.log(`  ${pass ? '过' : '不过'}（判据：零报错 + 至少画出一格；不过也会把能画的图留下）`);
  process.exit(pass ? 0 : 1);
}

/* ---------------- --verify ---------------- */
if (has('verify')) {
  if (!result) {
    console.error('--verify 需要 preview.html，但成片那一层没打出来：' + filmError);
    process.exit(1);
  }
  let session;
  try { session = await openPreviewPage(FILM_READY); }
  catch (e) { console.error(`--verify 打不开页面：${e.message}`); process.exit(1); }
  const { browser, page, logs, ready } = session;
  const probe = await page.evaluate(() => {
    const stat = (cv) => {
      const d = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height).data;
      let s = 0, s2 = 0, n = 0, ink = 0;
      for (let i = 0; i < d.length; i += 4) {
        const v = (d[i] + d[i + 1] + d[i + 2]) / 3;
        s += v; s2 += v * v; n += 1; if (v > 24) ink += 1;
      }
      const mean = s / n;
      return { mean: +mean.toFixed(1), sd: +Math.sqrt(s2 / n - mean * mean).toFixed(1), inkPct: +(100 * ink / n).toFixed(1) };
    };
    const out = { errors: window.__PREVIEW_ERRORS__ || [], missing: (window.__WISHIM__ || {}).missing || [], film: null, mini: [], fatal: null };
    try {
      if (window.renderAt && window.DURATION) { window.renderAt(window.DURATION * 0.08); out.film = stat(document.getElementById('c')); }
      ((window.__PREVIEW__ || {}).shots || []).forEach(function (s) {
        const cv = document.createElement('canvas');
        cv.width = 750; cv.height = 1334;
        const x = cv.getContext('2d');
        x.fillStyle = '#05070c'; x.fillRect(0, 0, cv.width, cv.height);
        if (window.__WISHIM__) window.__WISHIM__.setTarget(cv);   // 老接口的模块要找得到画布才算数
        try { s.mod.draw(x, { x: 0, y: 0, w: cv.width, h: cv.height }); out.mini.push({ id: s.id, ...stat(cv) }); }
        catch (e) { out.mini.push({ id: s.id, error: String(e && e.message || e) }); }
        if (window.__WISHIM__) window.__WISHIM__.setTarget(null);
      });
    } catch (e) { out.fatal = String(e && e.message || e); }
    return out;
  });
  fs.mkdirSync(path.join(filmDir, 'out'), { recursive: true });
  await page.screenshot({ path: path.join(filmDir, 'out', 'preview-check.png') });
  console.log(`\n== --verify ==`);
  console.log(`  页面就绪：${ready ? '是' : '否（初始化没走完）'}`);
  const errs = [...logs, ...probe.errors];
  if (probe.fatal) errs.push('探测脚本自己挂了：' + probe.fatal);
  console.log(`  报错：${errs.length ? errs.join('\n    ') : '没有'}`);
  console.log(`  没垫到的 wx API：${probe.missing.length ? probe.missing.join('、') : '没有'}`);
  if (probe.film) console.log(`  成片画布（renderAt 一帧）：均值 ${probe.film.mean} 标准差 ${probe.film.sd} 有图像素 ${probe.film.inkPct}%`);
  probe.mini.forEach((m) => console.log(`  小程序 ${m.id}：${m.error ? '✕ ' + m.error : `均值 ${m.mean} 标准差 ${m.sd} 有图像素 ${m.inkPct}%`}`));
  const blank = probe.mini.filter((m) => m.error || m.sd < 1 || m.inkPct < 0.5);
  await browser.close();
  const pass = ready && !errs.length && !blank.length;
  console.log(`  ${pass ? '过' : '不过'}（判据：就绪 + 零报错 + 每格都画出东西）`);
  console.log(`  写了 ${rel('out/preview-check.png')}`);
  process.exit(pass ? 0 : 1);
}
