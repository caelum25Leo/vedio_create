/*
 * 微信小程序 canvas API → 浏览器 Canvas 2D 的垫片。
 * 普通 <script> 直接加载，不用 module，所以 file:// 双击就能开。
 *
 * 覆盖两代接口：
 *   新：wx.createCanvas() / wx.createOffscreenCanvas() → 一个真 <canvas>，
 *       拿到的 canvas.getContext('2d') 就是浏览器那套，方法名一模一样，不用改。
 *   老：wx.createCanvasContext(canvasId) → 它的方法名和属性名不同（setFillStyle 而不是 fillStyle），
 *       而且必须调 draw() 才提交。这里把 setter 翻译成浏览器属性，draw() 翻译成就地提交。
 *
 * 垫不到的东西不静默吞掉：记进 __WISHIM__.missing，预览页右下角会红字列出来。
 * 宁可看见"某个 API 没垫"，也不要拿到一张黑图还以为片子渲坏了。
 */
(function (global) {
  'use strict';

  var missing = [];
  var calls = {};
  var log = [];
  // 宿主（预览页 / --stills）正在往哪块 canvas 画。小程序里 canvas-id 是写死在 WXML 上的，
  // 预览页不可能有同名节点；没这个口子，用老接口的模块会找不着画布、画到一块 300×150 的
  // 丢弃 canvas 上 —— 拿到的就是一张全黑图，还被当成“片子渲坏了”。
  var target = null;
  var announced = {};   // 同一块预览画布只说一次“你的 canvasId 找不到”，不然每次重画都刷屏

  function note(name, detail) {
    if (missing.indexOf(name) < 0) missing.push(name);
    if (detail) log.push(name + '：' + detail);
  }

  function counted(name) {
    calls[name] = (calls[name] || 0) + 1;
  }

  function num(v, dflt) {
    return typeof v === 'number' && isFinite(v) ? v : dflt;
  }

  /** 离屏画布：小程序里 wx.createCanvas() 不给尺寸就 300x150，这里保持一样的默认。 */
  function makeCanvas(width, height) {
    var canvas = document.createElement('canvas');
    canvas.width = num(width, 300);
    canvas.height = num(height, 150);
    counted('createCanvas');
    return canvas;
  }

  /** 有的写法传对象 {type:'2d',width,height}，有的传两个数（小游戏）。 */
  function createCanvas(a, b) {
    if (a && typeof a === 'object') return makeCanvas(a.width, a.height);
    return makeCanvas(a, b);
  }

  function createOffscreenCanvas(a, b) {
    counted('createOffscreenCanvas');
    return createCanvas(a, b);
  }

  /* ---------------- 老接口 wx.createCanvasContext ---------------- */

  // 小程序 setter → 浏览器属性（名字不一样的直接赋值就行）
  var SETTERS = {
    setFillStyle: 'fillStyle',
    setStrokeStyle: 'strokeStyle',
    setLineWidth: 'lineWidth',
    setGlobalAlpha: 'globalAlpha',
    setLineCap: 'lineCap',
    setLineJoin: 'lineJoin',
    setMiterLimit: 'miterLimit',
    setShadowColor: 'shadowColor',
    setShadowOffsetX: 'shadowOffsetX',
    setShadowOffsetY: 'shadowOffsetY',
    setTextAlign: 'textAlign',
    setTextBaseline: 'textBaseline'
  };

  // 浏览器把字体拼在一个 font 字符串里，老接口拆成字号 /  families / 字重三个 setter，
  // 所以自己记住这三个数，每次重新拼一遍。
  function applyFont(real, self, size) {
    self.__fontSize = num(size, self.__fontSize || 12);
    real.font = (self.__weight || '400') + ' ' + self.__fontSize + 'px '
      + (self.__family || 'system-ui, "Microsoft YaHei", sans-serif');
  }

  // 签名和浏览器不一样的几个，单独翻译
  var SPECIAL = {
    // 老接口 setShadow(offsetX, offsetY, blur, color)，浏览器是四个独立属性
    setShadow: function (real, self, a) {
      real.shadowOffsetX = num(a[0], 0);
      real.shadowOffsetY = num(a[1], 0);
      real.shadowBlur = num(a[2], 0);
      real.shadowColor = typeof a[3] === 'string' ? a[3] : 'rgba(0,0,0,0.5)';
    },
    setFontSize: function (real, self, a) { applyFont(real, self, a[0]); },
    setFontFamily: function (real, self, a) { self.__family = a[0]; applyFont(real, self); },
    setFontWeight: function (real, self, a) { self.__weight = a[0]; applyFont(real, self); }
  };

  // 老接口独有、浏览器没有对应物的方法；剩下的名字两边同名，直接转调
  //
  // 注意它是个“活句柄”：小程序里这个对象一直指页面那块 canvas，而很常见的写法是
  // 模块开头就 ctx = wx.createCanvasContext('gameCanvas') 把它缓存下来。所以画布不能
  // 在创建那一刻定死（那一刻宿主还没 setTarget），要每次用的时候再找，否则缓存早于
  // 预览画布，之后所有绘制都落到一块丢弃 canvas 上，存出来就是一张全黑。
  function OldContext(idOrCanvas) {
    var self = Object.create(null);
    var host = null;
    var rt = null;
    function real() {
      var cv = findCanvas(idOrCanvas);
      if (cv !== host) { host = cv; rt = cv.getContext('2d'); }
      return rt;
    }
    function liveCanvas() { real(); return host; }

    // 所有 2D 方法就地转调：浏览器是即时提交的，所以不需要小程序那套"攒着等 draw()"
    var passthrough = [
      'save', 'restore', 'beginPath', 'closePath', 'moveTo', 'lineTo', 'bezierCurveTo',
      'quadraticCurveTo', 'arc', 'arcTo', 'rect', 'fillRect', 'strokeRect', 'clearRect',
      'fill', 'stroke', 'clip', 'translate', 'rotate', 'scale', 'transform', 'setTransform',
      'drawImage', 'fillText', 'strokeText', 'createLinearGradient', 'createRadialGradient',
      'createPattern', 'setLineDash', 'getLineDash', 'putImageData', 'getImageData',
      'createImageData', 'measureText', 'drawFocusIfNeeded'
    ];
    passthrough.forEach(function (name) {
      self[name] = function () {
        var r = real();
        if (typeof r[name] !== 'function') {
          note('ctx.' + name, '老接口要的这个方法浏览器的 2D context 没有');
          return undefined;
        }
        var v = r[name].apply(r, arguments);
        // 老接口 setFontSize 之后 measureText 才准：浏览器没有 fontSize 属性，自己记一份
        if (name === 'measureText' && typeof arguments[0] === 'string') {
          var width = v && v.width !== undefined ? v.width : 0;
          var cb = arguments[1];
          if (typeof cb === 'function') cb({ width: width });
          return { width: width };
        }
        return v;
      };
    });

    Object.keys(SETTERS).forEach(function (name) {
      var prop = SETTERS[name];
      self[name] = function (v) { real()[prop] = v; };
    });
    Object.keys(SPECIAL).forEach(function (name) {
      self[name] = function () { SPECIAL[name](real(), self, arguments); };
    });
    // 老接口用 draw() 提交；浏览器已经就地画完了，所以 draw 只负责回调
    self.draw = function (reserve, callback) {
      counted('createCanvasContext.draw');
      if (typeof reserve === 'function') callback = reserve;
      if (typeof callback === 'function') setTimeout(callback, 0);
    };
    self.getRect = function (cb) {
      var canvas = liveCanvas();
      var r = { left: 0, top: 0, width: canvas.width, height: canvas.height, id: canvas.__wxId || '' };
      if (typeof cb === 'function') setTimeout(function () { cb(r); }, 0);
      return r;
    };
    Object.defineProperty(self, 'canvas', { get: liveCanvas, enumerable: false });
    return self;
  }

  /** 找画布：① 直接给了 canvas；② 页里有同 id / data-id 的；③ 宿主正在往这块上画；④ 现造一块（记成缺口）。 */
  function findCanvas(idOrCanvas) {
    if (idOrCanvas && typeof idOrCanvas.getContext === 'function') return idOrCanvas;
    var id = typeof idOrCanvas === 'string' ? idOrCanvas : '';
    var byId = document.getElementById(id) || document.querySelector('canvas[data-id="' + id + '"]');
    if (byId) {
      byId.__wxId = id;
      return byId;
    }
    if (target) {
      // 每次 draw 都会重新找，所以这句话只在第一个 id 上说一次，不然日志会被刷满
      if (!announced[id]) {
        announced[id] = 1;
        log.push('createCanvasContext("' + id + '") → 页面上没这个 canvasId，画给预览画布（尺寸 ' + target.width + '×' + target.height + '）');
      }
      target.__wxId = id;
      return target;
    }
    note('createCanvasContext(' + id + ')', '页面上没有这个 canvasId，宿主也没给预览画布；先用 <canvas data-id="' + id + '"> 摆一块');
    var made = makeCanvas(300, 150);
    made.__wxId = id;
    return made;
  }

  function createCanvasContext(idOrCanvas) {
    counted('createCanvasContext');
    return OldContext(idOrCanvas);
  }

  /* ---------------- 系统信息 / 图片 / 导出 ---------------- */

  /**
   * 这块“屏”到底多大。① 宿主正在往哪块 canvas 画，它就是屏；② 页面在加载包**之前**声明的
   * window.__WXSCREEN__（很多模块在文件顶层就调 getSystemInfoSync，那一刻 target 还没定，
   * 不声明就会拿到无头窗口的 1280×900，排版全歪）；③ 实在没有才退到浏览器视口。
   */
  function screenSize() {
    if (target) return { w: target.width, h: target.height };
    var s = global.__WXSCREEN__ || {};
    if (num(s.w, 0) > 0 && num(s.h, 0) > 0) return { w: s.w, h: s.h };
    return { w: num(document.documentElement.clientWidth, 750), h: num(document.documentElement.clientHeight, 1334) };
  }

  function getSystemInfoSync() {
    counted('getSystemInfoSync');
    var dpr = num(global.devicePixelRatio, 1);
    var sc = screenSize();
    return {
      pixelRatio: dpr,
      screenWidth: num(screen.width, 1080),
      screenHeight: num(screen.height, 1920),
      windowWidth: sc.w,
      windowHeight: sc.h,
      statusBarHeight: 0,
      safeArea: { top: 0, left: 0, right: sc.w, bottom: sc.h },
      platform: 'devtools',
      SDKVersion: '3.0.0-browser-shim'
    };
  }

  function getMenuButtonBoundingClientRect() {
    var w = screenSize().w;
    return { top: 48, left: w - 96 - 7, right: w - 7, bottom: 48 + 32, width: 96, height: 32 };
  }

  function canvasToTempFilePath(opt) {
    opt = opt || {};
    var canvas = opt.canvas || findCanvas(opt.canvasId);
    var out = canvas;
    if (opt.destWidth || opt.destHeight) {
      out = makeCanvas(opt.destWidth || canvas.width, opt.destHeight || canvas.height);
      out.getContext('2d').drawImage(canvas,
        num(opt.x, 0), num(opt.y, 0), num(opt.width, canvas.width), num(opt.height, canvas.height),
        0, 0, out.width, out.height);
    }
    var type = opt.fileType === 'jpg' || opt.fileType === 'jpeg' ? 'image/jpeg' : 'image/png';
    var url = out.toDataURL(type, num(opt.quality, 0.92));
    var info = { tempFilePath: url, width: out.width, height: out.height };
    counted('canvasToTempFilePath');
    if (typeof opt.success === 'function') setTimeout(function () { opt.success(info); }, 0);
    if (typeof opt.complete === 'function') setTimeout(function () { opt.complete(info); }, 0);
    return url;
  }

  /** 小程序没有 new Image()，只有 wx.createImage()（基础库 2.7+）；页面里还能用 this.createImage()。 */
  function createImage(width, height) {
    counted('createImage');
    var img = new Image();
    if (width) img.width = width;
    if (height) img.height = height;
    return img;
  }

  /** 节点查询：分镜代码偶尔用 selectorQuery 拿 canvas 节点，这里按 data-id / id 找。 */
  function createSelectorQuery() {
    var chain = [];
    var q = {
      in: function () { return q; },
      select: function (sel) { chain.push({ sel: sel, all: false }); return q; },
      selectAll: function (sel) { chain.push({ sel: sel, all: true }); return q; },
      boundingClientRect: function (cb) { return run(q, cb); },
      fields: function (option, cb) { return run(q, cb, option); },
      exec: function (cb) { return run(q, cb); }
    };
    function rectOf(node) {
      var r = node.getBoundingClientRect();
      return {
        id: node.id, width: r.width, height: r.height,
        left: r.left, top: r.top, right: r.right, bottom: r.bottom,
        node: node
      };
    }
    function run(self, cb, option) {
      counted('createSelectorQuery');
      var last = chain[chain.length - 1];
      var res = [];
      var sel = last ? last.sel : '';
      var nodes = sel ? document.querySelectorAll(sel) : [];
      for (var i = 0; i < nodes.length; i += 1) res.push(rectOf(nodes[i]));
      if (last && !last.all) res = res[0] || null;
      // 同上：小程序页面里的 id 在预览里不存在，拿宿主的预览画布当那个节点，不然模块拿不到尺寸
      if (!res && target) {
        log.push('selectorQuery("' + sel + '") 没找到节点，当成预览画布（' + target.width + '×' + target.height + '）');
        res = last && last.all ? [rectOf(target)] : rectOf(target);
        if (option && option.dataset) res.dataset = target.dataset;
      }
      if (typeof cb === 'function') setTimeout(function () { cb(res); }, 0);
      return q;
    }
    return q;
  }

  /* ---------------- 装到全局 ---------------- */

  var wx = {
    createCanvas: createCanvas,
    createOffscreenCanvas: createOffscreenCanvas,
    createCanvasContext: createCanvasContext,
    getSystemInfo: function (o) { o = o || {}; if (o.success) o.success(getSystemInfoSync()); },
    getSystemInfoSync: getSystemInfoSync,
    getWindowInfo: getSystemInfoSync,
    getDeviceInfo: function () { return { platform: 'devtools', pixelRatio: num(global.devicePixelRatio, 1) }; },
    getMenuButtonBoundingClientRect: getMenuButtonBoundingClientRect,
    createImage: createImage,
    createSelectorQuery: createSelectorQuery,
    canvasToTempFilePath: canvasToTempFilePath,
    canIUse: function (api) { return /^canvas/.test(String(api)) ? true : true; },
    env: { USER_DATA_PATH: 'browser://' },
    showToast: function (o) { log.push('toast: ' + (o && o.title ? o.title : '')); },
    showLoading: function () {},
    hideLoading: function () {},
    setStorageSync: function (k, v) { try { localStorage.setItem('wx:' + k, typeof v === 'string' ? v : JSON.stringify(v)); } catch (e) {} },
    getStorageSync: function (k) { try { return localStorage.getItem('wx:' + k); } catch (e) { return ''; } },
    setStorage: function (o) { if (o && o.key !== undefined) this.setStorageSync(o.key, o.data); },
    getStorage: function (o) { if (o && o.success) o.success({ data: this.getStorageSync(o.key) }); },
    navigateTo: function () { note('wx.navigateTo', '预览里不跳页面'); },
    createInnerAudioContext: function () {
      note('wx.createInnerAudioContext', '预览不管声音，只验画面');
      var noop = function () {};
      return { src: '', volume: 1, play: noop, pause: noop, stop: noop, seek: noop, destroy: noop, onPlay: noop, onPause: noop, onEnded: noop, onError: noop, onTimeUpdate: noop };
    }
  };

  // 没列进来的 wx.xxx：不炸，记一笔，返回一个只会调 success/fail 的空函数，
  // 让画面继续往下画 —— 预览要能"带着缺口跑"，不是有一点没垫就整个黑屏。
  if (typeof Proxy !== 'undefined') {
    wx = new Proxy(wx, {
      get: function (target, key) {
        if (key in target) return target[key];
        if (typeof key !== 'string') return undefined;
        return function (arg) {
          note('wx.' + key, '没垫这个 API');
          var o = arg && typeof arg === 'object' ? arg : {};
          if (typeof o.success === 'function') setTimeout(function () { o.success({}); }, 0);
          if (typeof o.complete === 'function') setTimeout(function () { o.complete({}); }, 0);
        };
      }
    });
  }

  global.wx = wx;
  // 页面/组件脚本常带的这几个，先接下来不让它 ReferenceError；预览不跑小程序生命周期
  global.App = global.App || function (obj) { global.__WISHIM__ && (global.__WISHIM__.app = obj); };
  global.Page = global.Page || function (obj) { global.__WISHIM__ && (global.__WISHIM__.page = obj); };
  global.Component = global.Component || function (obj) { global.__WISHIM__ && (global.__WISHIM__.component = obj); };
  global.getApp = global.getApp || function () { return (global.__WISHIM__ && global.__WISHIM__.app) || {}; };
  global.getCurrentPages = global.getCurrentPages || function () { return []; };

  global.__WISHIM__ = {
    version: 'wx-canvas-shim 1.1',
    missing: missing,
    calls: calls,
    log: log,
    makeCanvas: makeCanvas,
    // 宿主开始往某块 canvas 画之前叫一声，老接口的 createCanvasContext / selectorQuery 就能落到它身上
    setTarget: function (cv) { target = cv && typeof cv.getContext === 'function' ? cv : null; },
    getTarget: function () { return target; },
    screenSize: screenSize
  };
})(window);
