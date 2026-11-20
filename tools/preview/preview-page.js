/*
 * 预览页的界面脚本。build-preview.mjs 把它排在打包好的模块之后，所以这里假定：
 *   成片视图：window.renderAt / paintShot / buildStrip / loopCheck / SHOTS / JOINS / DURATION / FPS
 *             —— 这些全来自片子自己的 film.js，预览不另写一套合成，看到的就是出片那一帧。
 *   小程序视图：__PREVIEW__.shots[i].mod.draw(ctx, stage) —— 直接调用户模块的默认导出，
 *             等于手机上那一屏的画法则，不经 film.js。
 * 两个视图共用同一份垫片，所以"预览里对、小程序里也对"才有意义。
 */
(function () {
  'use strict';

  var meta = window.__PREVIEW__ || {};
  var qs = new URLSearchParams(location.search);
  var $ = function (id) { return document.getElementById(id); };
  var stage = { view: qs.get('view') === 'mini' ? 'mini' : 'film', t: Number(qs.get('t') || 0), playing: false, shot: qs.get('shot') || null };
  var miniSize = { w: Number(qs.get('w') || 750), h: Number(qs.get('h') || 1334) };
  var raf = 0;

  /* ---------- 报告区：错误和缺口必须显眼 ---------- */
  function report() {
    var box = $('report');
    var lines = [];
    var errs = (window.__PREVIEW_ERRORS__ || []).concat(window.__PREVIEW_LATE__ || []);
    if (errs.length) lines.push('<div class="bad">页面报错 ' + errs.length + ' 条</div>');
    errs.forEach(function (e) { lines.push('<div class="bad">' + esc(e) + '</div>'); });
    var shim = window.__WISHIM__;
    if (shim) {
      if (shim.missing.length) lines.push('<div class="bad">小程序 API 没垫到：' + shim.missing.map(esc).join('、') + '</div>');
      var hits = Object.keys(shim.calls).map(function (k) { return k + ' ×' + shim.calls[k]; });
      if (hits.length) lines.push('<div class="dim">垫片调用：' + hits.map(esc).join('　') + '</div>');
      shim.log.slice(-6).forEach(function (l) { lines.push('<div class="dim">' + esc(l) + '</div>'); });
    }
    if (meta.syntax) lines.push('<div class="bad">' + esc(meta.syntax) + '</div>');
    if (!lines.length) lines.push('<div class="ok">没有报错，小程序 API 全部垫上了。</div>');
    box.innerHTML = lines.join('');
  }
  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }

  /* ---------- 等 film.js 把入口挂上 ---------- */
  function whenReady(fn) {
    if (window.renderAt && window.assetsReady) return fn();
    var waited = 0;
    var tick = function () {
      waited += 60;
      if (window.renderAt && window.assetsReady) return fn();
      if (waited > 20000) {
        $('time').textContent = '片子初始化超时：' + ((window.__PREVIEW_ERRORS__ || []).join(' / ') || '没等到 window.renderAt');
        report();
        return;
      }
      setTimeout(tick, 60);
    };
    tick();
  }

  /* ---------- 成片视图 ---------- */
  var DUR = 1;
  var FPS = 24;

  function seek(t) {
    stage.t = Math.max(0, Math.min(DUR, t));
    $('seek').value = String(Math.round(stage.t * 100));
    $('time').textContent = stage.t.toFixed(2) + ' / ' + DUR.toFixed(2) + ' s　第 ' + Math.floor(stage.t * FPS) + ' 帧';
    window.renderAt(stage.t);
    // 落在哪一格 / 哪个接点，直接写在标题上，省得自己算
    var label = '';
    var pl = meta.plan || {};
    (pl.shots || []).forEach(function (s) { if (stage.t >= s.t0 && stage.t < s.t1) label = '格 ' + s.id + '（停）'; });
    (pl.joins || []).forEach(function (j) {
      if (stage.t >= j.t0 && stage.t < j.t1) {
        var u = (stage.t - j.t0) / (j.t1 - j.t0);
        label = j.a + ' → ' + j.b + '　u=' + u.toFixed(3);
      }
    });
    $('where').textContent = label || '空档';
  }

  function loop() {
    if (!stage.playing) return;
    seek(stage.t + 1 / FPS);
    if (stage.t >= DUR) seek(0);
    raf = requestAnimationFrame(loop);
  }

  function play(on) {
    stage.playing = on === undefined ? !stage.playing : on;
    $('play').textContent = stage.playing ? '⏸ 停' : '▶ 放';
    cancelAnimationFrame(raf);
    if (stage.playing && stage.view === 'film') raf = requestAnimationFrame(loop);
  }

  /* ---------- 小程序视图：直接调模块自己的 draw(ctx, stage) ---------- */
  function shots() { return meta.shots || []; }
  function keyOf(s) { return String(s.path).split('/').pop().split('.').shift(); }
  // 深链 ?shot= 既认模块报的 id，也认文件名（UI 那边只有文件名，d.id 叫什么不一定）
  function isShot(s, key) { return !!key && (s.id === key || keyOf(s) === key); }

  function paintMini(canvas, item, w, h) {
    canvas.width = w;
    canvas.height = h;
    var ctx = canvas.getContext('2d');
    ctx.fillStyle = meta.space || '#0b0f16';
    ctx.fillRect(0, 0, w, h);
    var label = '';
    try {
      if (!item || typeof item.mod.draw !== 'function') throw new Error('这个模块没有默认导出的 draw(ctx, stage)');
      // 老接口的模块拿不到自己的 canvasId（那是 WXML 上写的），让垫片把这块当它的画布
      if (window.__WISHIM__) window.__WISHIM__.setTarget(canvas);
      item.mod.draw(ctx, { x: 0, y: 0, w: w, h: h, dpr: window.devicePixelRatio || 1 });
    } catch (e) {
      label = String(e.message || e);
      (window.__PREVIEW_LATE__ = window.__PREVIEW_LATE__ || []).push('小程序视图 ' + item.id + '：' + label);
    }
    if (window.__WISHIM__) window.__WISHIM__.setTarget(null);
    ctx.font = '400 22px system-ui, sans-serif';
    ctx.fillStyle = 'rgba(232,238,246,0.85)';
    ctx.fillText(item.id + '  ' + (item.name || '') + (w >= 360 ? '　' + w + '×' + h : '') + (label ? '　✕ ' + label : ''), 12, h - 14);
    return label;
  }

  function renderMini() {
    var list = shots();
    var tabs = $('miniTabs');
    var one = $('mini');
    var grid = $('miniGrid');
    if (!tabs.built) {
      tabs.innerHTML = list.map(function (s, i) {
        return '<button data-mini="' + i + '">' + esc(s.id) + (s.name ? ' · ' + esc(s.name) : '') + '</button>';
      }).join('') + '<button data-mini="-1">全部一格一屏</button>';
      tabs.built = true;
      tabs.addEventListener('click', function (e) {
        var b = e.target.closest('button[data-mini]');
        if (!b) return;
        stage.shot = b.dataset.mini === '-1' ? 'grid' : list[Number(b.dataset.mini)].id;
        renderMini();
      });
    }
    Array.prototype.forEach.call(tabs.children, function (b) {
      var key = b.dataset.mini;
      var on = key === '-1' ? stage.shot === 'grid' : list[Number(key)] && isShot(list[Number(key)], stage.shot);
      b.className = on ? 'on' : '';
    });
    $('miniW').value = miniSize.w;
    $('miniH').value = miniSize.h;

    if (stage.shot === 'grid') {
      one.style.display = 'none';
      grid.style.display = 'grid';
      grid.innerHTML = '';
      // 按格子大小画一遍就够：再按手机尺寸画一次会把四个模块的精灵重做两遍，白等几秒
      var cw = Math.max(160, Math.min(420, Math.round(miniSize.w * Math.min(1, 420 / miniSize.w))));
      var ch = Math.round(cw * miniSize.h / miniSize.w);
      list.forEach(function (s) {
        var cell = document.createElement('div');
        var c = document.createElement('canvas');
        c.width = cw;
        c.height = ch;
        c.style.width = '100%';
        c.style.height = 'auto';
        c.title = s.id + ' @ ' + miniSize.w + '×' + miniSize.h;
        paintMini(c, s, cw, ch);
        // 先画完再拼说明：cell.innerHTML += 会把已经画好的 canvas 重建掉，图就空了
        var cap = document.createElement('div');
        cap.className = 'dim';
        cap.textContent = '按 ' + miniSize.w + '×' + miniSize.h + ' 算，缩到 ' + cw + '×' + ch + ' 显示';
        cell.appendChild(c);
        cell.appendChild(cap);
        grid.appendChild(cell);
      });
      return;
    }
    grid.style.display = 'none';
    one.style.display = 'block';
    var pick = list.filter(function (s) { return isShot(s, stage.shot); })[0] || list[0];
    if (pick) { stage.shot = pick.id; paintMini(one, pick, miniSize.w, miniSize.h); }
    fit(one);
    report();  // 每格都可能新建精灵，画完再报一次数，不然面板上是上一个视图的计数
  }

  function fit(el) {
    var box = $('stage');
    var availH = Math.max(240, box.clientHeight - 8);
    var availW = Math.max(240, box.clientWidth - 8);
    var k = Math.min(availW / el.width, availH / el.height, 1);
    el.style.width = Math.round(el.width * k) + 'px';
    el.style.height = Math.round(el.height * k) + 'px';
  }

  function showView() {
    var film = stage.view === 'film';
    $('c').style.display = film ? 'block' : 'none';
    $('miniWrap').style.display = film ? 'none' : 'block';
    $('filmCtl').style.display = film ? 'flex' : 'none';
    Array.prototype.forEach.call(document.querySelectorAll('#tabs button'), function (b) {
      b.className = b.dataset.view === stage.view ? 'on' : '';
    });
    if (film) { seek(stage.t); fit($('c')); } else { renderMini(); }
    report();
  }

  /* ---------- 三道体检按钮：不用回命令行就能看关 ---- */
  function doSample() {
    var out = $('out');
    out.innerHTML = '<div class="dim">在跑，稍等…</div>';
    setTimeout(function () {
      var html = shots().map(function (s) {
        window.paintShot(s.id);
        var url = $('c').toDataURL('image/png');
        return '<figure><img src="' + url + '" alt=""><figcaption>样帧 ' + esc(s.id) + '　' + (s.name ? esc(s.name) : '') + '</figcaption></figure>';
      }).join('');
      out.innerHTML = html || '<div class="bad">没有模块可看</div>';
      report();
    }, 0);
  }

  function doStrips() {
    var out = $('out');
    out.innerHTML = '<div class="dim">在算接片条，推镜要重画投影，几秒…</div>';
    setTimeout(function () {
      var joins = window.JOINS || [];
      var html = '';
      for (var i = 0; i < joins.length; i += 1) {
        var url = window.buildStrip(i, [0.2, 0.4, 0.6, 0.8, 0.93]);
        var back = window.loopCheck(i) || [];
        var pass = back.every(function (r) { return r.ssim >= 0.97 || r.meanAbs <= 2; });
        html += '<figure><img src="' + url + '" alt="">'
          + '<figcaption>接片条 ' + esc(joins[i]) + '　回环'
          + back.map(function (r) { return esc(r.id + '@u=' + r.u + ' SSIM ' + r.ssim.toFixed(4) + ' 均差 ' + r.meanAbs.toFixed(2)); }).join('　')
          + '<br>' + (pass ? '<span class="ok">过（DIRECTOR §9：SSIM ≥ 0.97 或平均差 ≤ 2）</span>'
            : '<span class="bad">不过：插值在往别处漏</span>') + '</figcaption></figure>';
      }
      out.innerHTML = html || '<div class="bad">这个片子没有 joins</div>';
      report();
    }, 0);
  }

  function saveShot() {
    var name = (stage.view === 'film' ? 'film-t' + stage.t.toFixed(2) : 'mini-' + stage.shot + '-' + miniSize.w + 'x' + miniSize.h) + '.png';
    var src = stage.view === 'film' ? $('c') : (stage.shot === 'grid' ? null : $('mini'));
    if (!src) { $('out').innerHTML = '<div class="bad">整屏模式下没有单张可存，切一格再存</div>'; return; }
    var a = document.createElement('a');
    a.href = src.toDataURL('image/png');
    a.download = name;
    a.click();
  }

  /* ---------- 接上事件 ---------- */
  whenReady(function () {
    var p = meta.plan || {};
    DUR = window.DURATION || p.duration || 1;
    FPS = window.FPS || p.fps || 24;
    $('seek').max = String(Math.round(DUR * 100));
    $('seek').step = '1';
    $('meta').textContent = (p.name || '没读到 shotplan') + '　' + (p.size ? p.size.join('×') : '?')
      + '　' + FPS + ' fps　' + DUR.toFixed(2) + ' s';
    if (!stage.shot) stage.shot = shots().length ? shots()[0].id : null;

    $('tabs').addEventListener('click', function (e) {
      var b = e.target.closest('button[data-view]');
      if (!b) return;
      play(false);
      stage.view = b.dataset.view;
      showView();
    });
    $('play').addEventListener('click', function () { play(); });
    $('seek').addEventListener('input', function () { play(false); seek(Number(this.value) / 100); });
    $('stepB').addEventListener('click', function () { play(false); seek(stage.t - 1 / FPS); });
    $('stepF').addEventListener('click', function () { play(false); seek(stage.t + 1 / FPS); });
    $('miniW').addEventListener('change', function () { miniSize.w = Math.max(60, Number(this.value) || 750); if (stage.view === 'mini') renderMini(); });
    $('miniH').addEventListener('change', function () { miniSize.h = Math.max(60, Number(this.value) || 1334); if (stage.view === 'mini') renderMini(); });
    $('preset').addEventListener('click', function (e) {
      var b = e.target.closest('button[data-p]');
      if (!b) return;
      var p = b.dataset.p.split('x');
      miniSize.w = Number(p[0]);
      miniSize.h = Number(p[1]);
      if (stage.view === 'mini') renderMini();
    });
    $('btnSample').addEventListener('click', doSample);
    $('btnStrip').addEventListener('click', doStrips);
    $('btnSave').addEventListener('click', saveShot);

    document.addEventListener('keydown', function (e) {
      if (e.target.tagName === 'INPUT') return;
      if (e.code === 'Space') { e.preventDefault(); play(); }
      else if (e.key === 'ArrowLeft') { play(false); seek(stage.t - 1 / FPS); }
      else if (e.key === 'ArrowRight') { play(false); seek(stage.t + 1 / FPS); }
      else if (e.key === 'm') { play(false); stage.view = stage.view === 'film' ? 'mini' : 'film'; showView(); }
    });
    window.addEventListener('resize', function () { fit(stage.view === 'film' ? $('c') : $('mini')); });

    window.__PREVIEW__.ready = true;
    showView();
  });

  window.addEventListener('error', function () { report(); });
})();
