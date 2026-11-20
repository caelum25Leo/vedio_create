# Directing a film from the user's storyboard JS

The method behind every film built from a user-supplied storyboard JS. The companion is [`TECHNIQUE.md`](TECHNIQUE.md) (how to build).

**You do not draw.** The user's JS draws every sample still in `out/samples/`, exactly as it was written. Your film is made of two things: the order and duration of those stills, and **the in-between frames between them** — the frames the JS does not produce. Nothing about the look is yours to decide.

Three rules that override everything below:

- **No `STYLE.md`, no `DEMO.md`, no style engine.** Don't read `director/styles/` for the look, don't copy a brush or a palette from it, don't judge the user's art against it. The style *is* the sample stills. The `style` field below is a leftover of the old flow: the brief form still fills it, and it decides nothing here.
- **Never redraw a sample.** "Simplify", "fix", "fill in" a still in code is the worst failure this method has: the film then reads as a stick-drawing imitation of the user's art. A sample is pixels you use as they are.
- A film is still judged in this order: **sound, rhythm, camera, directing**. Since the picture is given, those four are the whole of your contribution.

---

## 1. Take the brief

The art is already decided; the brief says what the film is about, how long, and in which frame size. Ask once, in a single message (AI_GUIDE.md §1), then no more questions.

**First, read `films/<name>/brief.md`.** Four fields decide whether you can start: `brief:drawing` (the entry JS inside `films/<name>/storyboard/`, next to every module and image it loads), `brief:materials` (the read-out the brief form took from that JS — guessed A/B/C/D mode, canvas size, frame rate, shot list, missing imports; mode D also writes how the push-in joins, where the anchor is, and which modules need a `wx` stub), `brief:shots` (one line of shot ids: the order the shots play), and `brief:motion` (the hero's keyframes: `t x y action`). If `brief:drawing` is empty, stop and ask for the JS: without it there is nothing to work from. The read-out is a hint, not a verdict — open the JS yourself too and check which shots it exposes and how many stills each shot gives. `films/earth-to-fukushima/` is the worked mode-D example, carried end to end (brief + four framing modules + `camera.mjs`/`camera.json` with the measured framings + `shim.mjs`/`film.js`/`shotplan.json`/`export.mjs` + the stills and join sheets + `TREATMENT.md`/`CREDITS.md` + the delivered mp4); when a JS of that shape arrives, read its brief first and its `TREATMENT.md` §4 second — that section is the list of things that go wrong in this mode.

The empty marker pairs below are left in place only so an older brief still parses. Nothing is written into this file any more: the brief stays in `brief.md`, and what you see between these pairs is never a film's content.

Defaults:

- Frame size and frame rate come from the JS's canvas. `24 fps` unless the JS states another rate. In mode D the modules state neither, so the brief's `size` is what the user typed in the form — take it as the target, and never mistake a module's internal sprite cap for the frame size. If the JS's size and the brief's `size` disagree, ask the user which one wins; never resample or stretch the art to settle it yourself.
- Length **30–60 s**, made of the shots the JS provides; hold each still long enough to read. If the JS has more shots than the requested length fits, say so and ask which to drop rather than cutting silently. A mode-D push-in is the exception: one continuous move needs no 30 s to read, so there the brief's own timeline wins (the worked example is 24 s, and the UI ships it as the default brief). When the brief states a length in one field and a timeline in another and the two disagree, ask which wins — do not silently cut the tail to meet the shorter number.
- Voice and subtitles in the language the user writes in. Subtitles burned in and exported as `.srt`.
- **Type follows the stills.** Whatever lettering the JS draws (titles, labels, its own caption look) is the film's typography: place your subtitles in the same register, and pick one openly licensed face of the same kind when the JS gives you none. Do not invent a new caption design, and do not move or cover what the stills already drew.
- Loudness −14 LUFS.

Say it in the questions when it matters: a good Chinese voice uses Microsoft's online service (edge-tts) and needs a connection; offline, Kokoro reads Chinese but sounds plain (TECHNIQUE.md §4).

Requests that only change the plan:

- **No voice, or no subtitles.** Drop those steps and say so in the treatment.
- **Their own voice recording.** Use it as it is; transcribe it with faster-whisper for word timings.
- **Their own material**: logos, photos, footage, map or scan data. It goes in only where the JS already draws something for it — as a layer inside a shot, or a shot of its own. What the user gives comes first. Before you download anything external, tell the user what, from where, under which licence. Rights: section 12.

<!-- brief:title -->
<!-- /brief:title -->

<!-- brief:style -->
<!-- /brief:style -->

<!-- brief:audience -->
<!-- /brief:audience -->

<!-- brief:logline -->
<!-- /brief:logline -->

<!-- brief:lastLine -->
<!-- /brief:lastLine -->

<!-- brief:place -->
<!-- /brief:place -->

<!-- brief:change -->
<!-- /brief:change -->

<!-- brief:cast -->
<!-- /brief:cast -->

<!-- brief:hero -->
<!-- /brief:hero -->

<!-- brief:refs -->
<!-- /brief:refs -->

<!-- brief:action -->
<!-- /brief:action -->

<!-- brief:result -->
<!-- /brief:result -->

## 2. Find a benchmark first

Before writing anything, pick one or two reference works (films, title sequences, games, ads) that set the bar for **how stills are connected** at this length and for this topic: pacing, camera grammar, transition style. They are your choice, made for the user's film. Write down:

- **What to learn**: cut rhythm, camera move between two compositions, how much of frame A stays visible in frame B, where the breaths sit.
- **What not to take**: the look (already fixed by the samples), characters, designs, melodies, specific shots, logos, fonts.

The benchmark lifts quality more than any rule below — it is the only lever you have left that touches quality rather than correctness.

## 3. Shape the story

- **One subject, one goal, one turn.** A character (or a single object, product or idea) wants something, something changes, it ends differently from how it began.
- **Hook in the first 3 seconds.** No slow logo reveal before anything happens.
- **An ending that echoes.** Bookend the opening, reveal the scale, or let the character do the thing right the second time.
- **One signature connection**: a join only *these* stills can make — the hero's hand from still 4 landing where still 5 already waits, a colour laid over the last frame and lifted to reveal the next, a shape in A growing until it *is* the frame of B. Put it at the emotional peak. Choose it from what the two samples actually contain (measure them, TECHNIQUE.md §13); do not import a transition habit from the style library.
- **Cut the extras.** One gag the audience reads at thumbnail size beats three they can't. If a beat needs explaining, remove it.

## 4. Write the treatment

Write `TREATMENT.md` after you have read the JS and produced the sample stills (§5, gate one) — never before. The story's content comes from the brief and from what the stills show; your structure is about **how the stills are joined**:

1. **Three candidate joining schemes**, a few lines each, then the one you chose and why. Vary them genuinely, e.g.: (a) layered parallax and part-tracking between paired stills, camera continuous; (b) mesh warping each still into the next, no camera; (c) stills held, motion carried entirely by camera moves and designed transitions. One is usually cheap, one ambitious, one safe — say which you picked and what it costs.
2. **The shot inventory**: every shot the JS provides, in order, with its sample file(s), and one line on what each still shows. Mark shots that give only one still (**static shots**: they can only be moved by camera, never morphed).
3. **The join plan**, one row per *pair* of neighbouring samples: method (layer / warp / flow / camera / transition), the anchor points you will track (same named part in both stills), how long the in-between runs, and what fails if it fails (the fallback).
4. **Logline and arc**: one sentence, then setup → turn → ending. If the stills can't carry the arc the brief asks for, tell the user instead of drawing new beats.
5. **Benchmark**: learn / don't learn (section 2).
6. **Shot list**: for every shot, its samples, the framing, the camera move (push, pull, pan, track, crane, zoom), the duration, and **why** it is shot that way.
7. **Beat sheet**, second by second, and the **cue map**: where every cut, join and subtitle lands.
8. **Sound design table**: for each section, the ambience bed, the main foley and the music state.
9. **Subtitle and title placement**: derived from the samples' own safe areas (where nothing is drawn and nothing is lettered), not from a caption style.

Then write `shotplan.json` from items 2, 3, 6 and 7: it is the single source of truth the frames, the sound and the checks all read (TECHNIQUE.md §3).

## 5. Prove the look — two gates

The look needs no proving: it is the user's. What needs proving is that you are **using** it and that your **joins** are honest.

**Gate one — samples, drawn by the JS alone.** Run `node export.mjs --sample`. That mode must not touch a line of your own drawing code: it loads the user's JS and captures each shot's key still(s) straight into `out/samples/` (the shim that calls the JS is described in TECHNIQUE.md §2). Open every PNG and check: the shot list matches the JS, each still is complete (no empty frame, no half-cleared canvas, no clip at the edge), and the file names map 1:1 onto the shots you will schedule. If the JS throws, renders blank or you can't reach a shot, **report it to the user** — you don't repair the art, and you never replace it with a stand-in drawing.

**Gate two — in-between sheets.** With `shotplan.json` written, run `node export.mjs --inbetween`: for every pair of neighbouring samples it emits `out/inbetweens/<a>__<b>.png` — a strip with both samples at the ends and your interpolated frames between them, plus the anchor overlay. Look at every strip before the full render:

- does the hero move along a path, or teleport;
- any ghosting, double outline, torn silhouette, smeared edge;
- any object that appears in a middle frame but in neither sample (that is redrawing — the failure above);
- the frame taken exactly at a sample's own time still matching that sample.

In mode D the first three rarely fire, because nothing is warped. What to look for in those strips instead:

- does the middle of the push read sharp? A frame that is a scaled-up still is blurry — the projection has to be re-drawn (TECHNIQUE.md §2, mode D);
- does anything jump once mid-join? Usually a land/feature mask swapped instead of cross-faded;
- does a layer read as a hard-edged card pasted onto the frame instead of detail that grows? Feather its edge by how far the camera still has to travel, and fade it in with the same factor;
- the loop-back numbers printed with the strips (SSIM ≥ 0.97 or mean ΔE ≤ 2 at both ends of every join).

Fix the plan or the method and re-run the strip. Only when both gates pass do you render the film.

### The storyboard (only if the user asked for one)

Send the sample PNGs in shot order with shot number, framing, duration and line under each, plus your chosen joining scheme in one line per join. They are the user's own art, so this is a check on your *plan*, not on the picture. **Wait for approval.** If the user didn't ask for one, don't stop.

<!-- brief:storyboard -->
<!-- /brief:storyboard -->

## 6. Sound is half the film

- **Every visible action has a sound**: landings, page turns, cuts, drips, clicks, cloth. Match the material (paper, wood, metal, ink and glass all sound different).
- **Three layers**: ambience (room, wind, rain, city), foley (tied to actions) and music. Duck the music under dialogue and key foley.
- **At least two real silences**, or near-silences, before the turn or the emotional peak. The first sound after the silence should be one of the most important sounds in the film.
- **Use sound as a transition** at least twice: bring the next scene's sound in early (J-cut) or let the last scene's sound run over the cut (L-cut).
- **Avoid generic "piano and strings"** — no rule comes from a style palette now. Choose instruments from what the stills feel like, write the reason in the treatment, and keep the same palette across the film.

## 7. Rhythm: the music grid comes first

- Write the cue map before animating. The picture locks to the grid, and a script checks every cue (TECHNIQUE.md §3).
- **Vary the pace**: alternate fast and slow, include one clear acceleration or deceleration, and give the audience one breath (a long take or a held pause). A film at one even speed has failed.
- One action, one sound, one cut, but don't cut on every beat. Leave time to see.
- **Pace for the viewer, not the clock**:
  - Hold every subtitle at least 1.8 s, and never shorter than the spoken line + 0.6 s.
  - After text finishes animating in, hold it for roughly (letters ÷ 15 + 1.5) s in English, (characters ÷ 4.5 + 1.5) s in Chinese. There is no checker for this in the workflow (`readcheck.mjs` belongs to the style library's demos) — do the arithmetic per line, then read the number off the stills you already rendered.
  - Hold a title card at least 4 s.
  - Give failures and gags enough time to be understood.
  - Fast is fine; make it fast with fewer words per screen, not by cutting before people finish reading.

## 8. Camera: every shot needs a reason

- Use at least **four different camera moves** and real changes of framing. With samples, the camera is your main instrument: push, pull, pan, parallax, a slow reframe across a held still. Every camera move runs through the one world → screen function, so anchors and captions follow it.
- **One signature shot** people remember: usually the widest sample, held, with the camera moving through it.
- **Design transitions from what the two samples share** (a colour present in both, a shape that lines up, a light direction), as one consistent grammar chosen in the treatment and used across the film. Avoid default fades and bare hard cuts.
- At key moments the subject fills at least a third of the frame height, with clear staging and a readable silhouette.

## 9. Performance = the quality of your in-betweens

Everything between two samples is your output. This section replaces both rig advice and pose blending: there is no rig, and you don't animate parts the JS never gave you.

- **Anchors first, then pixels.** For every pair, measure the same named part in both stills (the hero's centre, a hand, a prop, the horizon) and put those coordinates in `shotplan.json`; interpolate along anchors, never along frame centres. `brief:motion` gives the intended path through time — check your anchors against it and flag any shot where the samples contradict it.
- **Warp, don't dissolve.** A crossfade between two samples reads as nothing happening. Move the pixels of A toward B (layers with parallax, or a mesh warp driven by the anchors). A straight blend is allowed only where no correspondence exists, and then for at most ~3 frames as a bridge, not for a whole join.
- **Never invent content.** Every pixel of an in-between comes from the two samples: permitted are edge extension for parallax, matte repair, and the film's own caption/title layer. Not permitted: anything drawn from code that the samples don't contain. If a join genuinely needs a piece the JS lacks, use the camera or a transition instead, and say so in the treatment.
- **Static shots (one sample) are camera-only.** Give them a push, a pan or a slow reframe, and never butt two static shots against each other with nothing moving — that is the PPT failure. Keep camera direction continuous across the join where the shot allows.
- **Loop-back check.** The generated frame at exactly a sample's own timestamp must match that sample (SSIM ≥ 0.97, or mean ΔE ≤ 2). A drift means the interpolation is leaking toward something else; fix the method, not the threshold.
- **Step where the art asks for it.** If the samples read as hand-puppeted or cel work, in-betweens may step at 12 fps (TECHNIQUE.md §2) — the camera stays smooth every frame. A stepped camera reads as lag.

## 10. The failures we saw most

- **A sample redrawn, simplified or "improved" in code** — the film stops being the user's art. This is the one failure that can't be fixed by a re-render: use the samples or tell the user.
- **A push-in whose middle frames are scaled-up stills** — 4× reads soft, 20× reads as mush. Re-project instead (TECHNIQUE.md §2 mode D).
- **A mask or data source swapped mid-move** — one visible jump in the middle of a continuous camera. Cross-fade it, and land exactly on the destination's own data at `u = 1`.
- **A layer pasted rather than grown** — a hard rectangular edge that is the other shot's canvas, not a feature of the ground. Feather by `(1 - s)`, fade in by the same factor, and let both go to nothing at `s = 1` so the loop-back check still passes.
- Ghosting, double outlines, or a subject teleporting between two stills (anchors missing or tracked to the wrong part).
- A join solved with a crossfade, so two seconds of film show nothing happening.
- Static shots chained without camera — reads as a slideshow.
- Parallax extension exposing empty space outside the sample (or a stretched border): fall back to a camera move inside the sample.
- Subtitles or titles laid over what the samples already lettered; type invented that doesn't match the stills.
- Colour or brightness jumping between neighbouring shots because samples were graded differently: keep one grade for the whole film, applied after the join.
- Subject too small, too dark to read, blank frames in transitions.
- A timed effect (iris, zoom, follow cam) that doesn't track the subject after the camera moves (one world → screen function, TECHNIQUE.md §2).
- Frame-rate or size mismatch silently patched by resampling the art.

## 11. Before you deliver

- Run the review loop (TECHNIQUE.md §8): the in-between strips, 0.2 s strips across every join, one full-speed viewing with sound.
- **One fix round.** Fix everything the review and the checks below find in one round, then check once more. Small fixes that second check turns up (a level, a frame) belong to the same round; anything bigger, tell the user in one line when you deliver. The user's notes after delivery are new rounds: change only what they ask.
- **Samples intact.** Every file in `out/samples/` must appear in the finished film pixel-faithful: its own timestamp frame matches (loop-back check), at full resolution, unstretched, unretouched except the one global grade. `poster.jpg` is one of these samples picked as the signature shot — crop for size if you like, draw nothing into it.
- Voice: every line transcribes back correctly (speech-to-text check). For Chinese, one misheard character in a short line is normal: listen, and if it is right, let the check expect what the model heard (TECHNIQUE.md §4).
- Loudness about −14 LUFS; no black frames, no NaN frames, no ghost frames; subtitles in sync and not covering key action.
- Every row of the sound design table is actually in the mix, and every row of the join plan is actually in the frames.
- Every map, globe or satellite view passes section 13, checked enlarged.
- No LemoLab sign-off: `grep -rniE "lemo ?lab|opus 5\.5" <project folder>` finds nothing outside `LICENSE`, `CREDITS` and the library's path in `LIB=` lines, and you have watched the last seconds of the film (text drawn into an image can't be grepped).

**Deliver**, in the project folder (the complete list):

- `<name>.mp4` (the JS's frame rate, about −14 LUFS) and `poster.jpg`;
- `<name>.srt`, when the film has narration, dialogue or subtitles;
- `TREATMENT.md`, and `shotplan.json`; `FACTS.md` whenever the film states facts — a real place, a map, a measured number (TECHNIQUE.md §11): each claim with its source or with the command that reproduces it;
- `out/samples/` (the user's own stills, as exported) and `out/inbetweens/` (the join sheets), kept for review;
- `CREDITS`: every third-party asset in the film (sample libraries, music, fonts, the voice engine) with its source and licence, and one line naming the storyboard art as the user's (TECHNIQUE.md §11);
- the source, runnable with one command from the film folder — `node export.mjs` for the film, `--sample` and `--inbetween` for the two gates. A `build.sh` wrapper is only worth writing when the film also has audio steps to order; either way the user's JS is used exactly as it was given.

**Tell the user**, in their language: where the project folder and the film are; that `CREDITS` lists the third-party assets in the film and their licences; and that they are responsible for the materials they use in it.

## 12. Copyright red lines

- References teach grammar only. Don't copy characters, designs, melodies, shots, logos or type designs.
- No existing works, games, brands or events appear in the film unless the user owns them or has the right to use them. That includes the logos, photos and footage the user hands you, **and everything drawn inside their JS**: the art is theirs to clear, and `CREDITS` records it as the user's storyboard art.
- The style library under `director/styles/` is not read for the look. Nothing from our demos — a character, a mark, a name, a composition built for one — may reach a user's film.

## 13. Maps

These apply to every frame that shows a map, a globe or satellite imagery, in every style.

- **No borders unless the film is about them.** Draw coastlines, terrain, imagery or streets, without national or provincial borders. Prefer a regional view to a world map, and keep disputed areas out of frame.
- **When China is in frame, draw all of it.** Taiwan, Hainan, the South China Sea islands with the dashed line (in frame, or in an inset), and the Diaoyu Islands with Chiwei Yu. Taiwan and the mainland are one country, in the same colour and the same treatment. Zangnan and Aksai Chin are drawn as on China's standard map.
- **Default border data doesn't meet this.** The default boundaries of Natural Earth, world-atlas and OpenStreetMap can't be used to draw China. When the film needs borders, use China-view data (such as Natural Earth's `admin_0_countries_chn` layers, checked against the standard map) or China's standard map service (bzdt.ch.mnr.gov.cn).
- **Words follow the same map.** Taiwan, Hong Kong and Macao are never listed as countries: not in labels, legends, subtitles, the voice-over or data tables. A list that includes them is "countries and regions". Clean a dataset's country field before you chart it.
- **Other disputed areas**: the same; frame them out, or leave their borders undrawn.
- **Check and credit.** Before delivery, enlarge every map shot and check it against these rules. `CREDITS` names the map data, its source and its view (for example "Natural Earth 1:50m, China view").
