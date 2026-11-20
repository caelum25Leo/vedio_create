# Technique: how these films are built

The picture is the user's: a storyboard JS draws every sample still, and nothing here comes from the style library. What is built in code is the **joining** of those stills. Pair this with [`DIRECTOR.md`](DIRECTOR.md). Capture, stills and mux are [`export.md`](export.md).

```
the user's storyboard JS
   └─► samples:  export.mjs --sample ──► out/samples/*.png        (the JS's own pixels; nobody else touches them)
         │
shotplan.json (the one timeline)
   ├─► in-betweens: a re-projected camera between modules (mode D, §13.1) ──► out/frames/0000.jpg ──► export.md
   │     └─ or layers / mesh warp / flow between paired samples (§13.2, not implemented)
   ├─► camera, transitions, subtitles: composed over the samples
   ├─► voice:  TTS per line ──► speech-to-text check ──► word timings
   ├─► music:  score from the same shotplan ──► stems
   └─► sound:  procedural foley at event times
mix (duck, compress, balance) ──► out/mix.wav ──► export.md mux ──► film.mp4
```

There is no `STYLE.md` step, no `DEMO.md` step and no style engine in a film folder. Do not copy a brush, a shader or a palette out of `director/styles/`.

## 1. Requirements and install

Picture export needs Node, Playwright, `ffmpeg-static` and the installed Edge. Commands and flags are in [`export.md`](export.md).

**Mode D needs no Python at all** — its in-betweens are re-projected in the page (§2, §13.1), and the loop-back check runs there too. Python 3.11+ through `uv` is only for sound (§4–§6).

The correspondence-based joiners for modes A/B/C — mesh, optical flow, layer splitting — are **designed in §13.2 but not implemented**: there is no `tools/join.py` in this repo yet, and nothing in `export.mjs` calls Python for pixels. Until it exists, a mode-A/B/C film joins through the camera and designed transitions, and the treatment says so. When it does get written it runs from the film folder:

```powershell
cd d:\code\vedio_create\films\<name>
uv run --with numpy --with scipy --with opencv-python-headless python tools/join.py --check   # planned, not in the repo
```

`opencv` would supply the correspondence and flow machinery (`calcOpticalFlowFarneback`, `estimateAffine2D`, `applyLinearDisplacement`, `remap`); numpy/scipy the mesh and the colour maths. Nothing heavier: no torch, no video model — the whole method is geometric, and it must stay reproducible.

Voice and music packages still come from the Lemo library (`lemo-opuscar-main`) until they are replaced. Install a tier only when the film needs it:

| Command | Adds | Size |
|---|---|---|
| `sh plugin/skills/lemo-opuscar/scripts/setup.sh deps` | Python `.venv` (numpy, scipy, soundfile, soxr, pillow) for synthesizing sound. Not used to capture or mux | ~350 MB |
| `… setup.sh deps voice` | Kokoro + model (English, offline), edge-tts (Chinese, online), faster-whisper (voice check) | ~0.55 GB + a Whisper model on first check (~145 MB for English, ~480 MB for Chinese) |
| `… setup.sh deps music` | numba, for plucked strings (`pluck.py`); the sampler itself needs only the core | ~140 MB |
| `sh tools/fetch.sh instruments <lib>` | a sample library: `freepats`, `karoryfer`, `salamander`, `vcsl`, `vsco2ce` or `all` | 70–400 MB each |

No sample library is mandatory: without `director/core/audio/instruments/` the film ships with a silent or synthesized track, and that goes in `TREATMENT.md` (say it, don't hide it).

**The JS's own dependencies come first.** Read what the storyboard imports — images, fonts, `three`, a hand-written module — and install or copy exactly those into the film folder so the JS runs as the user wrote it. Never edit the JS to make it run: if an import can't be satisfied, tell the user which path is missing. If it references the style library's engines, that's the user's choice of look and you follow it; it is not your licence to go reading more of `director/styles/`.

A synthesized score needs no big download. A sampled one needs the libraries its instruments live in: `sampler.py` downloads the missing ones itself, all at once, when the score is rendered (`LEMO_FETCH=0` turns that off and prints the `fetch.sh` commands instead). Put `"$LIB/.venv/bin/python" core/audio/sampler.py --need <your mix.py>` at the top of a `build.sh`, so they arrive before the long picture render, not after it. A pack needs about twice its size free while it unpacks. Downloads come from npm, PyPI, GitHub Releases and Hugging Face (`HF_ENDPOINT` sets a mirror).

## 2. The picture is samples plus interpolation

A film folder still holds one `index.html` with the capture contract (`renderAt`, `DURATION`, `FPS`, `assetsReady`, viewport, jpeg sequence, mux) from [`export.md`](export.md). What `renderAt(t)` does has changed: **it composites, it doesn't draw.**

```
films/<name>/
  storyboard/     the user's JS and everything it loads — copied, never edited
                  `brief:drawing` points at the entry file inside it
  shim.mjs        the adapter: exposes the JS's shots and paints each one into an offscreen canvas
  shotplan.json   the timeline: shots, sample files, joins, anchors   (§3)
  film.js         the compositor: sample → camera → join → caption → frame    ← the only drawing-ish code you own
  tools/join.py   the in-between solvers (mesh, flow, layers) — §13.2, not implemented yet
  out/samples/    PNGs produced by the JS alone (--sample)
  out/inbetweens/ join sheets produced by --inbetween
```

The brief form fills `storyboard/` for you: it copies the picked JS together with its sibling modules and assets, and writes the read-out (mode, canvas size, frame rate, shot ids, shot order, missing imports, environment notes) into `brief:materials`; the shot order it guesses also lands in `brief:shots` (one line of shot ids = the order the shots play). Trust that report over your own guess, but open the JS too — the mode is inferred from what the file looks like, and an unusual JS needs a human read.

**The shim** is the one place the JS is touched, and only from the outside. Whatever the JS exposes, the shim gives `film.js` the same two calls:

```js
listShots()            → [{ id, dur?, canv: {w, h}, stills: [{ u, file }] }]
paintShot(id, u, ctx)  → paints that shot at normalised progress u into the ctx you hand it
```

Those two only cover modes A/B/C, where a shot has a progress to seek. **Mode D has no `u`** — a module draws one static framing and stops — so its shim exposes the framing instead of a timeline. This is the shape that worked in `films/earth-to-fukushima/shim.mjs`:

```js
configure({width, height, camera})   // read the measured framings in, build one frame per shot
camAt(idA, idB, u)                   // the interpolated camera between two shots (§2 mode D)
shotPixels(id) / shotCanvas(id)      // that module's own still, at its own framing
basePixels(cam, weight)              // re-project the shared world at an arbitrary camera, mask blend included
sceneCanvas()                        // the layer one module draws privately, cut out as a transparent sheet
```

Same rule as the two-call contract: the shim calls the JS from outside and never redraws it.

Pick the mode from what the JS actually offers, and write it into `TREATMENT.md`:

| The JS gives… | Mode | `--sample` captures | Cost |
|---|---|---|---|
| a per-shot `draw(ctx, u)` | **A** | each shot at `u = 0, 0.5, 1` (or its own key times) | real in-betweens possible inside a shot |
| one `render(canvas, t)` for the whole piece | **B** | seek the JS's own `t`, one PNG per shot boundary | shots arrive as whole images: joins are camera + warp only |
| a list of finished images / no time input | **C** | copy them in as samples | static shots: camera and transitions only |
| one `draw(ctx, stage)` per module, each a static framing at its own scale | **D** | one PNG per module, at that module's own framing | the motion is all camera *between* shots: the in-betweens must be re-projected, never scaled up |

**Mode D — the push-in.** The storyboard is a folder of modules, one shot each, exporting `{ id, name, draw }` with no progress argument: a whole planet, then an island chain, then a coastline, then one plant. Nothing inside a shot can be seeked, so the film's movement is the camera travelling between the modules' own framings. Those framings are usually three numbers (`radius`, `originX`, `originY` from a `frameFor(w, h)`), so:

1. capture one still per module at its own framing (that is `--sample` in mode D);
2. interpolate the camera, not the pixels — `radius` in log space, the anchor pixel linearly, then solve `originX/originY` back from the anchor; endpoints reproduce both modules' own frames, so the joins land on the stills instead of jumping;
3. redraw the shared world→screen projection for every intermediate frame. A 20× jump in one join is beyond any honest warp or upscale, and the modules' own bitmaps are capped (often ~640–720 px), so scaling a still up is blurring, not zooming.

Modules that only carry a mask and never draw are not shots — the scan lists them apart from the shot table. The projection base they share (the file everything else imports) usually draws the widest shot, so it goes first. Watch which mask is valid for which latitude band — a coastline mask used outside its band invents land.

**Three things that bite, all measured on the worked example** (`films/earth-to-fukushima/`, its `TREATMENT.md` §4 has the numbers):

1. **Export the framings at full precision.** If a module derives texture from a hash of the projected coordinates (`sin(lon*127.1+lat*311.7)*43758.5453` taking the fraction), a framing rounded by 1e-10 misaligns the whole grain field, and any diff you run afterwards measures noise instead of content. Write raw doubles into `camera.json`; round only for printing. Rounding them cost that film a matte that came out 69 % of the frame instead of 29 %.
2. **A mask switch inside a join is visible.** Two land masks at different resolutions disagreed on **4.71 % of pixels (mean ΔE 167.9)** over one shot's framing — a hard swap mid-push reads as a jump. Cross-fade between the two over the last stretch of that join, and land fully on the narrow-band mask by `u = 1` so the endpoint still matches its own still exactly. Re-measure this whenever a mask's resolution changes (`mask-probe.mjs` in the example prints it).
3. **A private layer can only be scaled, so feather it.** Where a module draws something no interpolated camera can reach (the plant inside `paintFukushima`), cut that layer out by subtracting the same-framing base, then composite it scaled by `s = radius / radius_shot` about the framing origin, at `alpha = min(1, s)`, feathering the frame edge by `(1 - s)` — the layer's boundary is the other shot's canvas edge, not a feature of the ground. At `s = 1` the feather must go to zero, or the loop-back check fails.

Never redraw a shot in the shim, never "normalise" its size by stretching it, never re-light it. If the JS disagrees about shot count, order or duration, that's a report to the user, not a fix.

Optional, not read by the exporter:

```js
window.EV = [{t, type, ...}];      // sound and cue events for the mixer
window.TEXTS = (t) => [{id, text, x0, y0, x1, y1}];   // every text visible at t (pass: true = not meant to be read in full)
```

Rules that keep it reliable:

- **Deterministic.** No `Date.now()`, no `Math.random()` without a seed, no state carried from the previous frame. Any frame can be rendered alone by one `renderAt(t)`. Optical flow and mesh solves are seeded and cached, so a re-run gives the same pixels.
- **Samples are never resampled.** Composite at the sample's own resolution inside a canvas of the film size; where the frame size differs, cover-fit (crop the overflow) or letterbox (extend the film's own edge colour) — decide per film, state it in the treatment, and don't scale to fit. Any warp reads the sample pixels at full resolution.
- **One world → screen function.** Every camera move, iris, follow-cam and caption offset goes through it, so an anchor tracked at sample pixel `(x, y)` lands where the camera put it. Never hand-type screen coordinates for something that lives inside a sample.
- **Cache what the samples can't give you.** A join solved once per pair (flow field, mesh, matte — all §13.2 machinery, so nothing to cache in mode D yet) goes to `out/cache/<a>__<b>/`, and `film.js` reads it. Recompute only when the join plan changes — otherwise a 40-second film re-solves thousands of times. Mode D's equivalents, which *are* worth caching: the four module stills and the cut-out layer, both keyed on the framing numbers.
- **Hold the look steady.** One global grade applied after the join, across the whole film; per-shot grade makes neighbours jump.
- **Step where the art asks for it.** If the samples are stop-motion or cel, in-betweens may step at 12 fps (`round(t * 12) / 12` for the picture layer) while the camera keeps moving every frame. A stepped camera reads as lag.
- Grain, if the film wants any, is an ffmpeg filter at mux time — never baked into a page, and never into a sample.

Three commands, in this order, all inside the film folder:

```powershell
node export.mjs --sample      # only the user's JS runs; writes out/samples/
node export.mjs --inbetween   # writes out/inbetweens/<a>__<b>.png join sheets from shotplan.json
node export.mjs               # full render, frame by frame
```

Look at both folders before the full run. Hold subtitles long enough to read (DIRECTOR.md §7) by inspecting those stills and the cue times.

### 2.1 If the JS came from a WeChat mini-program

Nothing in it has to be rewritten for the browser. `tools/preview/wx-shim.js` maps both canvas generations:

- `wx.createCanvas()` / `wx.createOffscreenCanvas()` → a real `<canvas>`. Their `getContext('2d')` is already the browser API, so `createImageData` / `putImageData` / `drawImage` need nothing.
- the older `wx.createCanvasContext(id)` → its setters (`setFillStyle`, `setLineWidth`, `setFontSize`, `setShadow(ox, oy, blur, color)`, …) become properties, its split font setters are re-assembled into one `font` string, and `draw()` becomes a callback-only no-op, because a browser commits as you call. `measureText(text, cb)` answers both styles.
- `wx.getSystemInfoSync()`, `wx.createImage()`, `wx.createSelectorQuery()`, `wx.canvasToTempFilePath()` are there too.

Anything unmapped is **recorded and printed in the page**, never swallowed — a Proxy catches unknown `wx.*` names and shows them in red. A black rectangle must not be mistaken for a broken render; that distinction is the whole first gate.

`node tools/preview/build-preview.mjs films/<name>` bundles the modules, everything they import, and any file the page `fetch`es into one self-contained `preview.html`: a plain `<script>`, no ES modules, so it opens over `file://` with no server (compare §2's exporter, which needs the loopback http server for exactly that reason). Two views:

- **成片** calls the film's own `renderAt(t)` — the same code path the mp4 is rendered from, so the join sheets and loop-back numbers it prints match `--inbetween` (verified on the example: six endpoints, SSIM 1.0000, mean Δ 0.00 in both).
- **小程序** calls each module's default-exported `draw(ctx, stage)` at a size you type (750×1334, 375×667, the film size), which is what the phone shows, with no compositor and no captions in the way.

`--verify` loads it headless in msedge and reports page errors, unmapped `wx` calls, and per-shot pixel statistics (mean / σ / share of lit pixels), so a blank shot cannot pass quietly. Note that σ alone is weak: the `coast` shot is land-green next to sea-blue, whose *luminance averages* are nearly equal, so it reads σ ≈ 2 on a frame that is plainly drawn. Trust the screenshot too.

`--stills` does the same walk but keeps the pixels: one `out/preview/<file>.png` per storyboard JS, at a size passed down the deep link (`?view=mini&w=&h=`, default 750×1334), plus a same-named `.txt` holding either the statistics or the reason nothing was drawn (no default-exported `draw()`, not in the bundle, `draw()` threw). *Every* file under `storyboard/` gets a `.txt`, so a blank is always explained rather than left to look normal. The size matters twice over: the page declares `window.__WXSCREEN__` **before** loading the bundle, because modules that read `wx.getSystemInfoSync()` at file top level size their layout from it — if the target canvas were only known later they would get the headless window's 1280×900 and draw a crooked frame that looks fine but isn't.

Two things worth knowing before you plan around this file:

1. The bundler is hand-written because the repo has no esbuild or rollup and installs nothing (§1). It supports the module forms named in its own error message and **exits non-zero on anything else** — a silently mis-bundled module yields pictures nobody can trust. Data files are inlined, so a `preview.html` is ~400 KB when the masks are big.
2. **Do not try to embed this page in the Qt tool.** `Qt 5.7`'s WebEngine is Chromium 45: no ES modules, no `**` (Chrome 52), no spread (46), no `async` (55). This isn't only our compositor's fault — the user's own `storyboard/earth.js` uses `**` at lines 251 and 256, so even the raw art needs a modern engine. The syntax check in `build-preview.mjs` prints which constructs a given bundle needs. So the Qt tool works in pictures, not pages: its bottom **预览** button opens a dialog (`UI/previewdialog.*`) that lists every `storyboard/*.js` as one button and shows the still `--stills` wrote for it — or the `.txt` reason when there is no still, never an unexplained blank. **刷新** re-scans the folder (a newly copied JS gains a button) and re-renders; **在浏览器打开这一格** is the one escape hatch, handing Edge the deep link `preview.html?view=mini&shot=<file>`. **读取 JS** builds nothing and opens nothing: reading and previewing are separate, so picking files no longer throws you out of the window.

<!-- brief:seconds -->
<!-- /brief:seconds -->

<!-- brief:size -->
<!-- /brief:size -->

## 3. One shotplan drives everything

`shotplan.json` is the single source of truth. The frames, the score, the mixer, the subtitles and every check read the same numbers out of it — nothing is duplicated in a `.js`:

```jsonc
{
  "size": [1920, 1080], "fps": 24, "mode": "B",
  "shots": [
    { "id": "earth",  "t0": 0.0, "t1": 3.2,  "stills": ["out/samples/earth-0.0.png"],
      "camera": { "from": [0, 0, 1.0], "to": [40, -30, 1.12], "ease": "smooth" },
      "static": true },
    { "id": "shore",  "t0": 6.5, "t1": 10.0, "stills": ["out/samples/shore-0.0.png"] }
  ],
  "joins": [
    { "a": "earth", "b": "plates", "method": "warp", "frames": 14, "ease": "slow-out",
      "anchors": [ { "name": "island", "a": [512, 640], "b": [388, 720] },
                   { "name": "hero",   "a": [540, 300], "b": [540, 402] } ],
      "fallback": "camera" }
  ],
  "EV":  [ { "t": 3.2, "type": "cut", "from": "earth", "to": "plates" } ],
  "lines": [ { "t0": 17.2, "t1": 20.0, "text": "…", "safe": [70, 1268, 1010, 1360] } ]
}
```

`method` is one of `layer` (split the two samples into planes, parallax between them), `warp` (mesh / thin-plate spline driven by the anchors), `flow` (optical-flow interpolation), `camera` (one sample held, moved), `blend` (≤3 frames, and say why in the treatment). `warp` / `flow` / `layer` need the solvers of §13.2, which are not implemented — until they are, only `camera` and `blend` are reachable. `anchors` are measured from the samples, not guessed — §13.

A mode-D join carries the camera instead of a method, and `films/earth-to-fukushima/shotplan.json` is the shape to copy: `"method": "camera"`, `"project": true`, `"ease"`, `"zoom"` (the measured multiplier, so a reader can see why no warp was attempted), `"mask"` (`global` / `coast` / `blend`) with `"maskBlend": [from, to]` when it cross-fades, `"layer"` for a scaled private layer, and one `anchors` entry per tracked part with its pixel position in each shot.

There is no cue checker in this workflow: `cuecheck.py` and `readcheck.mjs` live only inside `director/styles/*/demo/tools/`, which this flow does not read. Compare every visual hit in `EV` with the cue it should land on by hand, or with a few lines in the film's own export script. Aim for 0 ms.

## 4. Voice

- **TTS is only the default.** If the user brings a recording or names a voice, use that. Otherwise:
  - **English**: [Kokoro](https://github.com/thewh1teagle/kokoro-onnx), local, many voices (an unknown `voice` name makes `tts.py` print the full list; `af_` / `am_` are American, `bf_` / `bm_` British; cast the narrator for this film, not the demo's). `core/tts/tts.py lines.json out/` writes one WAV per line and a durations file. Its Chinese voices (`"lang": "cmn"`) work offline but sound plain.
  - **Chinese**: `core/tts/tts_zh.py lines.json out/` uses [edge-tts](https://github.com/rany2/edge-tts) with Microsoft's neural voices (`zh-CN-XiaoxiaoNeural`, `zh-CN-YunxiNeural`…), with the same outputs as `tts.py`, a rate per line and the silence trimmed. **It needs a network connection.**
- Spell numbers out in the TTS text and write them as digits in the subtitles.
- **Check every line**: `core/tts/asr_check.py` transcribes each WAV with faster-whisper and compares it with the script (`--lang zh`: numbers by value, so 十九 matches 19, and the rest character by character); re-generate until it exits 0. A misheard character in a short Chinese line is normal: listen, and if it is right, put what the model heard in the line's `asr` field. It also writes word timestamps for placing lines and subtitles.
- **Before mixing**: TTS has a high peak-to-average ratio. Compress the voice first, then balance by RMS: voice about 10 dB above the music.

## 5. Music

Write an original score from the cue map. Choose the instruments from what the samples feel like (their period, their weight, their temperature), state that choice once in `TREATMENT.md`, and keep the same palette across the film; no generic piano and strings.

- **Samples**: `core/audio/sampler.py` plays real instruments from free libraries (VSCO 2 CE, VCSL, FreePats, Karoryfer, Salamander Grand Piano; list and licences in `core/audio/INSTRUMENTS.md`). Write the score as events `(time, instrument, pitch, duration, velocity, pan)`. Long notes are extended seamlessly, and round-robins avoid the machine-gun effect.
- **Plucked and folk strings** (guqin, pipa, shamisen, banjo…): `core/audio/pluck.py`, physical modelling (Karplus–Strong) with bends and vibrato.
- **Synthesis** with numpy for chip tunes, drones and effects; no download needed.
- **Balance the low end.** Synthetic scores get bass-heavy quickly. Measure energy per band after mixing; keep 20–120 Hz around −3 dB relative to the rest, skip pad notes below MIDI 48, and give the bass some 2nd and 3rd harmonics so it speaks on small speakers.

## 6. Sound effects and mix

- `core/audio/sfx.py` synthesises foley procedurally (click, whoosh, thump, ding, paper, noise beds…) with filters, envelopes and `add(buffer, sound, at, gain, pan)`. Drive it from `shotplan.json`'s `EV` (mirrored to `window.EV`), so every sound sits exactly on its frame. Every join gets one: the cut, the whoosh of the warp, the landing at the new sample.
- **Master**: write the mix to `out/mix.wav`, then mux with [`export.md`](export.md). Loudness about −14 LUFS is fixed in that wav (a limiter on the true peak). On light backgrounds use less grain; for pixel and vector styles, none. Grain is an ffmpeg filter at mux time, never drawn in the page.
- Films with grain baked into the picture compress badly; re-encode with `-tune grain` and a higher CRF.

<!-- brief:materials -->
<!-- /brief:materials -->

## 7. Subtitles

- Burn captions into the frame so [`export.md`](export.md) captures them, styled to match the lettering already inside the samples (DIRECTOR.md §1: the samples set the type, you follow it). A sidecar `.srt` sits in the film folder and is generated from the same `lines` in `shotplan.json` — by the film's own `export.mjs` (the worked example writes it at the end of a full render) or by hand, whichever way, never from a second set of cue times.
- Captions sit in a sample's own safe area — the band where that still draws and letters nothing. Measure it per shot (as part of §13's inventory) rather than reusing one position for the whole film, and never cover a subject the sample worked to place.
- Timing rules are in DIRECTOR.md §7.

## 8. Review loop

1. `node export.mjs --sample`, then open every file in `out/samples/`: the JS's own pixels, complete, matching the shot list. Nothing here is yours to fix — a problem is reported to the user.
2. `node export.mjs --inbetween`: one strip per join (both samples at the ends, the interpolated frames between, anchors drawn on). Look at all of them enlarged. The loop-back numbers come out of this same run — in mode D `film.js` implements the check and `export.mjs` prints it per pair (§13.1). The ghosting and invented-content reports belong to `tools/join.py --check`, which is not implemented (§13.2).
3. For every join, a 0.2 s strip across the cut with the same `renderAt` path, watched enlarged: does anything appear that is in neither sample.
4. On the final file: `ffmpeg -af ebur128` (loudness), `blackdetect` (blank frames), whisper again on the final mix if it has voice, and every sample timestamp frame diffed against its PNG (it must match).
5. Watch it once, at full speed, with sound.

## 9. 3D notes

Only when the user's JS is 3D: three.js rendered headless through WebGL2 on the GPU, with the GPU left on in [`export.md`](export.md). Depth of field and ambient occlusion then live in the film's own post pass, applied after the samples arrive — never added inside the JS.

- Lighting: soft area lights plus a Poly Haven HDRI at low intensity reads more "real" than bright ambient light.
- A perspective depth pass breaks on an orthographic camera. Swap `perspectiveDepthToViewZ` for `orthographicDepthToViewZ`, or the depth of field silently fails.
- A 3D sample cannot be mesh-warped safely: joins between 3D shots go through the camera (`method: "camera"`) or a designed transition.

## 10. Splitting a sample into layers

**This whole section is the unimplemented half of §13.2** — it needs the OpenCV machinery that has no code in this repo yet. Mode D never comes here: its one layer is cut out by differencing two renders of the same framing inside the page (`sceneCanvas()` in the worked example), which needs nothing but the JS's own painter.

Warping and parallax need the subject separated from its background. That separation is derived from the sample's own pixels — it is not a licence to redraw.

- Segment with what is there: colour and edge gradients (`cv2.pyrUp`/`Canny`/`grabCut`/`medianBlur`), the anchor set from §13, and, if the user supplied a reference image of the character, that image as the mask seed. Save masks to `out/cache/` so they are computed once.
- Repair only where the layer moves away from the frame: edge extension (`BORDER_REFLECT_101` on the background plane) and a thin band of inpainting (`cv2.inpaint`, `TELEA`) at the boundary. Never fill a hole in the middle of a subject — that is inventing content.
- Move a part, not the whole subject, only when the part is measurable in both samples (the same hand, the same head). Two anchors and a local mesh cover most of it. If a part appears in one sample and not the other, the join is a camera or transition problem.
- Keep the layers honest: the union of what you move must be visible in the join sheet. If a strip shows a seam, fall back to `method: "camera"`, and say in the treatment which join did.

## 11. Assets, credits and fonts

Only CC0, CC BY or OFL material, each listed in the film's `CREDITS` with its source and licence; `core/audio/sampler.py credits(names)` writes the lines for the instruments you used. Public-domain works count as CC0; "no known restrictions" does not. Data or images under other open licences (ODbL, CC BY-SA, open-hardware licences) only after you have named the licence and its terms (attribution, share-alike) to the user and they agree. The user's own material is fine as it is — **the storyboard art is theirs**: `CREDITS` carries one line for it ("storyboard art and sample stills: the user's `<file>`"), and it needs no licence check from you.

A film that states facts (real data, scans, maps, measured numbers) keeps a `FACTS.md` in the project folder: each claim with its source URL and the quoted wording, or the measurement that produced it.

Fonts: the JS may carry its own; use those first. Otherwise Google Fonts (OFL), self-hosted in the project's `fonts/` (`https://github.com/google/fonts/tree/main/ofl/<family>`), matching the lettering the samples already use — weight, serif or sans, printed or written. `core/fonts/` holds a few Latin display fonts and a Chinese display font with only about 200 glyphs, not enough for running Chinese text. For Chinese, subset a CJK font to the characters you use: the Google Fonts CSS API with `&text=` returns just those glyphs (`https://fonts.googleapis.com/css2?family=Noto+Sans+SC:wght@700&text=<URL-encoded characters>`). Download the font file it points to into the project's `fonts/` and load it with `@font-face`; don't link Google Fonts at render time, and don't add a face the samples contradict.

## 12. Where technique questions go now

The style library is not a source for these films. Look here instead:

| If you need… | Look at |
|---|---|
| the capture contract, sample/join/whole-film commands, ffmpeg flags | [`export.md`](export.md); `tools/trailer/export.mjs` for the trailer wiring, `films/earth-to-fukushima/export.mjs` for a mode-D film (`--sample` / `--inbetween` / `--stills` / `--fps` / `--preview` / full) |
| a mode-D film end to end | `films/earth-to-fukushima/`: brief, `camera.mjs` + `camera.json`, `shim.mjs`, `film.js`, `shotplan.json`, `TREATMENT.md`, `CREDITS.md` |
| the frame-to-frame look of an existing cut | `films/fat-fish/` and `films/window-cup/` (older method: picture only for structure and export wiring, not for art) |
| procedural foley, a sampled score, the voice chain | `core/audio/`, `core/tts/` (§4–§6) |
| an optical-flow or mesh-warp reference | none yet — `tools/join.py` is the spec in §13.2, not code |
| a post pass the film wants (grain, damage, letterbox colour) | your own pass in `film.js`, applied after the join, never into a sample |

`director/styles/` is off for the look: no brush, palette, type, material or transition habit is taken from it, and no `STYLE.md`/`DEMO.md` is opened to decide anything about this film. It stays reachable for one thing only — if the user's own JS imports one of its engines, that import is theirs and you make it work.

## 13. The in-between procedure

Two different jobs go by this name. Pick one from the mode and write which into `TREATMENT.md`.

### 13.1 Re-projection (mode D) — implemented

One pass per neighbouring module pair, run before any full render. The worked example is `films/earth-to-fukushima/` — read its `shim.mjs`, `film.js` and `TREATMENT.md` §4 before writing a new one.

1. **Measure each framing once**: `radius`, `originX`, `originY` and where the anchor lands in pixels, at the export size and never at a module's internal sprite size. Write them raw — full precision, §2's first bite — into `camera.json`, and have `film.js` fetch that file so the measuring script and the page cannot drift apart.
2. **Interpolate the camera, never the pixels**: `radius` in log space, the anchor pixel linearly, then solve `originX`/`originY` back from the anchor. The formula must reproduce both modules' own framings at `u = 0` and `u = 1`; that property is what makes a join land instead of jump.
3. **Re-project the shared world for every intermediate frame** through the one world→screen entry the modules already expose. Where two masks cover the same ground at different resolutions, cross-fade rather than switch (§2, bite #2).
4. **Composite what no camera can reach** as a scaled layer, `alpha = min(1, s)`, edge feathered by `(1 - s)` (§2, bite #3).
5. **Ease `u`** from the shotplan (`smooth` = smoothstep, `slow-out`, `hold-then-go`). A join that starts at full speed reads as a slip, whatever method made it.
6. **Validate in the page.** `--inbetween` prints, per pair and per end: SSIM, mean ΔE, worst channel difference, share of pixels off by more than 8. Pass is SSIM ≥ 0.97 **or** mean ΔE ≤ 2 (DIRECTOR.md §9). A drift means the interpolation is leaking toward something else — fix the method, not the threshold. Ghosting and double-edge tests don't apply, since nothing is warped; what to look for in the strip instead is a hard rectangular edge, which means a layer got pasted rather than grown.

### 13.2 Correspondence-based joining (modes A/B/C) — spec, not implemented

`tools/join.py` does not exist in this repo, and no exporter calls Python for pixels. Until it is written, a mode-A/B/C film joins through the camera and designed transitions, and the treatment says so. The steps below are the spec; the thresholds are what a first pass should hit. Every step is seeded, cached and re-runnable.

1. **Inventory each sample once**: content bbox (alpha or non-background mask), the dominant colours, the brightness curve, and a text-safe band (the rows where that still draws and letters nothing). Write the numbers into `shotplan.json`; they drive cover-fit, caption placement and which shot can carry the title.
2. **Find the correspondence** for the pair: feature match (`cv2.SIFT_create` or `ORB`, `BFMatcher` kNN with a 0.75 ratio test) plus the named anchors. `estimateAffine2D` over the matches gives the dominant motion; what's left is real deformation. Under ~15 % inliers the two stills share no structure — that pair is a `camera` join or a transition, never a warp, and the treatment says so.
3. **Pick the method from the numbers**, not from preference:
   - same subject, small displacement, inliers ≥ 0.4 → `warp`: a 24–32 cell mesh, TPS or as-rigid-as-possible driven by the anchors;
   - background holds, subject travels → `layer`: segment (§10), parallax the planes, camera carries the rest;
   - the camera moved and the whole frame restructured → `flow`: `calcOpticalFlowFarneback(pyr_scale=0.5, levels=3, winsize=21–31, iterations=3–5, poly_n=5, poly_sigma=1.1)`, forward and backward, holding the nearer frame wherever the fwd–bwd error exceeds 1 px (that occlusion test is what stops ghosts);
   - nothing above fits → `camera` plus a designed transition.
4. **Solve once, interpolate cheaply**: compute the field, warp and masks at `u = 0, 0.25, 0.5, 0.75, 1`, cache to `out/cache/<a>__<b>/`, and let `film.js` blend between cached solves at the exact `u`. 432 frames then cost one solve each, and any frame stays renderable alone. Key the cache on a sha256 of the two samples plus the method's parameters, so a stale solve invalidates itself.
5. **Ease `u`** from the shotplan (`smooth` = smoothstep, `slow-out`, `hold-then-go`). A join that starts at full speed reads as a slip, whatever method made it.
6. **Validate** (`tools/join.py --check`, once it exists), per pair: loop-back against both samples at `u = 0/1` (SSIM ≥ 0.97 or mean ΔE ≤ 2); a ghosting score (Laplacian energy in the middle frames against both samples — a spike is double edges); an invented-content test (pixels covered by neither sample after edge extension: over ~1.5 % of the frame means the matte leaked or something was drawn); a jump test (mean |Δ| between consecutive middle frames against the sample-to-sample distance). Any failure downgrades that pair's `method` and is listed in `TREATMENT.md`, not fixed by loosening the threshold.
7. **Keep colour continuous without grading the art**: one mean/std transfer in Lab from B onto A, strength 0.25–0.4, applied *inside* the join and faded out by the time B owns the frame. Never per-shot grade, never restyle a sample.
