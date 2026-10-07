# Building a DAW plugin from JAMRACK's guitar-to-MIDI engine: frameworks, licences, build and distribution

Research note, 7 October 2026. Topic: how to turn the existing dependency-free
JavaScript DSP (`js/audio/guitar/tracker.js`, 550 lines; `js/audio/guitar/poly/*.js`,
about 1,150 lines of engine plus 290 lines of calibration assistant; typed arrays,
no DOM) into a plugin that Ableton Live and other DAWs can load, and what each
path costs. Every factual claim carries a source link; anything I could not
verify is flagged **[unverified]**. Facts about prices and licence tiers move
quickly; dates are given so the reader can judge staleness.

Context that constrains the choice (from the project itself, not from the web):
the plugin only needs **audio in, MIDI out**; it must run in **Ableton Live on
Windows** first; the project is **MIT**; the owner does not code and the code is
written by agents, so the framework must be well documented and well known to
LLMs; a Mac tester is not currently available.

---

## 1. What the DAW side imposes (summary of constraints relevant to the framework choice)

- **Ableton Live 12 loads VST2, VST3, AU2 and AU3 only** — no CLAP
  ([Live 12 manual, "Using Plug-Ins"](https://www.ableton.com/en/live-manual/12/using-plug-ins/)).
  CLAP support has no official Ableton statement; it is a user feature request
  with a Centercode vote (March 2024)
  ([Ableton forum thread](https://forum.ableton.com/viewtopic.php?t=245510&start=15)).
  So **VST3 is mandatory** for this project; CLAP is a bonus for Bitwig, Reaper,
  FL Studio and Studio One.
- **Live reads a plug-in's MIDI output** by loading the plug-in on a MIDI track,
  then on another MIDI track selecting that track in "MIDI From", the plug-in in
  the lower input-channel chooser, and monitor "In". The same article states
  "Live merges all MIDI channels to one channel when being routed internally
  from track to track" and "the Audio Unit (AU) plug-in standard does not
  support a direct MIDI out"; it is marked "Live Versions: All"
  ([Ableton help article](https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in)).
  Note that the article says "MIDI track": a guitar-to-MIDI plug-in on an
  *audio* track is what Jam Origin documents for its own product (see below),
  and what the seed forum posts describe; which track type works best for a
  VST3 instrument-with-audio-input in Live 12 should be tested on the owner's
  machine — it is not stated in the Ableton article.
- **VST3 note output works; pitch-bend and CC output from a VST3 are "legacy"**
  in the standard and not reliably supported by hosts. One plugin vendor's
  compatibility page lists Live 11, Cubase (since 2020), Reaper (since 2020) and
  Studio One 6 as hosts that do pass VST3 pitch-bend output
  ([feelyoursound.com, VST3 and MIDI](https://feelyoursound.com/vst3-midi/)).
  Ableton's own release notes: Live 11.3.25 (8 May 2024) "Live now accepts all
  MIDI CCs (0-127) sent by VST3 plug-ins"
  ([Live 11 release notes](https://www.ableton.com/en/release-notes/live-11/)).
  For JAMRACK this matters for the global bend in MONO mode and the per-note
  bend in POLY mode; note-on/off is the safe part.
- **The VST3 SDK itself is now MIT** (VST 3.8, 29 October 2025), which removes
  the old dual-licence friction for an open-source plugin
  ([Steinberg press release PDF](https://ocl-steinberg-live.steinberg.net/_storage/asset/819253/storage/master/Press%20Release%20-%202025-10-29%20-%20VST%203.8%20-%20EN.pdf),
  [Sonicstate](https://sonicstate.com/news/2025/10/30/vst-3-now-available-under-mit-license)).
  ASIO went GPLv3 at the same time (same sources).
- **CLAP hosts**: Bitwig (co-author of the format with u-he, 2022;
  [MusicTech](https://musictech.com/news/bitwig-u-he-clap-plug-in-standard-open-source/)),
  Reaper since v6.71, 30 November 2022
  ([KVR](https://www.kvraudio.com/news/cockos-updates-reaper-to-v6-71---clap-plugin-support-56611)),
  FL Studio 2024 (1 July 2024; [Image-Line](https://www.image-line.com/fl-studio-news/fl-studio-2024-whats-new)),
  Studio One Pro 7 ([Sound On Sound review](https://www.soundonsound.com/reviews/presonus-studio-one-pro-7)).
  Logic and Cubase: no CLAP as far as I could find **[unverified: only
  secondary blog sources, e.g. [Spectral Colors](https://spectral-colors.com/news/clap-vs-vst3-2026/)]**.
- **Prior art**: Jam Origin MIDI Guitar 3 is still a beta (Windows 3.0.68: App
  and VST2/3; macOS 3.0.74: App, AU, VST2/3; iOS 3.0.75: App, AUv3; "No stable
  release of MIDI Guitar 3 exists yet")
  ([jam.live downloads](https://jam.live/downloads/)). Their DAW page (written
  for MIDI Guitar 2) tells Ableton users to load the VST on an audio track and
  pick that track in "MIDI From"; on macOS the plug-in offers a built-in
  virtual MIDI output, on Windows a virtual MIDI cable driver (loopMIDI,
  loopBe1) is needed; an AAX version is only "considered for the future"
  ([jamorigin.com/docs/daw](https://www.jamorigin.com/docs/daw/)). So the kind
  of plugin the owner describes exists, is commercial, and routes MIDI exactly
  the way the Ableton article describes.

---

## 2. Framework comparison

| | JUCE 8 / 9 (C++) | iPlug2 (C++) | DPF / DISTRHO (C++) | nih-plug → nice-plug (Rust) | clap-wrapper (C++) | Cmajor | Embedded JS engine (QuickJS / Duktape) or JS worker thread |
|---|---|---|---|---|---|---|---|
| **Licence** | Dual: AGPLv3 **or** JUCE commercial licence; Starter tier free up to $20,000 revenue, Indie $40/month or $800 perpetual up to $300,000, Pro $175/month (12-month minimum) or $3,500 perpetual, no limit; JUCE 8 removed the Starter splash screen; JUCE 9 keeps "pricing and licensing terms identical to JUCE 8" ([JUCE LICENSE.md](https://raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md), [JUCE forum, EULA amendments, t0m 7 May 2024](https://forum.juce.com/t/amendments-to-the-juce-end-user-licence-agreement-for-juce-8/61265), [JUCE forum, reuk 19 Feb 2026](https://forum.juce.com/t/starter-plan-vs-pro-plan/68242), [JUCE 9 available, 21 July 2026](https://forum.juce.com/t/juce-9-is-available-now/69175)) | "liberal zlib-like licence", free for closed source; the WebView library inside iPlug2 is MIT ([iPlug2 README](https://github.com/iPlug2/iPlug2), [iPlug2 LICENSE.txt](https://raw.githubusercontent.com/iPlug2/iPlug2/master/LICENSE.txt)) | ISC; VST3 and LV2 and AU parts ISC, CLAP part MIT; VST2 via a clean-room header; VST3 via its own "travesty" headers, so no Steinberg SDK needed ([DPF LICENSING.md](https://github.com/DISTRHO/DPF/blob/main/LICENSING.md)) | ISC for the framework, but nih-plug's README warns "VST3 bindings fall under GPLv3, meaning VST3 plugins must comply with those terms" ([nih-plug README](https://raw.githubusercontent.com/robbert-vdh/nih-plug/master/README.md)); nice-plug: "framework and all of the example plugins are licensed under the ISC license" ([nice-plug on Codeberg](https://codeberg.org/RustAudio/nice-plug)) — whether its VST3 export is still GPL-encumbered **[unverified]** | MIT; the AAX target is "GPL3 **or** commercial Avid AAX SDK License Agreement" ([clap-wrapper README](https://github.com/free-audio/clap-wrapper)) | Dual GPLv3 (or later) / commercial; "Generated C++ code from your own Cmajor source is yours to use freely" ([cmajor.dev licence](https://cmajor.dev/docs/Licence), [LICENSE.md](https://raw.githubusercontent.com/cmajor-lang/cmajor/main/LICENSE.md)) | QuickJS MIT ([bellard.org](https://bellard.org/quickjs/)); Duktape MIT ([duktape GitHub](https://github.com/svaarala/duktape)) |
| **Formats exported** | VST, VST3, AU, AUv3, AAX, LV2, Standalone, Unity ([JUCE CMake API, FORMATS](https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/CMake%20API.md)); CLAP via the MIT `clap-juce-extensions` (JUCE 6, 7, 8; "unofficial", "forkless", CMake `clap_juce_extensions_plugin()`) ([clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions)). Official CLAP was announced for JUCE 9 ([JUCE forum roadmap, t0m 23 July 2024](https://forum.juce.com/t/juce-roadmap-updates/62275)), but the JUCE 9 release post does not mention it and the CMake API doc on `master` still lists no CLAP format **[the exact state of official CLAP in JUCE 9.0.x is unverified; KVR's JUCE 9 news lists CLAP among formats, the JUCE docs do not]** ([KVR](https://www.kvraudio.com/news/juce-9-now-available-67802)) | CLAP, VST2, VST3, AUv2, AUv3, AAX (Native), Web Audio Module (WAM v1), standalone, Reaper extensions ([iPlug2 README](https://github.com/iPlug2/iPlug2)) | LADSPA, DSSI, LV2, VST2, VST3, CLAP, JACK/standalone ([DPF README](https://raw.githubusercontent.com/DISTRHO/DPF/main/README.md)); AU appears in LICENSING.md **[AU support status unverified]** | VST3, CLAP, standalone (JACK) via `nih_export_*` macros and `cargo xtask bundle` ([nih-plug README](https://raw.githubusercontent.com/robbert-vdh/nih-plug/master/README.md), [nice-plug](https://codeberg.org/RustAudio/nice-plug)) | Takes one CLAP and projects it to VST3, AUv2, AUv3 (macOS/iOS), AAX, standalone ([clap-wrapper README](https://github.com/free-audio/clap-wrapper)) | `cmaj generate --target=juce` (then any JUCE format) and `--target=clap`, plus a JIT "loader" plugin ([cmajor.dev Getting Started](https://cmajor.dev/docs/GettingStarted)) | Not a plugin framework; would sit inside one of the others |
| **Web-technology UI (reuse of the HTML/JS card)** | Yes since JUCE 8 (12 June 2024): `WebBrowserComponent` with `Options::withNativeFunction`, `withResourceProvider` (serve the HTML/JS from the binary), `emitEventIfBrowserIsVisible` (C++ → JS events); WebKit on macOS, WebView2 on Windows with `JUCE_USE_WIN_WEBVIEW2`; `WebViewPluginDemo` example; JUCE 9.0.1 "added a new TypeScript npm package for WebView integration" ([JUCE docs](https://docs.juce.com/master/classWebBrowserComponent.html), [audioXpress on JUCE 8](https://audioxpress.com/news/juce-8-adds-important-new-features-and-enhancements-for-audio-application-and-plugin-development), [JUCE releases](https://github.com/juce-framework/JUCE/releases)) | Yes: `IPlugWebUI` example, WKWebView on macOS/iOS and Edge WebView2 on Windows ([iPlug2 WebView docs](https://www.mintlify.com/iPlug2/iPlug2/ui-frameworks/webview), [iPlug2 README](https://github.com/iPlug2/iPlug2)) | Custom C++/OpenGL UI through DGL; a web-view UI option **[unverified]** | egui, iced, VIZIA, or custom OpenGL/wgpu ([nih-plug README](https://raw.githubusercontent.com/robbert-vdh/nih-plug/master/README.md)); no first-party web view | n/a (wraps the CLAP's own GUI) | Cmajor patches carry an HTML/JS GUI, but the DSP must be rewritten in Cmajor | n/a |
| **Platforms / toolchain** | CMake ≥ 3.22, C++17; Visual Studio 2019+, Xcode 12.4+, GCC 7 / Clang 6; macOS 10.11+, Windows 10 1607+, Linux ([JUCE README](https://raw.githubusercontent.com/juce-framework/JUCE/master/README.md)) | Windows 8+, macOS 10.13+, iOS 15+, visionOS 26+, Windows ARM64EC ([iPlug2 README](https://github.com/iPlug2/iPlug2)); **Linux is not in that list [unverified]** | Makefile and CMake, Linux-first project ([DPF README](https://raw.githubusercontent.com/DISTRHO/DPF/main/README.md)) | Rust toolchain (`cargo xtask bundle`, cross-compilation supported) ([nih-plug README](https://raw.githubusercontent.com/robbert-vdh/nih-plug/master/README.md)) | CMake; `-DCLAP_WRAPPER_BUILD_AUV2=ON` needs the Apple AudioUnitSDK; AUv3 needs the Xcode generator ([clap-wrapper README](https://github.com/free-audio/clap-wrapper)) | `cmaj` CLI + a JUCE or CLAP build afterwards | Any |
| **Maintenance (Oct 2026)** | Active: JUCE 9.0.0 on 21 July 2026, 9.0.3 on 28 September 2026 ([JUCE releases](https://github.com/juce-framework/JUCE/releases)) | Active ("13,363 commits", iPlug2OOS recommended for new projects) ([iPlug2 README](https://github.com/iPlug2/iPlug2)) | Active (CI badges, commits in 2025–2026 on git.kx.studio) ([DPF mirror](https://git.kx.studio/DISTRHO/DPF)) | nih-plug "currently in maintenance mode"; BillyDM announced a maintained hard fork on 29 March 2026 ([nih-plug issue #265](https://github.com/robbert-vdh/nih-plug/issues/265)); the Codeberg URL `BillyDM/nih-plug` now serves **RustAudio/nice-plug**, last commit 14 September 2026 ([Codeberg](https://codeberg.org/BillyDM/nih-plug)) | Active; "mature" per the search summary; AAX PR opened March 2025, AUv2 MusicEffect PR August 2025 **[PR details unverified, from search summary only]** ([clap-wrapper PRs](https://github.com/free-audio/clap-wrapper/pulls)) | Active (1,068 commits, Cmajor Software Ltd) ([cmajor GitHub](https://github.com/cmajor-lang/cmajor)) | QuickJS release 4 June 2026 ([bellard.org](https://bellard.org/quickjs/)) |
| **MIDI output from the plugin** | `NEEDS_MIDI_OUTPUT TRUE` in `juce_add_plugin` ([JUCE CMake API](https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/CMake%20API.md)) | Yes (CLAP/VST3/AU MIDI out) **[detail unverified]** | `DISTRHO_PLUGIN_WANT_MIDI_OUTPUT` ([DPF macros](https://distrho.github.io/DPF/group__PluginMacros.html)) | "Full support ... outputting polyphonic note expression events, MIDI CCs, channel pressure, pitch bend, and SysEx" ([nih-plug README](https://raw.githubusercontent.com/robbert-vdh/nih-plug/master/README.md)) | Inherits the CLAP's note output; VST3 MIDI 1.0/2.0 event fixes in recent releases **[unverified detail]** | Yes (event outputs) | — |
| **Fit for this project** | Best documented, most LLM-familiar, native WebView UI, VST3 + AU + CLAP(ext). Licence needs care (see §3). | Fully permissive, native CLAP and WebView UI, smaller community; Linux unclear. | Fully permissive, no Steinberg SDK, Linux-native; UI would have to be rewritten in C++/OpenGL; AU unclear. | Permissive core, but VST3 (the format Live needs) was GPLv3-bound in nih-plug; cleanest MIT route would be nice-plug CLAP + clap-wrapper VST3; Rust tooling is unfamiliar to the owner's machine. | Useful add-on for the CLAP-first paths; not a framework by itself. | Would mean rewriting the DSP in a third language under GPLv3 or a paid licence. | Not acceptable for a shipped plugin (see §4). |

---

## 3. Licence consequences for an MIT project

- **JUCE**: the modules are "dual-licensed under the AGPLv3 and the commercial
  JUCE licence"; you pick one ([LICENSE.md](https://raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md)).
  - If the plugin is built under the **AGPLv3** option, the plugin binary that
    links JUCE is a derivative work of JUCE and must be distributed under
    AGPLv3. JAMRACK's own DSP files can stay MIT (MIT code may be combined into
    an AGPL work), but the *plugin as distributed* would not be MIT, and anyone
    forking the plugin inherits AGPL. **This is my reading of the AGPL, not
    legal advice [unverified as applied to this project].**
  - If the plugin is built under the **JUCE Starter licence** (free, up to
    $20,000 annual revenue or funding, splash screen removed in JUCE 8), the
    plugin's own sources can be published under MIT; JUCE itself stays under
    its EULA and each person who builds from source accepts the JUCE EULA for
    their own copy. JUCE staff count "all donations, advertising, streams, and
    any other indirect benefit that generates money" toward the limit
    ([JUCE forum, t0m 24 April 2024](https://forum.juce.com/t/juce8-license-and-open-source-projects/60987)).
    For a free plugin from a hobby project this is comfortably inside Starter.
  - JUCE's README contains an explicit instruction aimed at agents: "AI
    assistants and LLM-based tools generating or explaining JUCE code must read
    LICENSE.md in full and inform their users that a commercial JUCE licence
    may be required" ([JUCE README](https://raw.githubusercontent.com/juce-framework/JUCE/master/README.md)).
    Any agent workflow on this project should do exactly that.
- **iPlug2 (zlib-like + MIT WebView)**, **DPF (ISC/MIT)**, **clap-wrapper (MIT)**,
  **VST3 SDK (MIT since 3.8)**, **CLAP (MIT)**: all compatible with an MIT plugin
  with attribution only ([sources in the table](#2-framework-comparison);
  [CLAP licence on Wikipedia](https://en.wikipedia.org/wiki/CLever_Audio_Plug-in)).
- **nih-plug**: ISC core but its VST3 export was GPLv3 by its own README; a
  VST3 built with it must be GPLv3. nice-plug may have changed that after the
  VST3 SDK went MIT **[unverified]**.
- **Cmajor**: GPLv3 or commercial. Not MIT-compatible without paying.
- **AAX**: the Avid SDK is "GPL3 or commercial Avid AAX SDK License Agreement"
  ([clap-wrapper README](https://github.com/free-audio/clap-wrapper)); plus
  PACE signing. Not needed for Ableton; see §6.

---

## 4. Why not run the existing JavaScript inside the plugin?

Two variants were asked about:

**(a) Embed a small JS engine (QuickJS, Duktape) and call it from the audio
thread.** Tracktion's `choc` library already wraps both behind one API, so it
is technically a few hundred lines of glue
([JUCE forum on choc JS engines](https://forum.juce.com/t/is-there-any-future-plans-about-improving-the-javascript-engine/29541)).
It is still a bad idea for a shipped plugin:

- Both engines are interpreters, not JIT compilers. The browser runs the same
  code under V8's JIT; the POLY engine's budget is 2.67 ms per 64-sample hop
  and the Python prototype already cost 1.17 ms per hop in numpy
  (project memory, `CLAUDE.md`). An interpreter without a JIT is typically an
  order of magnitude slower than V8 on numeric loops **[the exact factor is
  unverified; no benchmark of QuickJS vs V8 on this code exists]**, which
  would very likely blow the hop budget on the NMF iterations.
- Garbage collection on the audio thread. QuickJS uses "reference counting
  (to reduce memory usage and have deterministic behavior) with cycle removal"
  ([bellard.org](https://bellard.org/quickjs/)); Duktape documents that
  "stop-and-go garbage collection is also a potential issue" in
  timing-sensitive environments and recommends disabling voluntary GC and
  calling `duk_gc()` explicitly
  ([Duktape timing-sensitive.rst](https://github.com/svaarala/duktape/blob/master/doc/timing-sensitive.rst)).
  Even reference counting frees memory on the audio thread (a call into
  `free()`), which real-time audio code avoids.
- The JS engine's heap allocations, exceptions and string handling all happen
  on the thread the DAW expects to be lock-free and allocation-free.

**(b) Run the JS on a worker thread (Node/V8 or a browser engine embedded in
the plugin) and exchange audio/MIDI through ring buffers.** This is the
architecture Elementary Audio uses: "the main thread on which all JavaScript
is executed, and the realtime thread on which all of the actual audio
processing maths takes place"
([Elementary docs](https://elementary.audio/docs)). It works for Elementary
because the JS only *describes* a graph that C++ renders; here the JS *is*
the DSP, so the worker must process every hop. Consequences: an extra
thread hop and at least one hop of extra buffering (≥ 2.67 ms) plus
scheduling jitter on Windows; a V8 or Node runtime inside every plugin
instance (tens of MB **[size unverified]**); and the plugin would still need
a native shell written in C++ or Rust. It is acceptable for a **throwaway
proof of concept** (e.g. to measure Live's routing and latency before
committing to a port), not for a release.

**The honest conclusion**: the DSP has to be ported. The good news is that the
JS was written "pure JavaScript over typed arrays" with pre-allocated buffers
(`poly/engine.js` header comment in the repository), so it ports to C++ or
Rust almost line for line, and the existing Node tests and Python
equivalence fixtures (`test/poly/`) become the port's acceptance tests.

---

## 5. Toolchain, validation and CI

- **CMake + compilers**: JUCE needs CMake ≥ 3.22, Visual Studio 2019+ on
  Windows, Xcode on macOS, GCC/Clang on Linux
  ([JUCE README](https://raw.githubusercontent.com/juce-framework/JUCE/master/README.md)).
  Visual Studio Community is free for "any individual developer" and for
  "contributing to open source projects"
  ([Microsoft](https://visualstudio.microsoft.com/vs/community/)).
  iPlug2 and clap-wrapper are also CMake; DPF offers Makefile and CMake;
  nice-plug uses `cargo xtask bundle`.
- **CI**: "The use of standard GitHub-hosted runners is free in public
  repositories", including the M1 macOS runner (free in public repos since
  30 January 2024)
  ([GitHub docs, billing](https://docs.github.com/en/actions/reference/usage-limits-billing-and-administration),
  [GitHub changelog](https://github.blog/changelog/2024-01-30-github-actions-introducing-the-new-m1-macos-runner-available-to-open-source/)).
  This is what makes macOS builds possible without a Mac on the owner's desk;
  macOS *testing* still needs a human with a Mac.
- **pluginval** (Tracktion, GPLv3 tool, which does not affect the plugin's
  licence): validates VST3/AU/VST2 at strictness levels 1–10, "5 being
  generally recognised as the lowest level for host compatibility", headless
  with exit code 0/1 for CI
  ([pluginval README](https://raw.githubusercontent.com/Tracktion/pluginval/develop/README.md)).
  Its README does not list CLAP; for CLAP use **clap-validator** (MIT,
  `clap-validator validate plugin.clap`, with fuzzing)
  ([clap-validator](https://github.com/free-audio/clap-validator)).
- **WebView2 on Windows** (needed by a JUCE or iPlug2 web UI): "the Evergreen
  WebView2 Runtime will be included as part of the Windows 11 operating
  system"; "the vast majority of Windows 10 devices have the WebView2 Runtime
  installed already"; a ~2 MB bootstrapper covers the rest
  ([Microsoft Learn, WebView2 distribution](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution)).

---

## 6. Signing and distribution per OS

| OS | What is required | Cost | Source |
|---|---|---|---|
| **Windows** | Nothing technically: a `.vst3` is a DLL loaded by the DAW. SmartScreen reputation applies to downloaded installers/executables: unsigned = "Strong SmartScreen block; enterprises may block entirely"; Azure Artifact Signing (ex Trusted Signing) ~$9.99/month but **individuals only in the USA and Canada** (organisations also EU/UK); OV certificate $150–300/year with hardware key; EV "no longer instant bypass" since 2024; **SignPath Foundation offers free code signing to open-source projects**. Whether SmartScreen is triggered at all when Live loads an unsigned `.vst3` from a zip is **[unverified]**; it is certainly triggered by a downloaded `.exe` installer. | $0 (unsigned zip) / $0 (SignPath, OSS) / $150–300 per year (OV) | [Microsoft Learn, code signing options, 29 Aug 2026](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options), [SignPath Foundation](https://signpath.org/), [Azure Artifact Signing pricing](https://azure.microsoft.com/en-us/pricing/details/trusted-signing/) |
| **macOS** | Developer ID signing + notarization, which require the paid Apple Developer Program ("Mac software notarization" is a paid-member feature); otherwise users must manually allow the plugin. | $99 per year | [Apple, compare memberships](https://developer.apple.com/support/compare-memberships/), [Apple enrollment](https://developer.apple.com/programs/enroll/), [moonbase.sh round-up 2025](https://moonbase.sh/articles/code-signing-audio-plugins-in-2025-a-round-up/) |
| **AAX (Pro Tools)** | Free Avid developer account and SDK; Pro Tools Developer build; PACE Eden tools; a physical iLok; retail Pro Tools "refuses unsigned plug-ins" (error -7054); PACE signing is free for Avid-registered developers (one memo reports 8 days from request to credentials, March 2026; an older HISE post reports 8 months). Not needed for Ableton. | $0 for Avid/PACE + an iLok dongle (~¥10,000 per the memo) + Apple $99 for macOS | [truce.audio AAX](https://truce.audio/docs/formats/aax/), [kawato memo, March 2026](https://note.com/kawato3/n/ne11473420ad5), [HISE forum](https://forum.hise.audio/topic/8190/how-i-got-my-plugin-codesigned-for-aax) |
| **Linux** | No signing. Reaper and Bitwig run natively and load VST3 and CLAP; LV2 is the native format for Ardour. | $0 | [KVR Linux plugins thread](https://www.kvraudio.com/forum/viewtopic.php?p=9261832), [REAPER on Wikipedia](https://en.wikipedia.org/wiki/REAPER) |

---

## 7. Recommendation for JAMRACK and honest effort

**Recommended path: a C++ port of the two trackers inside JUCE, built as VST3
first, with the existing HTML/JS card reused as the GUI through JUCE's
WebView, CLAP added via `clap-juce-extensions`, AU built on GitHub's macOS
runner but marked untested until a Mac tester appears. Licence: JUCE Starter
(not AGPL), plugin sources MIT, JUCE's EULA notice kept in the README.**

Why JUCE over the fully permissive alternatives:

1. The owner does not code and agents write everything. JUCE is the framework
   with by far the most documentation, forum answers and examples, including
   an official `WebViewPluginDemo`; this directly lowers the number of
   iterations the owner has to test at the guitar.
2. The WebView path means the GUI is the same HTML/CSS/JS already in
   `js/ui/rack.js`-style cards, not a second UI in C++.
3. Starter is free, has no splash screen, and the project has no revenue.
4. VST3 is required for Live, and JUCE's VST3 export with `NEEDS_MIDI_OUTPUT`
   is the most battle-tested path to "MIDI From" routing in Live.

Fallback if the JUCE licence is considered unacceptable for an MIT project:
**iPlug2** (permissive, native CLAP and WebView UI); its weaker Linux story is
irrelevant for a Windows-first owner. Second fallback for a Linux-first
future: **DPF**.

Not recommended: Cmajor (GPL or paid, third language), running the JS at
runtime (§4), AAX (Pro Tools only, iLok hardware), Rust/nice-plug (viable and
permissive via CLAP + clap-wrapper, but less familiar tooling for this owner's
Windows machine and a VST3 licence question still open).

### Effort (estimates, not measured — flagged as such)

Calendar time assumes the owner tests at the guitar between agent sessions.

| Step | Agent work | Owner work | Notes |
|---|---|---|---|
| 0. Throwaway check of Live routing and latency | ½–1 day | 1 evening | A trivial JUCE "audio in → fixed note out" VST3 to confirm "MIDI From" routing, track type, and measure input-to-MIDI latency in Live on the Gigcaster. Avoids porting for nothing. |
| 1. Port MONO tracker (550 lines) + POLY engine (~1,150 lines) to C++ with equivalence tests against the Node/Python fixtures | 3–6 days | none | Typed-array JS ports almost 1:1; the FFT, resampler and NMF are plain loops. Keep the per-hop state machine identical so the measured GuitarSet numbers (F1 81 % POLY, 74 % MONO, from `docs/polyphonic-plan.md`) transfer. |
| 2. JUCE plugin shell: audio in, note events out, parameters GAIN/SENS/DECAY/DYN/BEND/OCTAVE/MODE/PROFILE, state save/restore | 2–3 days | 2–3 evenings | VST3 note-off, bend (legacy in VST3: test in Live), MPE-style per-note bend for POLY is an open question in VST3 and should be reduced to global bend in MONO at first. |
| 3. WebView GUI reusing the card HTML/CSS/JS | 2–4 days | 1–2 evenings | `withResourceProvider` to embed the files; `withNativeFunction` for parameter changes; meters via `emitEventIfBrowserIsVisible`. |
| 4. Calibration assistant (profile capture, IndexedDB → plugin state/file) | 2–3 days | 1–2 evenings | Optional for a first release; POLY works with the generic bank. |
| 5. CI (GitHub Actions: Windows VST3/CLAP, macOS VST3/AU/CLAP, Linux VST3/CLAP), pluginval ≥ 5, clap-validator, zip releases | 1–2 days | none | Free on a public repository. |
| 6. Signing | 0–1 day | admin | Windows: apply to SignPath Foundation or ship unsigned zips; macOS: only if someone pays the $99 and can test. |

**Total: roughly 2–4 weeks of calendar time, 10–20 agent-days, for a usable
Windows VST3 beta in Ableton Live, with CLAP as a free by-product; AU and
notarized macOS builds depend on a Mac tester and $99 per year.** The
polyphonic quality limits measured in the browser (doubles ~56 %, triads
~54 %, `docs/polyphonic-plan.md` §13) do not change by moving to a plugin;
only the browser's audio buffers disappear, which is the latency gain the
project memory already predicted ("a native bridge would only gain on the
buffers", `docs/guitar-to-midi.md`).

---

## 8. Things I could not verify (consolidated)

1. Whether JUCE 9.0.x ships an official CLAP exporter (JUCE docs on `master`
   say no; KVR's news item lists CLAP). Until confirmed, plan on
   `clap-juce-extensions`, and verify its compatibility with JUCE 9 (its
   README says JUCE 6/7/8).
2. Whether nice-plug's VST3 export is still GPLv3-bound after the VST3 SDK
   went MIT.
3. Whether DPF has a web-view UI option and real AU support.
4. iPlug2 Linux support (not listed in its platform list).
5. The exact slowdown of QuickJS/Duktape versus V8 on this NMF code (no
   benchmark run).
6. Whether Windows SmartScreen reacts to an unsigned `.vst3` loaded by Live
   from an extracted zip (as opposed to a downloaded `.exe` installer).
7. Logic/Cubase CLAP status (only secondary blog sources).
8. Jam Origin's exact prices ($10–$149.95 comes from a KVR listing summary),
   and whether MIDI Guitar 3's VST3 build outputs pitch bend in Live.
9. The clap-wrapper AAX/AUv2 pull-request details (from a search summary
   only).
10. My reading of AGPLv3 as applied to a JUCE-linked plugin is not legal
    advice.

---

## 9. Source list

- Ableton: [Accessing the MIDI output of a VST plug-in](https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in); [Live 12 manual, Using Plug-Ins](https://www.ableton.com/en/live-manual/12/using-plug-ins/); [Live 11 release notes](https://www.ableton.com/en/release-notes/live-11/); [Live 12 release notes](https://www.ableton.com/en/release-notes/live-12/); [Ableton forum CLAP thread](https://forum.ableton.com/viewtopic.php?t=245510&start=15); [Ableton forum, routing VST MIDI (2009)](https://forum.ableton.com/viewtopic.php?t=123973)
- VST3: [Steinberg VST 3.8 press release](https://ocl-steinberg-live.steinberg.net/_storage/asset/819253/storage/master/Press%20Release%20-%202025-10-29%20-%20VST%203.8%20-%20EN.pdf); [Sonicstate](https://sonicstate.com/news/2025/10/30/vst-3-now-available-under-mit-license); [feelyoursound VST3 MIDI](https://feelyoursound.com/vst3-midi/)
- CLAP: [cleveraudio.org developers](https://cleveraudio.org/developers-getting-started/); [Wikipedia](https://en.wikipedia.org/wiki/CLever_Audio_Plug-in); [MusicTech Bitwig/u-he](https://musictech.com/news/bitwig-u-he-clap-plug-in-standard-open-source/); [KVR Reaper 6.71](https://www.kvraudio.com/news/cockos-updates-reaper-to-v6-71---clap-plugin-support-56611); [Image-Line FL Studio 2024](https://www.image-line.com/fl-studio-news/fl-studio-2024-whats-new); [SOS Studio One Pro 7](https://www.soundonsound.com/reviews/presonus-studio-one-pro-7); [Spectral Colors CLAP vs VST3 2026](https://spectral-colors.com/news/clap-vs-vst3-2026/)
- JUCE: [LICENSE.md](https://raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md); [README](https://raw.githubusercontent.com/juce-framework/JUCE/master/README.md); [CMake API](https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/CMake%20API.md); [releases](https://github.com/juce-framework/JUCE/releases); [EULA amendments thread](https://forum.juce.com/t/amendments-to-the-juce-end-user-licence-agreement-for-juce-8/61265); [revenue limits thread](https://forum.juce.com/t/revenue-limits-for-juce-tiers/61058); [open-source projects thread](https://forum.juce.com/t/juce8-license-and-open-source-projects/60987); [Starter vs Pro, Feb 2026](https://forum.juce.com/t/starter-plan-vs-pro-plan/68242); [JUCE 9: no pricing or EULA changes](https://forum.juce.com/t/juce-9-no-pricing-or-eula-changes/69000); [JUCE 9 available](https://forum.juce.com/t/juce-9-is-available-now/69175); [KVR JUCE 9](https://www.kvraudio.com/news/juce-9-now-available-67802); [roadmap thread](https://forum.juce.com/t/juce-roadmap-updates/62275); [WebBrowserComponent docs](https://docs.juce.com/master/classWebBrowserComponent.html); [audioXpress JUCE 8](https://audioxpress.com/news/juce-8-adds-important-new-features-and-enhancements-for-audio-application-and-plugin-development); [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions)
- iPlug2: [README](https://github.com/iPlug2/iPlug2); [LICENSE.txt](https://raw.githubusercontent.com/iPlug2/iPlug2/master/LICENSE.txt); [WebView docs](https://www.mintlify.com/iPlug2/iPlug2/ui-frameworks/webview)
- DPF: [README](https://raw.githubusercontent.com/DISTRHO/DPF/main/README.md); [LICENSING.md](https://github.com/DISTRHO/DPF/blob/main/LICENSING.md); [plugin macros](https://distrho.github.io/DPF/group__PluginMacros.html)
- nih-plug / nice-plug: [nih-plug README](https://raw.githubusercontent.com/robbert-vdh/nih-plug/master/README.md); [issue #265](https://github.com/robbert-vdh/nih-plug/issues/265); [nice-plug on Codeberg](https://codeberg.org/RustAudio/nice-plug)
- clap-wrapper: [README](https://github.com/free-audio/clap-wrapper); [clap-validator](https://github.com/free-audio/clap-validator)
- Cmajor: [licence](https://cmajor.dev/docs/Licence); [LICENSE.md](https://raw.githubusercontent.com/cmajor-lang/cmajor/main/LICENSE.md); [Getting Started](https://cmajor.dev/docs/GettingStarted); [GitHub](https://github.com/cmajor-lang/cmajor)
- JS engines: [QuickJS](https://bellard.org/quickjs/); [Duktape timing-sensitive](https://github.com/svaarala/duktape/blob/master/doc/timing-sensitive.rst); [choc JS engines, JUCE forum](https://forum.juce.com/t/is-there-any-future-plans-about-improving-the-javascript-engine/29541); [Elementary Audio docs](https://elementary.audio/docs)
- Toolchain / CI / validation: [Visual Studio Community](https://visualstudio.microsoft.com/vs/community/); [GitHub Actions billing](https://docs.github.com/en/actions/reference/usage-limits-billing-and-administration); [M1 runner for open source](https://github.blog/changelog/2024-01-30-github-actions-introducing-the-new-m1-macos-runner-available-to-open-source/); [pluginval README](https://raw.githubusercontent.com/Tracktion/pluginval/develop/README.md); [WebView2 distribution](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution)
- Signing: [Microsoft code signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options); [Azure Artifact Signing pricing](https://azure.microsoft.com/en-us/pricing/details/trusted-signing/); [SignPath Foundation](https://signpath.org/); [Apple compare memberships](https://developer.apple.com/support/compare-memberships/); [Apple enrollment](https://developer.apple.com/programs/enroll/); [moonbase.sh 2025 round-up](https://moonbase.sh/articles/code-signing-audio-plugins-in-2025-a-round-up/); [truce.audio AAX](https://truce.audio/docs/formats/aax/); [kawato AAX memo, March 2026](https://note.com/kawato3/n/ne11473420ad5); [HISE AAX signing](https://forum.hise.audio/topic/8190/how-i-got-my-plugin-codesigned-for-aax)
- Prior art: [jam.live downloads](https://jam.live/downloads/); [Jam Origin DAW docs](https://www.jamorigin.com/docs/daw/); [KVR MIDI Guitar listing](https://www.kvraudio.com/product/midi-guitar-by-jamorigin)
