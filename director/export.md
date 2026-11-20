# Export

Picture capture and mux for every film under `director/`. This replaces `core/render` and `mux.sh`. Voice, sampled music and foley are a separate step: they produce a wav, and this page only attaches that wav.

Two kinds of pixels go through this page. **Samples** are drawn by the user's storyboard JS and by nothing else. **Frames** are the compositor's output: a sample, a camera, and the join between two samples. The exporter only ever seeks `renderAt(t)`; what it decides is *which* `t` values get captured.

Working examples of the same steps: `tools/trailer/export.mjs` (the JumpFish trailer, one hand-written HTML) and `films/earth-to-fukushima/export.mjs` (a mode-D film: samples from the user's modules, re-projected in-betweens, `--sample` / `--inbetween` / `--stills` / `--fps` / `--preview` / full). A new film uses the same capture and ffmpeg flags. It does not reuse those files' shot times, score or output name.

## Page contract

The film is one `index.html`. The frame is a function of time. The same `t` always paints the same frame.

```js
window.DURATION = 18;          // seconds
window.FPS = 24;               // the storyboard JS's own rate wins; 24 when it states none
window.renderAt = (t) => {};   // synchronous; composites samples; no Date.now(), no unseeded Math.random()
window.assetsReady = true;     // true after fonts, images and the samples decode; true immediately when there are none
```

`renderAt(t)` composites; it does not draw art. `TECHNIQUE.md` §2 defines the shim in front of the user's JS and the compositor behind it.

Frame size comes from the storyboard JS's canvas; 1920×1080 when the JS has no preference. Another size (1080×1920, for example) is the viewport the script opens, and the page lays out from that size. Device pixel ratio is 1. Samples are never stretched to meet the viewport (cover-fit or letterbox, TECHNIQUE.md §2).

`?preview=1` plays with `requestAnimationFrame`. Export does not add that query.

## Tools

- Node, in the film folder.
- Playwright, launched as the installed Edge: `chromium.launch({ channel: 'msedge' })`. Do not download Playwright's own browser (`PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1`).
- `ffmpeg-static`. If its binary is missing after install, set `FFMPEG_BINARIES_URL` to `https://cdn.npmmirror.com/binaries/ffmpeg-static` and run `node node_modules/ffmpeg-static/install.js`.
- Both are already installed somewhere in this repo (`films/fat-fish/node_modules`, `tools/trailer/node_modules`). A new film folder may resolve them from there instead of running `npm install` again — that is what the mode-D example does, and it costs no download. Say which path you resolved when you report the film.

A storyboard JS that is an ES module cannot be imported under `file://` — the browser blocks it. Serve the film folder over a loopback port for the run (the mode-D example uses `node:http`), which also lets the page `fetch()` its own `shotplan.json` and `camera.json` so the page and the scripts read one source of truth. `--allow-file-access-from-files` works but leaves those numbers duplicated.

2D canvas launches Edge with `--disable-gpu`. A storyboard JS that renders with WebGL (three.js, a shader pass) leaves the GPU on — decide by reading the JS, not a `STYLE.md`. Without the GPU, a 3D frame is black.

## Three runs, in this order

From the film folder:

```powershell
$env:PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD = '1'
npm install                     # skip it if the folder resolves playwright and ffmpeg-static from elsewhere in the repo
node export.mjs --sample       # out/samples/*.png
node export.mjs --inbetween    # out/inbetweens/<a>__<b>.png
node export.mjs                # out/frames/%04d.jpg → out/<name>.mp4
```

### `--sample` — the user's JS, untouched

This mode runs the storyboard JS and nothing of yours. For each shot the shim exposes, it seeks that shot's stills (mode A: `u = 0, 0.5, 1` or the JS's own key times; mode B: the JS's own `t` at each shot boundary; mode C: copies the supplied images in; mode D: one still per module, drawn at that module's own framing — there is no `u` to seek) and writes one PNG per still into `out/samples/`, named `<shot>-<u>.png` where there is a `u` and plain `<shot>.png` in mode D.

The compositor is **not** involved: no camera, no caption, no grade, no letterbox — raw canvas at the JS's own size. If a still arrives wrong, the JS or the shim is wrong; report it, never repair it in code.

Like the full run, `--sample` first deletes every png, jpg, jpeg and webp in `out/samples/`, so stills from an earlier cut can't mix with this one. Do not delete `out/<name>.mp4` or images outside that folder. Read every still before going on: complete image, nothing clipped at the edge, shot list matching the JS. Do not start the joins until those stills pass.

### `--inbetween` — one strip per join

Reads `shotplan.json` (TECHNIQUE.md §3) and, for each neighbouring pair, renders the two samples plus the interpolated frames between them into one `out/inbetweens/<a>__<b>.png` contact strip, with the anchors drawn on and the loop-back frame at each end. This mode exists so a join is judged as a strip rather than after 40 seconds of rendering. Clear `out/inbetweens/` the same way. Look at every strip before the full run (DIRECTOR.md §5, gate two).

### Full export

1. Open Edge at the film size, device pixel ratio 1, without `?preview=1`.
2. Wait until `window.assetsReady === true`.
3. For `i` from 0 to `DURATION * FPS - 1`, call `renderAt(i / FPS)` and screenshot the viewport to `out/frames/0000.jpg` (JPEG quality 92, animations disabled).
4. Mux with ffmpeg-static. Delete `out/frames` after the mp4 exists.

After the run, every still's own timestamp frame must still match its PNG in `out/samples/` (loop-back check, TECHNIQUE.md §13 step 6).

ffmpeg arguments:

- `-framerate` equal to `window.FPS`
- image sequence `%04d.jpg` plus one wav
- video `libx264`, `-pix_fmt yuv420p`, `-crf 18`, `-preset medium`
- audio AAC, 192 kbps
- `-shortest`, `-movflags +faststart`

Output is `out/<name>.mp4`: the page size, `FPS`, duration `DURATION`, and one audio track.

## Audio

Put the finished mix at `out/mix.wav` before the full export (voice, music and foley already aligned to the same `t`). The muxer uses that file.

If the film has no mix yet, write a silent wav of length `DURATION` so the mp4 still has one audio track. Do not invent a score inside the exporter. The JumpFish `writeScore` in `tools/trailer/export.mjs` belongs to that trailer only.

Grain stays out of the page. It inflates every JPEG, and it must never be baked into a sample. Pixel and vector art add none. A film that wants grain adds it in ffmpeg, lightly, and never on a light field.

## Checks

On the finished mp4, confirm size, frame rate, duration and that an audio stream exists. Optional ffmpeg filters `blackdetect` and `ebur128` catch blank frames and loudness — blank frames show up mostly at joins, so `blackdetect` failing means a warp exposed empty space. Loudness target remains about −14 LUFS; that is fixed in the mix, not by re-encoding the picture.

Subtitles are drawn by `renderAt`, above the samples. A sidecar `.srt` is generated from the same `lines` in `shotplan.json` — the mode-d example writes it at the end of a full run; nothing may keep a second copy of those cue times.
