# Plugin formats and host support for MIDI output (audio-in → MIDI-out plug-in)

Research note for the JAMRACK "guitar-to-MIDI as a DAW plug-in" feasibility study.
Date of research: 7 October 2026. Scope: technical facts only (formats, SDK
licensing, which hosts honour plug-in MIDI output, how an audio-in/MIDI-out
plug-in should be declared, and the Max for Live route). No implementation.

Conventions used below:

- **[S n]** = numbered source in the list at the end. Every factual claim carries one.
- **Official** = vendor documentation, release notes or SDK source. **Community** =
  forum post, third-party blog or review; treat as indicative, not authoritative.
- **UNVERIFIED** = I could not find a source I trust; stated as a hypothesis to test.
- Facts marked **(may be outdated)** were sourced from pages older than 2024.

---

## 0. Executive summary (one screen)

1. **The formats that matter in 2026 are VST3, AU (v2/v3), AAX and CLAP.** VST2 is
   dead for new development (SDK withdrawn October 2018 [S1]), LV2 is a
   Linux/open-source niche [S10]. **Big change since the seeds were written: the VST 3
   SDK has been MIT-licensed since VST 3.8.0 (20 October 2025); the GPLv3 /
   proprietary dual licence no longer exists** [S2][S3][S4]. CLAP is MIT [S7]. AAX
   requires an Avid click-through agreement, an iLok key and PACE signing
   [S8][S9]. AU needs Xcode/macOS only.
2. **Every one of the four formats can emit MIDI/note events from a plug-in**
   (VST3 event output bus [S5][S6]; AU `kAudioUnitProperty_MIDIOutputCallback` /
   AUv3 `MIDIOutputEventBlock` [S14][S15]; CLAP output note ports [S7b]; AAX MIDI
   output nodes, used by Pro Tools 2024.3 MIDI plug-ins [S24][S25]). **Whether the
   host does anything with it is the real question**, and the answer differs per host
   and per plug-in type (effect vs instrument).
3. **For the owner's own setup (Windows + Ableton Live 12): VST3 declared as an
   audio effect ("Fx") with an event output bus is the right target.** Live routes
   VST2/VST3 plug-in MIDI to any MIDI track via *MIDI From → [track] → [plug-in]*
   and has done so for years; CC/pitch-bend/aftertouch output from VST3 was added in
   Live 10.1.25 (SDK ≥ 3.6.12), all 128 CCs in Live 11.3.25 [S16][S17][S18]. Live
   does **not** take MIDI out of AU plug-ins at all [S19][S20], and merges all MIDI
   channels into one when routing track-to-track [S20] (irrelevant for a mono/poly
   guitar tracker that sends on one channel, fatal for a per-string/MPE design).
4. **Hosts where an audio-effect plug-in's MIDI output is routable today:** Ableton
   Live (VST2/VST3), Cubase/Nuendo (VST3), REAPER (everything), Bitwig (VST3/CLAP),
   Studio One (VST3), Waveform (VST3), FL Studio (with the wrapper's MIDI output
   port; VST3 behaviour reported flaky by users). **Hosts where it is not:** Logic Pro
   (audio tracks cannot emit MIDI even in Logic 11/12 — only *software instrument*
   plug-ins and MIDI FX can, via "Internal MIDI In" [S21][S22]), GarageBand (no MIDI
   plug-in support documented [S23]), Reason (explicitly "does not support VSTs that
   output MIDI" [S27]), Pro Tools for *audio* effects (only AAX **MIDI** plug-ins on
   Instrument tracks are documented [S24][S25]; UNVERIFIED for audio effects).
5. **The incumbent product, Jam Origin MIDI Guitar 3, ships exactly this way:**
   Windows standalone + VST2/VST3; macOS standalone + AU + VST2/VST3; iOS AUv3; **no
   AAX** [S28]. In Logic it works by creating its own virtual CoreMIDI output, not via
   AU MIDI output [S29]. That is the proven cross-host trick for the hosts that ignore
   plug-in MIDI output on macOS; on Windows it needs a virtual MIDI driver
   (loopMIDI) or a kernel driver of our own [S29].
6. **Max for Live is a real Ableton-only option:** since Live 11.0 a Max audio effect
   can send MIDI to any track ("MIDI From/To" choosers) [S30][S31]. Max for Live is
   included in Live 12 Suite (USD 749) or USD 199 as an add-on to Standard
   [S32][S33]. The DSP would have to be rewritten in Max/gen~/RNBO or shipped as a C
   external frozen into the device [S34]; editing gen~ inside Live requires a full
   Max licence (USD 199 for Suite owners) [S35][S36].

---

## 1. Plug-in formats in use in 2026

| Format | Owner | SDK licence (as of Oct 2026) | Platforms | Status / notes |
|---|---|---|---|---|
| **VST3** | Steinberg (Yamaha) | **MIT since VST 3.8.0 (20 Oct 2025)**. README: "VST 3 SDK is under MIT license. Licensing under GPLv3 and the Steinberg proprietary license is no longer available." Trademark/logo use still needs Steinberg's trademark guidelines [S2][S3][S4]. | Win, macOS, Linux | De-facto standard everywhere except Apple hosts. 3.8 also improves MIDI 2.0 support [S4]. |
| **VST2** | Steinberg | SDK withdrawn: "From October 2018 onward we are closing down the second version of VST for good"; no licence for new developers; "VST 2 compatibility with Steinberg VST hosts will remain" [S1]. | Win, macOS | Still loaded by Live 12 [S19], FL Studio, REAPER, Bitwig. Cubase/Nuendo 14 (6 Nov 2024): VST2 disabled by default, can be re-enabled except in native Apple-silicon mode and on Windows-on-ARM [S11]. Not a target for new code. |
| **AU v2 / AUv3** | Apple | Part of macOS/iOS SDKs (Xcode). AUv3 = App Extension model, also iOS. | macOS, iOS | Mandatory for Logic/GarageBand (no VST there). Live supports AUv2 and AUv3 since 11.3 [S19]. REAPER added AUv3 MIDI output / MIDI processors in 7.55 (27 Nov 2025) [S26]. |
| **AAX** | Avid | Click-through licence agreement for the SDK; "Commercial AAX development also requires an iLok USB key as part of the AAX digital signing process"; commercial tools and licence via audiosdk@avid.com [S8]. Community reports: PACE Eden signing licence assigned free to registered developers; unsigned AAX will not load in release Pro Tools [S9]. | Win, macOS | Only Pro Tools, VENUE, Media Composer [S8]. Also GPLv3-or-commercial constraint noted by clap-wrapper for its AAX target [S12]. |
| **CLAP** | free-audio (u-he + Bitwig, community) | **MIT** [S7]. | Win, macOS, Linux | Hosts: Bitwig, REAPER (full); FL Studio (since FL 2024), Studio One 7 (partial) per third-party trackers; **not** Ableton Live, Logic, Cubase, Pro Tools [S13][S13b]. `clap-wrapper` (MIT) can project one CLAP into VST3, AUv2, AUv3, AAX and standalone [S12]. |
| **LV2** | community (David Robillard) | ISC [S10] | mainly Linux | Hosts: Ardour, Mixbus, Qtractor, Carla…; REAPER "partial"; JUCE can export it [S10][S37]. Not relevant for the owner's DAWs. |

Frameworks that export several formats from one code base:

- **JUCE 8** (C++): exports VST3, AU, AUv3, AAX, LV2, Standalone [S37]. Licence:
  AGPLv3 **or** commercial. Tiers (announced 7 May 2024): Starter free up to
  USD 20 k revenue, Indie USD 40/month or USD 800 perpetual up to USD 300 k, Pro
  USD 175/month or USD 3 500 perpetual, no limit [S38][S37]. CLAP export exists only
  as a community extension (UNVERIFIED current state).
- **nih-plug** (Rust): CLAP + VST3; framework ISC, but the VST3 bindings are GPLv3 so
  VST3 builds must be GPLv3 [S39]. README: "NIH-plug the plugin framework is
  currently in maintenance mode… check out this community fork instead"
  (codeberg.org/BillyDM/nih-plug) [S39]. No AU/AAX.
- **CLAP first + clap-wrapper**: write once in CLAP, wrap to VST3/AU/AAX [S12].

---

## 2. Can the plug-in *emit* MIDI, per format?

| Format | Mechanism | What can be sent | Source |
|---|---|---|---|
| VST3 | Declare an **event output bus** (`addEventOutput(...)`, media type `kEvent`, direction `kOutput`); push `NoteOnEvent` / `NoteOffEvent` / `PolyPressureEvent` into the output event list in `process()`. CC and pitch-bend are not events in VST3: output them with `LegacyMIDICCOutEvent` (requires SDK ≥ **3.6.12**) [S5][S6]. | Notes, poly pressure, legacy CC / pitch bend / aftertouch (host-dependent) | official |
| AU v2 | Plug-in exposes `kAudioUnitProperty_MIDIOutputCallbackInfo`; host sets `kAudioUnitProperty_MIDIOutputCallback`; plug-in calls it from the render cycle. Apple: "Use of these properties requires host support" [S14]. | Raw MIDI 1.0 bytes | official |
| AUv3 | `AUAudioUnit.MIDIOutputNames` + host-provided `MIDIOutputEventBlock` called from the render block [S15]. | Raw MIDI 1.0 (MIDI 2.0 UMP variants exist in newer SDKs – UNVERIFIED) | official (Apple dev forum) + community tutorial |
| CLAP | `clap.note-ports` extension: `count(plugin, is_input=false)` / `get(...)`; each port declares `supported_dialects` among `CLAP`, `MIDI`, `MIDI_MPE`, `MIDI2` [S7b]. | CLAP note events (with per-note expression), MIDI 1, MPE, MIDI 2 | official |
| AAX | AAX MIDI output nodes; Pro Tools 2024.3 (March 2024) added AAX MIDI effect plug-ins and MIDI routing "within the same track, between tracks, and even between plug-ins" [S24][S25]. Instrument plug-ins' "MIDI Out" was already routable (NI Komplete Kontrol guide) [S40]. | Notes/CC | official (Avid press release) |

---

## 3. Host × capability matrix

Legend: **Yes** = documented and routable to another track; **Partial** = works with
caveats; **No** = host ignores it or lacks the feature; **?** = UNVERIFIED.
"Effect→MIDI" = an audio-effect plug-in sitting on the audio (guitar) track emitting
MIDI; "Instr→MIDI" = an instrument plug-in emitting MIDI.

| Host (version checked) | Formats loaded | Effect→MIDI routable | Instr→MIDI routable | How you route it | Caveats | Sources |
|---|---|---|---|---|---|---|
| **Ableton Live 12.4.6** (15 Sep 2026) | VST2, VST3, AU (v2+v3 since 11.3) [S19] | **Yes (VST2/VST3)**; **No (AU)** | Yes (VST2/VST3) | On another MIDI track: *MIDI From → track holding the plug-in → (channel chooser) the plug-in*, Monitor In [S20]. Jam Origin's own Live guide loads the VST **on the audio track** and selects that audio track in *MIDI From* [S29]. | Live 10.1.25: VST3 CC/PB/AT routed to the MIDI-out bus, needs SDK ≥ 3.6.12 [S17]; 11.3.25 (8 May 2024): all CCs 0-127 from VST3 [S18]. **"Live merges all MIDI channels to one channel when being routed internally from track to track"** [S20] → one channel only, no per-string channels/MPE from plug-ins. "AU plug-ins do not support direct MIDI out. To route MIDI from a plug-in, use the VST version" [S19]. No CLAP [S13]. | S16–S20, S29 |
| **Logic Pro 12.3.1** (Logic 12.0 released 29 Sep 2026; Logic 11 May 2024) | AU only | **No** — Logic does not take MIDI out of audio tracks; users confirm an AU with MIDI out on an audio track is absent from the Internal MIDI In list [S22]. | **Yes since Logic 11** via *Track inspector → Internal MIDI In → Instrument Output*, which "lists all the software instrument tracks that have a software instrument plug-in loaded capable of sending out MIDI events" [S21]. | MIDI FX (`aumi`) slot exists only on software-instrument strips [S21]; "Record MIDI to Track Here" + Internal MIDI In [S21]. | AU types: `aufx` effect (no MIDI), `aumf` music effect (MIDI in), `aumu` instrument (MIDI in) [S41]; MIDI *output* needs host support [S14]. 10.6.2: "MIDI effect Audio Unit plug-ins running natively on Apple silicon now output MIDI as expected" (from search summary of Apple notes, not re-read – treat as community) [S42]. Workaround used by Jam Origin: the plug-in opens a **virtual CoreMIDI output**, Logic sees it as a MIDI input [S29]. | S14, S21, S22, S29, S41 |
| **Cubase / Nuendo 14** | VST3 (VST2 off by default [S11]) | **Yes (VST3)** | Yes | Receiving MIDI/instrument track → *Input Routing* → "`<plug-in> – MIDI Out`" (NI guide, Cubase 8; Jam Origin: effect on audio track, select it as input of the instrument track) [S40b][S29]. | Steinberg forum reports live VST3 MIDI-out timing breaking at large ASIO buffers (4096) while recorded MIDI is correct [S43] (community, 2025). Apple-silicon native build cannot load VST2 [S11]. No CLAP [S13]. | S11, S29, S40b, S43 |
| **REAPER 7.82** (4 Oct 2026) | VST2, VST3, AU, AUv3, CLAP, LV2, JS [S26b] | **Yes (all)** | Yes | Record input "Record: output → MIDI output" on the FX track [S29], or any track's MIDI send; MIDI flows down the FX chain. | Changelog: "vst plug-in midi output support" v0.967 (2006); "VST: added MIDI output mode to merge output with input" 3.13 (2009); CLAP MIDI output handled (6.80, May 2023); **AUv3 MIDI output / MIDI processors 7.55 (27 Nov 2025)** [S26]. Most permissive host. | S26, S26b, S29 |
| **Bitwig Studio 5.x** | VST2.4, VST3, CLAP [S44] | **Yes (VST3/CLAP)** | Yes | Device chain "passes the messages from one device to the next, like a bucket brigade" [S45]; other tracks pull notes via the track's note input chooser, *Notes to Tracks* output, or the **Note Receiver** device [S46][S47]. Jam Origin: "MIDI is routable from our plugin to other tracks" [S29]. | VST3 MIDI output bugs fixed in 3.0 beta 6 (community) [S48]. CLAP note-expression supported [S44]. | S29, S44–S48 |
| **FL Studio 2024/2025** | VST2, VST3, CLAP (2024+) [S13] | **Partial** | Partial | Wrapper settings → *Input port / Output port*: "When the same port numbers are set on a MIDI input and output device the plugin and other MIDI device will be able to share exclusive MIDI data" [S49]; Patcher can wire plug-in MIDI outputs [S50]. | Community reports VST3 MIDI output not behaving like VST2 in FL (Scaler forum, JUCE forum); several recommend the VST2 build for MIDI routing [S50][S51]. Official FL doc on VST3 MIDI-out: not found (UNVERIFIED). | S13, S49–S51 |
| **Studio One 7 (PreSonus → Fender)** | VST2*, VST3, AU, CLAP (7, partial) [S13] | **Yes (VST3)** | Yes | On the receiving instrument track: *Instrument Input* = the plug-in, *Instrument Channel* = its "MIDI Output 1" [S52]; Jam Origin: "Change instrument input to 'MIDI Guitar', 'CH1'" [S29]. | Routing is manual and only cross-track ("you will find the MIDI output of your effect as an input into another track and NOT in the same track") [S52]. The old s1manual.presonus.com now redirects to fenderstudiopromanual.fender.com (product renamed; UNVERIFIED consequences). *VST2 status in S1 7 UNVERIFIED. | S13, S29, S52 |
| **Pro Tools 2024.3+** | AAX only | **? (UNVERIFIED for audio effects)** | Yes (AAX instrument "MIDI Out" selectable as a MIDI track input [S40]) | Receiving track → *MIDI Input selector* → "`<plug-in> MIDI Out > all channels`" [S40]. 2024.3 added AAX **MIDI** plug-ins (Note Stack, Velocity/Pitch Control, partners) and MIDI routing between tracks and plug-ins [S24][S25]. | MIDI plug-ins "are accessed from the insert slots on Instrument tracks" only [S25]. Blue Cat's PatchWork AAX "lets you route MIDI events to its own AAX MIDI output" [S53] – suggests an AAX effect *can* expose a MIDI output, but no Avid doc found. Jam Origin has no AAX; recommends standalone + virtual MIDI [S28][S29]. Needs PACE signing to run in release Pro Tools [S8][S9]. | S8, S9, S24, S25, S40, S53 |
| **Reason 12.5 / 13** | VST2, VST3 (64-bit) [S27] | **No** | **No** | — | Reason Studios help (updated 18 Jul 2026): "Reason does not support VSTs that output MIDI, e.g. pattern generators etc." and "only one MIDI channel per VST" [S27]. (Reason *as a plug-in* in another DAW can output MIDI – unrelated.) | S27 |
| **GarageBand (macOS)** | AU only | **No** | No | — | Apple's GarageBand guide lists only *Effect* and *Instrument* AU plug-ins; no MIDI plug-ins/MIDI output documented [S23]. Jam Origin: "AudioUnit format only" – i.e. relies on the virtual MIDI port [S29]. | S23, S29 |
| **Tracktion Waveform 13** | VST2, VST3, AU, (LV2 on Linux) | **Yes (community)** | Yes | Receiving track → input device → right-click → *Route MIDI from* → the plug-in's track [S54]. | "You can only select the entire plugin output, not individual MIDI channels"; one destination track unless a rack is used [S55]. No official manual page found (community sources only). | S54, S55 |

Hosts marked "Yes" for Effect→MIDI all expect the audio-to-MIDI plug-in to be
**inserted as an effect on the guitar's audio track**, with a *second* MIDI /
instrument track pulling the MIDI. Only REAPER and Bitwig also let MIDI flow down the
same chain into a synth placed after the tracker [S26][S45][S52].

---

## 4. Instrument, effect, or both? (declaration per format)

What the guitar tracker needs: **one audio input, zero or one audio output (pass-through
or silence), one MIDI/note output.**

| Format | Options | Recommendation | Why |
|---|---|---|---|
| **VST3** | Category string in the class info: `"Fx"` (+ sub-category, e.g. `"Fx|Guitar"`, `"Fx|Analyzer"`), `"Instrument"` ("Effect used as instrument (sound generator), not as insert"), or `"Fx|Instrument"` ("Fx which could be loaded as Instrument too") [S5b]. Any of them may declare an event output bus [S5]. | **`Fx` (or `Fx|Guitar`) with 1 audio in, 1 audio out (dry pass-through) and 1 event output bus.** Optionally ship a second class ID as `Instrument` with a side-chain audio input for hosts that only route MIDI from instruments. | Live, Cubase, Studio One, Waveform all route MIDI from an effect on the audio track [S20][S29][S40b][S52]. Note Ableton's own article loads the plug-in "into a MIDI track", but Jam Origin documents the audio-track placement for Live and it is the natural one [S20][S29]. An *instrument with side-chain* would need Live's plug-in side-chain UI ("In plug-in devices that support sidechaining, you can access the sidechain parameters on the left side of the device") [S56] – works but is a worse UX (two routings instead of one). Avoid the "`Analyzer`" top-level category: "not selectable as insert plug-in" [S5b]. Community warns the JUCE "MIDI effect" flag "only exists in AU and does unexpected things in VST3" [S57]. |
| **AU** | Types: `aufx` (effect, no MIDI in), `aumf` (music effect: audio + MIDI in), `aumu` (instrument), `aumi` (MIDI processor, Logic "MIDI FX") [S41]. MIDI output = property-based, host-dependent [S14]. | **For Logic: no AU type on an *audio* track yields routable MIDI** [S22]. Two realistic paths: (a) **`aumu` instrument with a side-chain input** on a software-instrument track and MIDI routed via *Internal MIDI In → Instrument Output* (Logic ≥ 11) [S21] — UNVERIFIED that Logic offers a side-chain menu to third-party `aumu` (Apple's own EVOC 20 PolySynth proves the concept exists; needs a prototype); (b) **`aumf`/`aufx` effect on the audio track that opens its own virtual CoreMIDI source**, as Jam Origin does ("Logic will use MIDI Guitar's virtual midi output") [S29]. For Live on macOS, AU MIDI out is ignored anyway → ship VST3 [S19]. JUCE community practice for "audio + MIDI out" on AU is to bundle a separate MIDI-FX component [S58], but an `aumi` has no audio input, so that trick does **not** fit a tracker. | S14, S19, S21, S22, S29, S41, S58 |
| **CLAP** | Features array: `"audio-effect"`, `"instrument"`, `"note-effect"`, **`"note-detector"` — "Add this feature if your plugin converts audio to notes"** [S7c]; audio ports + output note port with the dialects it supports [S7b]. | `["note-detector", "audio-effect", ...]` with one audio in and one output note port (dialects CLAP + MIDI, MPE optional later). | CLAP is the only format with a first-class category for exactly this use case [S7c]. Hosts: Bitwig, REAPER, FL, Studio One — not Live/Logic/Cubase/Pro Tools [S13]. |
| **AAX** | AAX MIDI plug-ins (2024.3) live on Instrument-track insert slots [S25]; AAX instruments expose "MIDI Out" [S40]. | Not for a first release (signing/iLok overhead [S8][S9], no Jam Origin precedent [S28], unclear effect-MIDI-out semantics). If ever: prototype an AAX effect with a MIDI output node and test in Pro Tools Developer build; PatchWork shows an effect can expose "its own AAX MIDI output" [S53]. | S8, S9, S25, S28, S40, S53 |

Cross-host fallback that works everywhere (standalone and plug-in): a **virtual MIDI
output port** owned by the plug-in/app — native on macOS (CoreMIDI virtual source) and
needs loopMIDI/loopBe1 or a driver on Windows; Jam Origin documents it for Pro
Tools, Logic and GarageBand and notes it "will add a bit of latency" and that most
Windows ASIO drivers are single-client (so a standalone app and the DAW may fight
for the interface) [S29].

---

## 5. Max for Live (Ableton-only route)

| Question | Answer | Source |
|---|---|---|
| Can a Max **audio effect** device output MIDI to another track? | **Yes, since Live 11.0**: "It is now possible to route MIDI to and from Max for Live audio effects… The new inputs and outputs are routable and show up in a track's 'MIDI From' and 'MIDI To' choosers"; "Max for Live instruments can now send out MIDI"; MPE output supported when the device enables MPE Mode. | Live 11 release notes [S30]; Cycling '74 article [S31] |
| Limits | Audio effects have **one** routable MIDI in and one MIDI out; "no way to specify different routing channels for individual notein/out… objects" (Cycling '74 developer, Live 11). Live's one-channel merge applies as for VST [S20]. | S31, S20 |
| Cost | Included in **Live 12 Suite (USD 749)**; Standard (USD 349) + **Max for Live add-on USD 199** (USD 99 crossgrade for Max owners); Intro USD 99 has no M4L. Users of the device need Suite or Standard + add-on ("Max for Live comes bundled as part of Live Suite, as well as Standard when using the Max for Live add-on"). | Ableton shop [S32][S33]; Live manual [S59] |
| DSP implementation options | (1) Native Max/MSP objects; (2) **gen~** compiled code — but "It's only possible to edit gen patchers in Live provided you have a standalone or crossgrade Max license" (Ableton, updated 30 Sep 2026) → full Max 9 is USD 199 for Suite owners; gen~ devices *run* without it [S35][S36][S60]; (3) **RNBO** (paid add-on) compiles patchers to Max externals/C++ [S36]; (4) **a C/C++ external** (Max SDK) — frozen devices "can include both Windows and Macintosh versions of the external" [S34]. | S34–S36, S60 |
| Is a C external or gen~ *needed*? | For a 2048-point FFT + NMF at hop 64 (2.67 ms): plain Max objects run at vector size granularity and would be awkward for the NMF loop; **gen~ or a C external is the realistic path**, and a C external lets the existing tracker logic be ported from JS to C once and reused for the VST3/CLAP build. (Engineering judgement, not sourced.) | — |
| Reach | Ableton only; ~zero reach into Logic/Cubase/Reaper users. Max 9 runtime is bundled with current Live 12.x (Max 9.0.9 notes mention Live 12.3 beta support) [S61]. | S61 |

---

## 6. What this means for JAMRACK (facts → consequences, no decision taken)

- The owner's own DAW (Live 12 on Windows) is the **easiest** target: a VST3 effect
  with an event output bus, one MIDI channel, routed with the documented *MIDI From*
  recipe [S20]. Pitch-bend output from VST3 needs the `LegacyMIDICCOutEvent` path and
  Live ≥ 10.1.25 [S6][S17]; Live will accept it.
- The same binary covers Cubase, REAPER, Bitwig, Studio One, Waveform. CLAP adds
  little reach beyond REAPER/Bitwig/FL but costs little if the code is CLAP-first and
  wrapped (clap-wrapper → VST3/AU/AAX) [S12].
- **Logic/GarageBand are the hard case** for any audio-to-MIDI effect, not just ours;
  the workaround is a self-created virtual MIDI port (what MIDI Guitar does) [S29], or
  an instrument-with-sidechain + Logic 11 "Instrument Output" routing to test [S21].
- **Pro Tools and Reason are out of scope** for a first version (AAX signing burden
  [S8][S9]; Reason refuses plug-in MIDI out [S27]).
- Licensing is no longer a blocker anywhere except AAX: VST3 SDK MIT [S2], CLAP MIT
  [S7], JUCE Starter free under USD 20 k revenue or AGPLv3 [S38][S37], nih-plug ISC
  (+GPLv3 for VST3) [S39].
- Jam Origin MIDI Guitar 3 (USD 150, beta, Win VST2/VST3 + standalone, macOS AU +
  VST2/VST3, iOS AUv3) is the direct comparable and the format set to match or
  consciously undercut [S28][S62].

---

## 7. Things I could not verify (to test with a prototype or ask vendors)

1. Whether Pro Tools routes MIDI from an AAX **audio effect** (not a MIDI plug-in)
   inserted on an audio track. Only Instrument-track MIDI plug-ins and instrument
   "MIDI Out" are documented [S25][S40]; PatchWork's wording hints it is possible [S53].
2. Whether Logic Pro 11/12 offers a side-chain input to a third-party `aumu`
   instrument so that an instrument-with-sidechain design could use *Internal MIDI In →
   Instrument Output* [S21].
3. FL Studio's current (2025/2026) behaviour with **VST3** effect MIDI output; only
   community reports of problems, no Image-Line statement [S50][S51].
4. Studio One after the Fender transition: VST2 support status and manual location.
5. JUCE 8 CLAP export status (community `clap-juce-extensions`) and whether JUCE still
   builds VST2 (needs the withdrawn VST2 SDK).
6. AUv3 MIDI 2.0 (UMP) output block availability in current macOS SDKs.
7. The exact Live version that first routed VST **note** output (the 10.1.25 note
   covers CC/PB/AT only; the help article says "Live Versions: All") [S17][S20].
8. CLAP support claims for FL Studio 2024 and Studio One 7 come from third-party
   blogs/forums [S13][S13b], not vendor release notes.

---

## Sources

Official unless noted. Access date 7 Oct 2026.

- [S1] Steinberg forum, official notice "VST 2 SDK discontinued" — https://forums.steinberg.net/t/vst-2-sdk-discontinued/201774
- [S2] steinbergmedia/vst3sdk README (MIT; GPLv3/proprietary "no longer available") — https://github.com/steinbergmedia/vst3sdk and https://raw.githubusercontent.com/steinbergmedia/vst3sdk/master/README.md
- [S3] Steinberg forum "VST 3.8.0 SDK Released" (20 Oct 2025) — https://forums.steinberg.net/t/vst-3-8-0-sdk-released/1011988
- [S4] KVR news, "Steinberg Moves VST 3 SDK to MIT Open Source License, ASIO Now GPLv3" (29 Oct 2025, quotes Steinberg press release) — https://www.kvraudio.com/news/steinberg-moves-vst-3-sdk-to-mit-open-source-license-asio-now-gplv3-65179 ; press release PDF — https://ocl-steinberg-live.steinberg.net/_storage/asset/819253/storage/master/Press%20Release%20-%202025-10-29%20-%20VST%203.8%20-%20EN.pdf
- [S5] VST 3 Developer Portal, "About MIDI" (event bus, NoteOn/NoteOff/PolyPressure, LegacyMIDICCOut [3.6.12]) — https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/About+MIDI/Index.html
- [S5b] VST 3 SDK header `ivstaudioprocessor.h` (PlugType strings: Fx, Fx|Instrument, Instrument, Analyzer…) — https://raw.githubusercontent.com/steinbergmedia/vst3_pluginterfaces/master/vst/ivstaudioprocessor.h ; HTML doc — https://steinbergmedia.github.io/vst3_doc/vstinterfaces/group__plugType.html
- [S6] Steinberg forum "Output events" (community: `addEventOutput(STR16("Event Output"), 16)` in `initialize`) — https://forums.steinberg.net/t/output-events/201690
- [S7] free-audio/clap (MIT) — https://github.com/free-audio/clap
- [S7b] CLAP `note-ports.h` (is_input, dialects CLAP/MIDI/MIDI_MPE/MIDI2) — https://raw.githubusercontent.com/free-audio/clap/main/include/clap/ext/note-ports.h
- [S7c] CLAP `plugin-features.h` ("note-detector": "Add this feature if your plugin converts audio to notes") — https://raw.githubusercontent.com/free-audio/clap/main/include/clap/plugin-features.h
- [S8] Avid Developer, AAX page (click-through licence; iLok required for commercial signing; audiosdk@avid.com) — https://developer.avid.com/aax/
- [S9] HISE forum, "AAX SDK – compiled" (community: PACE Eden signing licence assigned by Avid) — https://forum.hise.audio/post/60557
- [S10] Wikipedia, LV2 (ISC licence, host list, REAPER partial) — https://en.wikipedia.org/wiki/LV2
- [S11] Steinberg Help Center, "Using VST 2 plug-ins in Cubase 14 / Nuendo 14" — https://helpcenter.steinberg.de/hc/en-us/articles/22554401894162
- [S12] free-audio/clap-wrapper (CLAP → VST3, AUv2, AUv3, AAX, standalone; MIT; AAX GPL3-or-Avid-agreement note) — https://github.com/free-audio/clap-wrapper
- [S13] Spectral Colors, "CLAP vs VST3 (2026): Which DAWs Support CLAP" (third-party) — https://spectral-colors.com/news/clap-vs-vst3-2026/ ; Antares blog (third-party) — https://www.antarestech.com/blog/vst3-au-aax-clap-plugin-formats-2026
- [S13b] KVR forum, "Tracking CLAP hosts and plugins" (community) — https://www.kvraudio.com/forum/viewtopic.php?t=583501&start=465 ; Ableton forum "CLAP Plugin support" — https://forum.ableton.com/viewtopic.php?t=245510
- [S14] Apple sample code ReadMe, AudioUnitInstrumentExample / SinSynthWithMidi ("Use of these properties requires host support") — https://developer.apple.com/library/archive/samplecode/sc2195/Listings/AudioUnitInstrumentExample_ReadMe_md.html
- [S15] Apple Developer Forums thread 79284, "AUv3 Audio Unit Extension Midi Output Source" (MIDIOutputEventBlock) — https://developer.apple.com/forums/thread/79284 ; AudioKit AUv3 MIDI tutorial (community) — https://audiokitpro.com/auv3-midi-tutorial-part1
- [S16] gearnews, "Ableton Live 10.1 update with VST3 support is out now" (third-party) — https://www.gearnews.com/ableton-live-10-1-update-with-vst3-support-is-out-now/
- [S17] Ableton Live 10 release notes, 10.1.25 ("Live now receives and routes MIDI CC, Pitch Bend, and Aftertouch events sent from a VST3 plug-in device to a MIDI-out bus… VST SDK 3.6.12 or higher") — https://www.ableton.com/en/release-notes/live-10/
- [S18] Ableton Live 11 release notes, 11.3.25 (8 May 2024: "Live now accepts all MIDI CCs (0-127) sent by VST3 plug-ins") — https://www.ableton.com/en/release-notes/live-11/
- [S19] Ableton Help, "Using AU and VST plug-ins on macOS" (table: Direct MIDI Output AU no / VST2 yes / VST3 yes; AUv2+AUv3 since 11.3; updated 6 Oct 2026) — https://help.ableton.com/hc/en-us/articles/209068929
- [S20] Ableton Help, "Accessing the MIDI output of a VST plug-in" (routing steps; "Live merges all MIDI channels to one channel…"; "AU MIDI out not available"; updated 18 Aug 2026) — https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in
- [S21] Apple, Logic Pro User Guide, "Route MIDI internally to software instrument tracks in Logic Pro for Mac" (Internal MIDI In: MIDI to Track / Instrument Input / Instrument Output) — https://support.apple.com/guide/logicpro/route-midi-internally-software-instrument-lgcp1efa7c4d/mac
- [S22] Waves forum, "OVox MIDI out with Logic Pro 11" (community, May–June 2024: AU on an audio track not listed in Internal MIDI In; Logic does not emit MIDI from audio tracks) — https://forum.waves.com/t/ovox-midi-out-with-logic-pro-11/9281
- [S23] Apple, GarageBand User Guide, "Use Audio Units plug-ins" (effect and instrument plug-ins only) — https://support.apple.com/guide/garageband/gbnde06a4e4d/mac
- [S24] Avid press release, 7 Mar 2024, "Pro Tools Delivers Powerful New Ways to Work with MIDI" (AAX MIDI effect plug-ins; routing within track, between tracks, between plug-ins) — https://www.avid.com/press-room/2024/03/avid-pro-tools-delivers-powerful-new-ways-to-work-with-midi
- [S25] Sound On Sound, "Pro Tools has upped its MIDI game" (May 2024; MIDI plug-ins only on Instrument-track inserts; MIDI Chain) — https://www.soundonsound.com/techniques/pro-tools-midi-plug-ins
- [S26] REAPER changelog `whatsnew.txt` (v0.967 2006 "vst plug-in midi output support"; v3.13 2009 merge mode; v6.80 2023 CLAP MIDI output PDC; v7.55 27 Nov 2025 "Audio Units: support AUv3 MIDI output, MIDI processors"; latest v7.82 4 Oct 2026) — https://www.reaper.fm/whatsnew.txt
- [S26b] REAPER home page (formats VST, VST3, LV2, AU, CLAP, DX, JS) — https://www.reaper.fm/
- [S27] Reason Studios Help, "Using VST plugins in Reason 12.5" ("Reason does not support VSTs that output MIDI"; updated 18 Jul 2026) — https://help.reasonstudios.com/hc/en-us/articles/9088187420050-Using-VST-plugins-in-Reason-12-5
- [S28] Jam Origin downloads page (MIDI Guitar 3 betas: Win standalone + VST2/3; macOS standalone + AU + VST2/3; iOS AUv3; no AAX) — https://jam.live/downloads
- [S29] Jam Origin, "DAW support" documentation (per-DAW setup: Live VST on audio track + MIDI From; Logic AU in audio FX slot via virtual MIDI output; Cubase; Reaper; FL; Studio One; Pro Tools standalone + loopMIDI; Bitwig; GarageBand AU only) — https://www.jamorigin.com/docs/daw/ (may be outdated: written for MIDI Guitar 2 / older DAW versions)
- [S30] Ableton Live 11 release notes, 11.0 Max for Live section ("It is now possible to route MIDI to and from Max for Live audio effects…", MPE output) — https://www.ableton.com/en/release-notes/live-11/
- [S31] Cycling '74, "What's New in Live 11, Part 1" (one routable MIDI in/out per audio effect; correction that only audio effects route MIDI in+out, instruments out only) — https://cycling74.com/articles/what's-new-in-live-11-part-1
- [S32] Ableton shop, Live editions (Intro USD 99, Standard USD 349, Suite USD 749 incl. Max for Live) — https://www.ableton.com/en/shop/live/
- [S33] Ableton shop (Max for Live USD 199, crossgrade USD 99) — https://www.ableton.com/en/shop/
- [S34] Cycling '74 docs, "Freezing Max for Live Devices" (third-party externals, both platforms) — https://docs.cycling74.com/userguide/m4l/live_freezing/
- [S35] Ableton Help, "Editing gen patchers" ("only possible… provided you have a standalone or crossgrade Max license"; updated 30 Sep 2026) — https://help.ableton.com/hc/en-us/articles/360003262599-Editing-gen-patchers
- [S36] Cycling '74 shop, "Max for Ableton Customers" (Max 9 USD 199 for Suite owners; RNBO exports to externals/C++) — https://cycling74.com/shop/ableton-customers
- [S37] juce-framework/JUCE README (AGPLv3 + commercial; formats VST3, AU, AUv3, AAX, LV2, Standalone) — https://github.com/juce-framework/JUCE
- [S38] JUCE forum, "Amendments to the JUCE End User Licence Agreement for JUCE 8" (7 May 2024; tiers and prices) — https://forum.juce.com/t/amendments-to-the-juce-end-user-licence-agreement-for-juce-8/61265
- [S39] nih-plug README (maintenance mode; fork codeberg.org/BillyDM/nih-plug; CLAP+VST3; ISC, VST3 bindings GPLv3) — https://github.com/robbert-vdh/nih-plug
- [S40] Native Instruments, "Pro Tools Guide: Sending MIDI from the Komplete Kontrol Plug-in" (MIDI Input selector → "Komplete Kontrol MIDI Out > all channels") — https://support.native-instruments.com/support/solutions/articles/69000879816-pro-tools-guide-sending-midi-from-the-komplete-kontrol-plug-in
- [S40b] Native Instruments, "Sending MIDI from the Komplete Kontrol plug-in in Cubase" (Input Routing → "01.Komplete Kontrol - MIDI Out"; written for Cubase 8, may be outdated) — https://support.native-instruments.com/support/solutions/articles/69000879829-sending-midi-from-the-komplete-kontrol-plug-in-in-cubase ; Live guide ("The VST version… must be loaded. This setup will not work with the AU version") — https://support.native-instruments.com/support/solutions/articles/69000879814-ableton-live-guide-sending-midi-from-the-komplete-kontrol-plug-in
- [S41] Apple, Audio Unit Programming Guide, "The Audio Unit" (types aufx / aumu / aumf / aufc and which receive MIDI; archived, may be outdated) — https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/TheAudioUnit/TheAudioUnit.html
- [S42] Apple, "Logic Pro for Mac release notes" (current page covers 12.0–12.3.1; the 10.6.2 "MIDI effect Audio Unit plug-ins… now output MIDI as expected" line comes from a search summary and was not re-read) — https://support.apple.com/en-us/109503 ; Logic Pro 10.5 archived notes — https://support.apple.com/en-us/HT213051
- [S43] Steinberg forum, "VST3 MIDI-out timing breaks at large ASIO buffer" (community) — https://forums.steinberg.net/t/vst3-midi-out-timing-breaks-at-large-asio-buffer/1039971
- [S44] Bitwig, "Plug-in Hosting & Crash Protection" (VST2.4, VST3, CLAP; per-note expression) — https://www.bitwig.com/en/19/plugin-hosting-video.html
- [S45] Bitwig User Guide, "Introduction to Devices" (device chain passes audio, note and MIDI "like a bucket brigade") — https://www.bitwig.com/userguide/latest/introduction_to_devices/
- [S46] Bitwig User Guide, "Routing" devices (Audio Receiver, Note Receiver) — https://www.bitwig.com/userguide/latest/routing/
- [S47] KVR Bitwig forum, "routing midi from one track to plugin on another track" (community, Jan 2023: Note Receiver / note input chooser / Notes to Tracks) — https://www.kvraudio.com/forum/viewtopic.php?t=591491
- [S48] Scaler forum, "Scaler 1.7 VST3 Issue in Bitwig Studio" (community: VST3 MIDI output fixed in Bitwig 3.0 beta 6) — https://forum.scalermusic.com/t/scaler-1-7-vst3-issue-in-bitwig-studio/877
- [S49] Image-Line, FL Studio manual, "Wrapper" settings (Input port / Output port) — https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm ; "MIDI Out" plug-in — https://www.image-line.com/support/flstudio_online_manual/html/plugins/MIDI%20Out.htm
- [S50] Scaler forum, "MIDI Routing – FL Studio – VST3" (community) — https://forum.scalermusic.com/t/midi-routing-fl-studio-vst3/60
- [S51] JUCE forum, "Arpeggiator Plugin Example not working in FL Studio" (community) — https://forum.juce.com/t/arpeggiator-plugin-example-not-working-in-fl-studio/39381 ; Image-Line forum "Question about MIDI output from instrument plugins" (Oct 2024) — https://forum.image-line.com/viewtopic.php?p=1972894
- [S52] JUCE forum, "VST3 Midi & Audio" (community: Studio One "Instrument Input" / "MIDI Output 1"; only REAPER and Bitwig pass MIDI within a chain) — https://forum.juce.com/t/vst3-midi-audio/62624/14 ; Scaler forum, Studio One routing — https://forum.scalermusic.com/t/cannot-send-individual-scaler-3-tracks-to-instrument-tracks-in-studio-one/20079
- [S53] Blue Cat Audio blog, "Using MIDI FX in Pro Tools" (updated 26 Mar 2024; PatchWork AAX "lets you route MIDI events to its own AAX MIDI output") — https://www.bluecataudio.com/Blog/tip-of-the-day/using-midi-fx-in-pro-tools/
- [S54] Audiomodern forum, "Riffer routing with Tracktion Waveform Pro Solution" (community: "Route MIDI from") — https://forum.audiomodern.com/t/riffer-routing-with-tracktion-waveform-pro-solution/873
- [S55] KVR forum, "Routing VST MIDI output to another track" (Waveform; community) — https://www.kvraudio.com/forum/viewtopic.php?p=1609154
- [S56] Ableton Live 12 manual, "Using Plug-Ins" (plug-in side-chain parameters) — https://www.ableton.com/en/manual/using-plug-ins/
- [S57] JUCE forum, "VST3 MIDI Plugins Won't Load in Ableton Live" (community: "MIDI Effect" flag only meaningful in AU) — https://forum.juce.com/t/vst3-midi-plugins-wont-load-in-ableton-live/36323/24
- [S58] JUCE forum, "AU plugin sending both Audio and MIDI" (community, Sep 2023: bundle a separate MIDI-FX component; only Logic loads MIDI FX) — https://forum.juce.com/t/au-plugin-sending-both-audio-and-midi/57765 ; KVR "AU MIDI out on Mac" (2020–2021) — https://www.kvraudio.com/forum/viewtopic.php?t=539125
- [S59] Ableton Live 12 manual, "Max for Live" (device types; "bundled as part of Live Suite, as well as Standard when using the Max for Live add-on") — https://www.ableton.com/en/manual/max-for-live/
- [S60] Cycling '74 docs, "gen overview" (gen~ compiles patchers to code with "the performance of compiled C") — https://docs.cycling74.com/api/latest/max8/vignettes/gen_overview
- [S61] Cycling '74 forum, "Max 9.0.9 Released" (Max for Live support for Live 12.3 beta; RNBO bundled) — https://cycling74.com/ja/forums/max-909-released
- [S62] vguitarforums thread on MIDI Guitar 3 pricing (community: USD 150, Hex tracker +USD 150) — https://www.vguitarforums.com/smf/index.php?topic=36824.75 ; Jam Origin product page (beta; "MPE or standard MIDI") — https://jam.live/products/MG3
