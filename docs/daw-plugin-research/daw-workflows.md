# How a guitarist would use a JAMRACK guitar-to-MIDI plug-in in each DAW

Research note for the JAMRACK "guitar-to-MIDI as a DAW plug-in" feasibility study.
Date of research: 7 October 2026. Scope: **user workflows only** (routing, recording,
monitoring, known gotchas), per DAW, with sources. Technical format/SDK questions are
in `formats-hosts.md`; existing products in `existing-products.md`; latency in
`midi-latency.md`.

Conventions:

- **[S n]** = numbered source at the end. Every factual claim carries one.
- **Official** = vendor manual, help centre, release notes. **Community** = forum
  post or third-party article; indicative, not authoritative.
- **(may be outdated)** = source older than 2024 or written for an older DAW version.
- **UNVERIFIED** = I could not reach a source I trust (the shared web-search budget
  ran out part-way through; the Ableton help centre, Avid, Gearspace, Cherry Audio,
  MacProVideo and LogicProHelp block automated readers; Ableton articles were read
  through the Zendesk JSON API instead). Stated as something to test, not as fact.
- Assumed plug-in model throughout: an **audio effect** (audio in, audio thru) that
  **emits MIDI** on one channel (mono or poly tracker), exactly how Jam Origin's
  MIDI Guitar ships for VST [S20][S21]. Where a host only honours MIDI from
  *instrument* plug-ins, that is called out.

---

## 0. One-line verdict per DAW

(a) = plug-in inside the DAW, MIDI routed by the host to another track.
(b) = standalone JAMRACK app with a virtual MIDI port, recorded like a keyboard.

| DAW | (a) Plug-in + host MIDI routing | (b) Standalone + virtual port |
|---|---|---|
| **Ableton Live 12** (Win/mac) | **Works** with VST2/VST3 ("MIDI From" → track → plug-in, Monitor In). AU: **does not work**. All channels merged to one [S1][S2]. | **Works** (IAC on Mac; loopMIDI on Windows, documented by Ableton) [S9] |
| **Logic Pro 11/12** (mac) | **Works with caveat**: only *software-instrument* plug-ins and MIDI FX feed "Internal MIDI In" (Logic 11.0+); an AU on an **audio** track cannot emit MIDI → ship a second AU variant declared as an instrument (audio via side-chain) or use (b) [S12][S13][S15] | **Works** (CoreMIDI virtual source or IAC; Jam Origin's documented route) [S20][S17][S18] |
| **Cubase 13/14** (Win/mac) | **Works with caveat**: VST3 MIDI output appears as an input of a MIDI/instrument track ("NN.Plug-in – MIDI Out"); official Steinberg page for audio-insert case not verified; VST2 off by default in 14 [S24][S25][S27] | **Works** (any MIDI input port) [S20] |
| **REAPER 7** (Win/mac/Linux) | **Works**, best in class: "Record: output (MIDI)" on the same track, or MIDI sends/receives to any track; VST, VST3, CLAP, AU/AUv3 all pass MIDI [S28][S29][S30] | **Works** [S20][S31] |
| **Bitwig Studio 5** (Win/mac/Linux) | **Works with caveat**: plug-in notes travel down the chain or via Note Receiver / "Notes to Tracks"; recording into a clip needs the receiving track's input set to the source track (community) [S33][S34][S35] | **Works** (track input "All inputs" takes every MIDI source) [S32] |
| **FL Studio** (Win/mac) | **Works with caveat**: wrapper "Output port = Input port" routing for live control; recording the routed notes into the piano roll is reported unreliable; VST3 routing problems reported (old reports) [S36][S37][S38][S39] | **Works** (loopMIDI/loopBe port = a controller; Jam Origin's documented route) [S20] |
| **Studio One 6/7** (Win/mac) | **Works** with VST3: instrument track "Instrument Input" lists the plug-in; AU lacks it [S41][S42][S43] | **Works** (add port as a Keyboard in External Devices) [S44] |
| **Pro Tools 2024.3+** (Win/mac) | **Does not work** for an *audio* AAX effect; only AAX **MIDI** plug-ins on Instrument-track inserts are documented; AAX needs Avid signing anyway [S45][S46] | **Works** (Jam Origin's documented Pro Tools route: loopMIDI / virtual out) [S20] |
| **GarageBand** (mac) | **Does not work** (AU effects and instruments only; no MIDI plug-ins documented) [S47] | **Works** (virtual MIDI; Jam Origin's documented route) [S20][S48] |
| **Any other DAW / notation / hardware** | depends | **Works** – a virtual MIDI port looks like a keyboard to everything [S9][S17][S20] |

Bottom line for the owner's own rig (Windows + BOSS Gigcaster 5 + Ableton Live 12):
a **VST3 audio effect with a MIDI output bus** is the native path, and the exact
Ableton routing is documented by Ableton itself (§1). A **standalone app with a
virtual port** is the universal fallback and the only path for Pro Tools, GarageBand
and (without a second AU variant) Logic.

---

## 1. Ableton Live 12 – detailed workflow

### 1.1 Which plug-in formats carry MIDI out in Live

Ableton's own comparison table (help centre, updated 6 Oct 2026) [S2]:

| | AU | VST2 | VST3 |
|---|---|---|---|
| Direct MIDI Output | no | yes | yes |

"AU plug-ins do not support direct MIDI out. To route MIDI from a plug-in, use the
VST version." [S2]. So on macOS the plug-in must be shipped as VST3 (or VST2) for
Live; on Windows the question does not arise.

History of VST3 MIDI output in Live (official release notes):

- Live **10.1** introduced VST3 support [S3].
- Live **10.1.25**: "Live now receives and routes MIDI CC, Pitch Bend, and
  Aftertouch events sent from a VST3 plug-in device to a MIDI-out bus." [S3]
- Live **11.3.25** (8 May 2024): "Live now accepts all MIDI CCs (0-127) sent by VST3
  plug-ins." [S4]. A JUCE developer thread confirms CCs from VST3 were lost before
  this fix and work in 11.3.25 / 12.0.10 (community) [S5].
- Live **12.0 → 12.4.6** (latest, 15 Sep 2026): I read all three chunks of the
  Live 12 release notes. **No entry adds or changes plug-in MIDI output**; the only
  VST3-MIDI entries concern *program change* sent **to** VST3 plug-ins (12.2,
  11 Jun 2025; fix in 12.3, 25 Nov 2025) [S6]. The seed claim "Live did not support
  VST3 MIDI output" is therefore **false for notes** (works since 10.1) and **was true
  for CC/bend/aftertouch before 10.1.25 and for CCs > a limited set before 11.3.25**.
  Pitch bend from a VST3 has been routed since 10.1.25 [S3]; I found no report of
  it breaking in 12.x, but **measure it** (unverified in 12.4).

Gotcha for developers (community, 2019–2021, Live 10.1.5–11): Live refuses to load a
VST3 that declares **no audio bus** ("Live doesn't like to load midi-only plugins");
the fix is to declare an audio output [S7]. A guitar tracker is audio-in/audio-thru
anyway, so this does not bite, but it rules out a "MIDI-only" build for Live.

### 1.2 Exact routing (Ableton's own help article, updated 18 Aug 2026) [S1]

Ableton's steps, verbatim structure, for "Accessing the MIDI output of a VST plug-in":

1. Load the VST plug-in into a track. (Ableton's article says "a MIDI track"; for a
   guitar tracker it is an **audio track** whose *Audio From* is the Gigcaster input —
   the Jam Origin guide and users do exactly that: "sits as a vst on an audio track
   and sends midi out" [S20][S22].)
2. Create another **MIDI track**.
3. In the Input Type chooser ("**MIDI From**") of the new track, select the track
   containing the VST plug-in.
4. In the (lower) Input Channel chooser, select **the VST plug-in** itself (it is
   listed by name, next to the "Post FX"-style entries).
5. Set the MIDI track's **Monitor to "In"** [S1]. (With *Auto* you must also **Arm**
   the track; Live's manual: Auto = "monitoring is on when the track is armed, but
   inhibited as long as the track is playing clips"; In = "permanently monitor the
   track's input, regardless of whether the track is armed or clips are playing"
   [S8].)
6. Put the sound source on the MIDI track: a Live instrument, an **Instrument Rack**
   (all chains receive the track's MIDI [S10]), or a **Drum Rack** (each pad has a
   *Receive* note chooser, "the incoming MIDI note to which the drum chain will
   respond" [S10] — so a guitar note of E2 = MIDI 40 triggers the pad on E2 unless
   you remap).
7. To **record** the MIDI clip: Arm the MIDI track and record in Session or
   Arrangement; Live's manual also notes that *Capture MIDI* works for any armed or
   input-monitored track: "Live is always listening to MIDI input on armed or
   input-monitored tracks, and Capture MIDI lets you retrieve the material you've
   just played" [S11] — useful for a guitarist who improvises first and decides later.
8. **Hardware synth** instead of a Live instrument: on the MIDI track insert
   **External Instrument**, choose the MIDI port/channel in its *MIDI To* choosers,
   the return input in *Audio From*, and set the **Hardware Latency** slider (ms or
   samples) because "external devices can introduce latency that Live cannot
   automatically detect"; the slider is disabled when Delay Compensation is off
   [S8b]. Alternatively set the MIDI track's *MIDI To* directly to the hardware port.

Monitoring the guitar itself: the audio track that hosts the tracker can be set to
Monitor *In* if you want to hear the guitar through Live, or *Off* if you listen via
the Gigcaster's direct monitoring (Ableton: "If using this option, turn off
monitoring in the recording track") [S9b]. The tracker still receives the input
either way (the plug-in sits in the track's device chain; the input reaches the
chain whenever the track is armed or monitoring is In/Auto [S8]).

### 1.3 Known gotchas in Live

1. **All MIDI channels are merged.** Official: "Live merges all MIDI channels to one
   channel when being routed internally from track to track. It is therefore not
   possible to send MIDI to separate tracks via separate MIDI channels." [S1]. The
   manual's Input Channel chooser offers individual channels only for external MIDI
   *ports* [S8]. A long-standing forum thread (2009–2011, Maschine, community) shows
   the same thing [S1b]. Consequence for JAMRACK: a **single-channel** mono/poly
   tracker is unaffected; a **per-string / MPE** design (as MIDI Guitar 3 offers,
   "MPE… channels 2–9 round-robin" [S21]) would collapse to one channel in Live via
   track routing. Whether Live preserves MPE from a *VST* plug-in output when the
   receiving track is in MPE mode is **UNVERIFIED** (Live 11.0 documents it only for
   Max for Live devices [S14]); per-note bend for the POLY engine must be tested.
2. **Recorded-MIDI timing.** Ableton's "Recordings are out of sync" article (updated
   5 Aug 2026) documents the rule for audio: with Monitor *Off* the recording keeps
   the played timing; with *Auto/In* Live "delays the start of recording by the value
   of the overall measured latency in the Set" so the clip reflects what was heard;
   Live 12 adds the **Keep Monitoring Latency in Recording** toggle (Mixer → Track
   Options) to turn that behaviour off [S9c]. The same article says explicitly that
   it is written for audio and that **Track Delay must not be used to fix sync**. For
   a *MIDI* clip generated by a plug-in on another track, a 2016 forum report says
   notes were recorded late by the chain's latency even with "Reduced Latency When
   Monitoring" (community, may be outdated) [S9d]. **To measure in Live 12**: record
   a metronome-locked riff and read the note offset; if needed the plug-in should
   report its own latency to the host and the user keeps *Keep Latency* off.
3. **Monitoring latency.** "Reduced Latency When Monitoring" (Options menu) bypasses
   device/plug-in latency on monitored tracks only; Ableton warns that recordings
   made with it active "may be out of sync" and that it is "designed for playing in
   a performance setting… not as a fix for latency issues during recording" [S9b][S9c].
   The guitar → MIDI → synth round trip inside Live is: input buffer + tracker
   detection (12–24 ms on low E, from the JAMRACK measurements in the task brief)
   + synth + output buffer. Jam Origin's guide for Live 8/9 recommended **128 or 256
   samples at 44.1 kHz** and *Latency compensation OFF* for the tracker track (may be
   outdated) [S20].
4. **AU does not output MIDI in Live** (macOS users must install the VST3) [S1][S2].
5. **Plug-in not found**: rescan with the **[ALT/OPTION]** key held at launch to start
   without plug-in scanning when a plug-in crashes Live [S2]; Jam Origin's guide also
   tells Live users to rescan with Alt if the VST does not appear (may be outdated)
   [S20].

### 1.4 Max for Live alternative (Suite or the USD 199 add-on)

Official Live 11.0 release notes: "It is now possible to route MIDI to and from Max
for Live audio effects. Max for Live instruments can now send out MIDI. The new
inputs and outputs are routable and show up in a track's 'MIDI From' and 'MIDI To'
choosers." and "Added support for MPE input to/output from Max for Live devices.
When a Max for Live device has MPE Mode enabled, MIDI output from the device will be
interpreted as MPE." [S14]. Cycling '74's article (2 Mar 2021) adds the correction
"only Audio effects can route MIDI inputs and outputs, and Instruments can only route
MIDI output", and notes that Live 12 ships a `live.routing` object to set the MIDI
destination from inside the device [S14b]. User workflow: drop the `.amxd` audio
effect on the guitar audio track; the device's MIDI output appears in other tracks'
*MIDI From* choosers exactly like a VST [S14]. The DSP would have to be rebuilt in
Max/gen~/RNBO (see `formats-hosts.md` §…), and it is Ableton-only. The MPE line above
is the one documented way to keep **per-note bend** inside Live without a virtual
port, which is relevant to the POLY engine — **to test**.

### 1.5 Standalone + virtual port in Live (fallback)

Ableton's own article "Setting up a virtual MIDI bus" (updated 6 Oct 2026): on Mac
enable the **IAC Driver** in Audio MIDI Setup ("Device is online"); on Windows
"Windows does not come with a native virtual MIDI driver" and Ableton lists
**loopMIDI** (Tobias Erichsen) and MIDI Yoke (32-bit only, unusable with Live 10+)
[S9]. The port then appears in Live's MIDI preferences; enable *Track* input and pick
it in the MIDI track's *MIDI From* [S9]. Jam Origin's note: "virtual midi is not
ideal in terms of latency" but the impact is usually unnoticeable (may be outdated)
[S20]; Sound On Sound (2014 review) found the Virtual MIDI Output "completely rock
solid, recording absolutely clean MIDI information" (may be outdated) [S23].

---

## 2. Logic Pro (macOS)

- **Since which version**: Logic Pro **11.0** (2024) added "an 'Internal MIDI in'
  setting in the Track Inspector to allow for recording MIDI from any other software
  instrument or External MIDI Instrument track" and "recording of MIDI from other
  tracks, including MIDI FX plug-in output and 3rd party MIDI generators"; 11.1.1
  added a Record pop-up to merge the normal MIDI input; 11.2: "Route MIDI signals
  generated by supported software instruments and effects to the input of other
  tracks for creative layering during playback or recording" [S12]. Logic 10.8 (the
  version before) only lists "Improved stability when MIDI 2.0 is enabled and using
  Audio Units that output MIDI" and "Audio Unit plug-ins that offer MIDI control now
  show up reliably as audio effects" [S12b]; a KVR thread describes 10.8's AU MIDI
  out as "a work in progress as it is buggy" (community) [S16]. Logic 12.0–12.4
  (latest 29 Sep 2026) add nothing on this topic [S12c].
- **How it works** (Apple user guide, current): the receiving **software instrument
  track** picks an *Internal MIDI In* source: **MIDI to Track** (output of a MIDI FX
  slot marked "Record MIDI to Track Here"), **Instrument Input**, or **Instrument
  Output** — "lists all the software instrument tracks that have a software
  instrument plug-in loaded capable of sending out MIDI events" [S13]. Every
  sending option is a **software instrument track**; audio tracks are not listed.
- **Consequence**: an AU *audio effect* on the guitar's audio track cannot be a MIDI
  source. Waves' OVox thread (May–June 2024, Logic 11): "Logic does not permit/support
  MIDI out from an audio track"; the suggested workaround is to load the plug-in as a
  **MIDI-controlled effect** (an *instrument*-type AU) and bring the guitar in through
  its **side-chain** input, "a really clumsy, impractical way of doing things in 2024"
  (community) [S15]. A JUCE thread confirms AU needs a separate "MIDI effect"/
  instrument build (`JucePlugin_IsMidiEffect`) for Logic (community) [S27].
- **User workflow (a)** if JAMRACK ships an AU *instrument* variant: Software
  Instrument track → Instrument slot = "JAMRACK Guitar→MIDI", side-chain = the
  Gigcaster input; second Software Instrument track with the synth, *Internal MIDI
  In → Instrument Output → track 1*, arm, record [S13][S15]. Not elegant, and
  side-chain only exists on tracks that are being recorded/monitored.
- **IAC fallback** (community, Logic 10.x era, may be outdated): duplicate the track,
  change its instrument to **External Instrument** with MIDI destination *IAC Driver
  Bus 1*, create a new track that receives IAC, record [S16][S16b]. Logic 11's Internal
  MIDI In can also take an *External MIDI Instrument track* as source [S12].
- **Workflow (b) – what Jam Origin actually does**: MIDI Guitar's AU "automatically
  generates a virtual MIDI port visible to your DAW" and tells users to avoid "Direct
  MIDI Output" with the AU version [S21]; their Logic guide loads the AU in the audio
  FX slot of the guitar track and lets the instrument track receive the virtual
  port, with Logic's *low latency mode* on (may be outdated) [S20]. The standalone
  app does the same via "MIDI Guitar Virtual MIDI Out" [S20]. The IAC driver is
  enabled in Audio MIDI Setup → MIDI Studio → IAC Driver → "Device is online" [S17];
  Logic also has a "Record MIDI messages from another music app" guide section
  (title seen in the Logic guide TOC, body not read) [S13].

Verdict: (a) works with caveat (instrument-type AU + side-chain, or AU with a built-in
virtual port like Jam Origin); (b) works.

---

## 3. Cubase / Nuendo

- Cubase exposes a plug-in's MIDI output as an **input** of a MIDI or instrument
  track: Native Instruments' guide selects "01.Komplete Kontrol - MIDI Out" in the
  track's *Input Routing* (prefix = track number), then enables **Monitor** and/or
  **Record Enable** (written for Cubase 8, may be outdated) [S24]. Steinberg forum
  users (2021–2022) describe the same: "create a MIDI track and set its input to the
  VST's MIDI output", then use the MIDI track's 4 MIDI sends (community) [S25].
- Jam Origin's guide: audio track with MIDI Guitar as *Effect* insert, instrument
  track whose input is "MIDI Guitar"; Cubase 9–10 on Windows needed **ASIO Guard
  off** (may be outdated) [S20].
- VST3 audio-insert specifics: a 2024 JUCE thread says Cubase (like Live and Studio
  One) wants MIDI routed track-to-track, not inside one chain (community) [S27]; a
  Steinberg forum thread on "VST3 MIDI-out timing breaks at large ASIO buffer"
  indicates VST3 audio effects' MIDI output is routable in Cubase (community, cited
  in `formats-hosts.md` [S43] there; not re-read here). **The official Steinberg
  manual page for this is UNVERIFIED** (search budget exhausted).
- **VST2 is disabled by default in Cubase/Nuendo 14** and unavailable in native
  Apple-silicon / Windows-on-ARM mode [S26] → ship VST3.
- (b): any MIDI port, including loopMIDI/IAC, is selectable as the track input [S24].

Verdict: (a) works with caveat (official doc not confirmed; VST3 required); (b) works.

---

## 4. REAPER

- Official changelog: "vst plug-in midi output support" since v0.967 (19 Jun 2006),
  "preliminary track record output (MIDI) mode" since v0.979 (5 Jul 2006), CLAP MIDI
  output since 6.x ("do not apply plugin PDC/latency to MIDI events if the plugin
  produces MIDI output", v6.80, 27 May 2023), **AUv3 MIDI output and MIDI processors
  in v7.55 (27 Nov 2025)**; latest version v7.82 (4 Oct 2026) [S28].
- **Workflow (a)**, three ways (community, 2016, still valid per Jam Origin's and
  2019 user guides): (1) on the guitar track right-click the record-arm button →
  **Record: output → Record: output (MIDI)**, so the track records the MIDI the plug-in
  emits instead of audio [S29][S20]; (2) create an instrument track and add a
  **receive** from the guitar track with *MIDI only* (optionally filtered by channel);
  record-arm it with input = the receive [S29][S31]; (3) offline: *Apply track/take
  FX to items as new take (MIDI output)* turns a recorded guitar item into MIDI
  [S29][S30]. Plug-ins can also sit **in the same chain** before a synth: "a few like
  Reaper and Bitwig do" pass MIDI between plug-ins in one chain (community, 2024)
  [S27b].
- Formats: VST, VST3, AU, CLAP, LV2, JS all load [S28].
- (b): loopMIDI port enabled in Preferences → MIDI devices; a user with a 100-track
  template preferred the standalone + loopMIDI route (community, 2019) [S31].

Verdict: (a) works (best host for this); (b) works.

---

## 5. Bitwig Studio

- Official user guide: an instrument track's input chooser lists "incoming MIDI
  sources… default *All inputs* so that every MIDI source should make it to the
  track"; audio tracks can take "the audio outputs of all other tracks"; every track
  shows "hardware Note Outputs, allowing you to route notes and other MIDI directly
  out from any track"; monitor is Off/Auto/On [S32]. The routing devices chapter
  defines **Note Receiver**: "a router that imports note signals from any designated
  project source" [S33].
- Community workflow (Jan 2023): put a **Note Receiver** before the target plug-in on
  the synth track and point it at the guitar track, or set the synth track's note
  input (inspector) to the guitar track — the latter "keeps the receiving track
  always armed" [S34]. Recording: a 2015 thread (Bitwig 1.3) found that notes
  arriving through a Note Receiver are **not recorded** into the clip, whereas setting
  the receiving track's **input to the source track** (under "Tracks") records them
  (community, may be outdated) [S35]; a 2021 thread says instrument-track inputs then
  listed only hardware "NOTE INPUTS" and recommends "Send MIDI Notes To" on the source
  track (community, conflicting, may be outdated) [S35b]. Jam Origin: Bitwig routes
  MIDI from the plug-in "to other tracks", with "a quirk with getting Bitwig to
  record MIDI onto a piano roll" (may be outdated) [S20]. Bitwig also lets a plug-in
  feed a synth **inside the same chain** (community, 2024) [S27b]. VST3 MIDI output
  from Scaler was fixed in Bitwig 3.0 beta 6 (community, cited in `formats-hosts.md`
  [S48] there, not re-read).
- (b): a loopMIDI/IAC port is just another controller in Settings → Controllers
  ("Add Controller…") [S32].

Verdict: (a) works with caveat (recording path needs the track-input method; verify
in Bitwig 5.x); (b) works.

---

## 6. FL Studio

- Official manual (Wrapper settings): "Input port / Output port – Select the MIDI
  input and output ports respectively… When the same port numbers are set on a MIDI
  input and output device the plugin and other MIDI device will be able to share
  exclusive MIDI data" [S36]. The *MIDI Out* plug-in page: "Matching port numbers on
  MIDI Out and the target creates the link… there are 256 available"; set the target
  VST's wrapper *Input Port* to the same number [S37].
- Vendor guide (codefn42, for its Chordz plug-in): source plug-in *Output port* = 1,
  target instrument *Input port* = 1, "both… must be the same, and it must be unique
  (not used by other plugins)" [S38].
- Gotchas: (i) a 2017 Image-Line forum thread reports that notes arriving through
  this plug-in-to-plug-in route **were not recorded** into the piano roll and the
  MIDI-input LED stayed dark; the moderator's answer was a **LoopBe** virtual port
  (community, may be outdated) [S39]; (ii) Scaler users (2018) found "MIDI routing
  does not work when using Scaler as a VST3 plugin in FL Studio" and used VST2
  (community, may be outdated) [S40]; a 2024 JUCE thread describes the port routing
  as working but reports pitch-wheel messages forcing a synth's bend to the bottom
  (community) [S27]; (iii) FL also refuses a plug-in with no audio bus (community)
  [S7]. Jam Origin: FL 20+ has internal routing, "note erratic buffer display and
  stability concerns"; older FL = standalone + loopMIDI (may be outdated) [S20].
- (b): loopMIDI/loopBe port enabled in MIDI settings = a normal controller [S20][S39].

Verdict: (a) works with caveat (live control yes; recording and VST3 behaviour must
be re-tested on FL 2025/2026); (b) works.

---

## 7. Studio One (PreSonus)

- Workflow (a): add an **Instrument track**, open its **Instrument Input** dropdown —
  the MIDI-emitting plug-in appears below "All Inputs" — arm, record (community)
  [S41]. Scaler users (Mar 2025, Studio One 6.6.4 and 7): works with the **VST3**
  build, which also offers a MIDI-channel selector in the input chooser; the **AU**
  build does not expose MIDI output; set the generating track's *Instrument Output*
  to *None* to avoid feedback (community) [S42]. A JUCE developer: Studio One shows
  a VST3 effect's MIDI "as an input into another track and NOT in the same track"
  (community, 2024) [S43]. Jam Origin: Professional edition needs *Dropout
  Protection* lowered (not maximum); Prime has no VST support at all, Artist needs
  the VST add-on → virtual MIDI there (may be outdated) [S20].
- (b): PreSonus' Studio One Pro 7 article (9 Oct 2024) shows the instrument track's
  MIDI IN box and that devices are declared in Options → External Devices; a loopMIDI
  port is added there as a Keyboard [S44].

Verdict: (a) works (VST3); (b) works.

---

## 8. Pro Tools (Avid)

- Pro Tools **2024.3** introduced AAX **MIDI effect plug-ins**, "accessed from the
  insert slots on Instrument tracks" only — MIDI tracks have no insert slots — and a
  **MIDI Chain** internal bus to route MIDI between tracks and record a plug-in's
  output (Sound On Sound, May 2024) [S45]. Avid's press release (7 Mar 2024) speaks of
  routing "within the same track, between tracks, and even between plug-ins" (page
  blocked for automated reading; quote from search summary and `formats-hosts.md`
  [S24] there) [S46]. Nothing I found documents MIDI output from an **audio** AAX
  effect; Blue Cat's PatchWork exposes "its own AAX MIDI output" as a workaround
  product (updated 26 Mar 2024) [S46b]. AAX itself requires Avid's SDK agreement and
  PACE signing (`formats-hosts.md`).
- Jam Origin's position: "Does not support VST or AU; requires standalone + virtual
  MIDI driver" — loopMIDI/loopBe1 on Windows, the built-in virtual output on Mac,
  then enable the port as a Pro Tools MIDI input; "AAX plugin: will consider in the
  future" (may be outdated) [S20]; MIDI Guitar 3's current downloads still list **no
  AAX** [S49].

Verdict: (a) does not work (for an audio effect; an AAX MIDI plug-in on an Instrument
track with side-chain is UNVERIFIED); (b) works.

---

## 9. GarageBand (macOS)

- Apple's guide lists only two Audio Units categories, **effect** plug-ins (audio and
  software-instrument tracks, master) and **instrument** plug-ins (software-instrument
  tracks); nothing about MIDI effects or MIDI output [S47]. Jam Origin provides only a
  video for GarageBand (AU) and, for iOS/"MIDI Guitar for GarageBand", routes "via
  Apple's Virtual MIDI standard to any synth or DAW app" [S20][S48].
- (b): GarageBand records from connected MIDI sources; a virtual port (IAC or the
  app's own CoreMIDI source) is such a source — this is Jam Origin's documented route
  [S20][S48]. Exact GarageBand input-selection behaviour: UNVERIFIED (page not read).

Verdict: (a) does not work; (b) works.

---

## 10. Universal fallback: standalone app + virtual MIDI port

What the user does: run JAMRACK standalone (audio in from the Gigcaster), choose
"MIDI out = <virtual port>", then in any DAW enable that port as a MIDI input and
arm a MIDI/instrument track exactly as for a keyboard [S9][S20].

- **macOS**: two ways. (1) The **IAC Driver** in Audio MIDI Setup → MIDI Studio →
  "Device is online" (Apple and Ableton both document it) [S17][S9]. (2) The app
  creates its **own CoreMIDI virtual source** — what MIDI Guitar does ("MIDI Guitar
  Virtual MIDI Out"; MG3's AU "automatically generates a virtual MIDI port") [S20][S21].
  No driver install for the user.
- **Windows**: "Windows does not come with a native virtual MIDI driver" (Ableton)
  [S9]. Options: **loopMIDI** (free download, v1.0.16.27, page lists Windows 7–10;
  licence terms not stated on the page) [S18]; the underlying **virtualMIDI SDK**
  lets an application "dynamically create and destroy freely nameable MIDI-ports"
  with C/C++/C#/Java bindings — the way to give JAMRACK its own port without asking
  users to install loopMIDI (licensing for redistribution not stated on the page →
  UNVERIFIED) [S19]; and **Windows MIDI Services**, Microsoft's new MIDI stack,
  "available on Windows 11 releases 25h2 and later", with "built-in virtual /
  app-to-app MIDI 2.0" endpoints [S50] — the long-term native answer, but users on
  Windows 10 / older 11 still need loopMIDI.
- **iOS/iPadOS**: Virtual MIDI is built in (Jam Origin's iOS route) [S48].
- **Cost of (b)**: an extra process and the OS MIDI stack between tracker and synth;
  Jam Origin calls it "not ideal in terms of latency" but usually unnoticeable [S20].
  `midi-latency.md` has the measurements question.
- **Who needs (b)**: Pro Tools and GarageBand always; Logic unless a second AU
  variant is shipped; FL Studio when recording misbehaves; anyone who wants to drive a
  hardware synth directly (the standalone can pick the hardware port instead of the
  virtual one, as MG3's MIDI Output module does [S21]).

---

## 11. What this means for the JAMRACK plug-in design (user-facing)

1. Ship **VST3 (Win/mac)** as an *audio effect with a MIDI output bus*: covers Live,
   Cubase, REAPER, Bitwig, Studio One, FL [S2][S24][S28][S32][S41][S36]. Keep an
   audio output bus (Live/FL refuse bus-less plug-ins) [S7].
2. Ship an **AU** only together with a **built-in virtual MIDI port** (MG3 model) or
   as a MIDI-controlled-effect/instrument variant, because Live ignores AU MIDI out
   [S2] and Logic ignores audio-track MIDI out [S13][S15].
3. Ship a **standalone** with a selectable MIDI output (virtual or hardware) [S20][S21].
4. Send on **one MIDI channel** by default; Live merges channels [S1]. Make the
   channel selectable for Cubase/Reaper multi-track setups [S31].
5. Document per DAW the four lines a guitarist needs: *where the plug-in goes, where
   the MIDI appears, what to arm, what buffer to use* — the Jam Origin DAW page [S20]
   is the model, and it is a decade old; a fresh Live 12 / Logic 11 / Cubase 14 /
   Studio One 7 / FL 2025 verification pass with the actual plug-in is the real test.

---

## 12. Not verified / left open

- Live 12.4: whether VST3 **pitch bend** output still routes (documented in 10.1.25
  [S3]; no later regression found but not measured) and whether **MPE / per-note
  bend** survives track-to-track routing from a VST (documented only for Max for
  Live [S14]).
- Live 12: exact timing offset of a MIDI clip recorded from a plug-in on another
  track (only a 2016 community report [S9d]); Ableton's Keep-Latency text covers
  audio [S9c].
- Cherry Audio "MIDI Out module not working in Live 12" forum thread (title only;
  page blocked) — could be a relevant Live 12 VST3 regression.
- Steinberg's official manual page for recording a VST3 audio insert's MIDI output
  in Cubase 14.
- Bitwig 5.x: which of the two recording methods ([S35] vs [S35b]) is current.
- FL Studio 2025/2026: whether plug-in-to-plug-in MIDI is now recordable and whether
  VST3 routing works (reports are from 2017–2018 [S39][S40]).
- Pro Tools: whether an AAX *MIDI* plug-in with a side-chain audio input can act as a
  tracker on an Instrument track.
- GarageBand: input-device selection page not read.
- Scaler 3 and Xfer Cthulhu official per-DAW routing guides: not reached (search
  budget); the Scaler forum threads [S40][S42] stand in.
- virtualMIDI SDK redistribution licence; loopMIDI licence [S18][S19].
- Logic 11.0 release date (May 2024 from memory; the release-notes page shows only
  its own publication date, 9 Apr 2026) [S12].

---

## Sources

Ableton (official unless noted)

- [S1] Ableton Help, "Accessing the MIDI output of a VST plug-in" (updated 18 Aug 2026; read via the Zendesk API) — https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in
- [S1b] Ableton Forum, "Midi out for VST plugins is always merged to channel 1" (community, 2009–2011, may be outdated) — https://forum.ableton.com/viewtopic.php?t=132446
- [S2] Ableton Help, "Using AU and VST plug-ins on macOS" (updated 6 Oct 2026; AU/VST2/VST3 table) — https://help.ableton.com/hc/en-us/articles/209068929
- [S3] Ableton, Live 10 release notes (10.1 VST3; 10.1.25 CC/Pitch Bend/Aftertouch from VST3) — https://www.ableton.com/en/release-notes/live-10/
- [S4] Ableton, Live 11 release notes (11.3.25, 8 May 2024: all CCs from VST3) — https://www.ableton.com/en/release-notes/live-11/
- [S5] JUCE forum, "MIDI CC from VST3 in Ableton Live 11" (community) — https://forum.juce.com/t/midi-cc-from-vst3-in-ableton-live-11/51107
- [S6] Ableton, Live 12 release notes (12.0–12.4.6, latest 15 Sep 2026; read in full) — https://www.ableton.com/en/release-notes/live-12/
- [S7] JUCE forum, "VST3 MIDI Plugins Won't Load in Ableton Live" (community, 2019–2021) — https://forum.juce.com/t/vst3-midi-plugins-wont-load-in-ableton-live/36323
- [S8] Ableton Reference Manual 12, ch. 17 "Routing and I/O" (17.1 Monitor In/Auto/Off, Keep Monitoring Latency; 17.3 Input Channel chooser; 17.5.2.4; 17.5.2.6 External Instrument) — https://www.ableton.com/en/live-manual/12/routing-and-i-o/
- [S8b] Ableton Reference Manual 12, "Live Instrument Reference" → External Instrument (MIDI To, Audio From, Hardware Latency) — https://www.ableton.com/en/live-manual/12/live-instrument-reference/#external-instrument
- [S9] Ableton Help, "Setting up a virtual MIDI bus" (updated 6 Oct 2026; IAC, loopMIDI, MIDI Yoke 32-bit) — https://help.ableton.com/hc/en-us/articles/209774225-Setting-up-a-virtual-MIDI-bus
- [S9b] Ableton Help, "How to reduce latency while monitoring" (updated 9 Aug 2026) — https://help.ableton.com/hc/en-us/articles/360011924559-How-to-reduce-latency-while-monitoring
- [S9c] Ableton Help, "Recordings are out of sync" (updated 5 Aug 2026; Monitor timing table, Keep Latency, Delay Compensation, Reduced Latency When Monitoring, Track Delay warning) — https://help.ableton.com/hc/en-us/articles/19450890686876-Recordings-are-out-of-sync
- [S9d] Ableton Forum, "Live recording of MIDI and delay compensation" (community, 2016, may be outdated) — https://forum.ableton.com/viewtopic.php?t=220590
- [S10] Ableton Reference Manual 12, "Instrument, Drum and Effect Racks" (Drum Rack Receive chooser; Instrument Rack chains) — https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/
- [S11] Ableton Reference Manual 12, "Recording New Clips" (Arm, auto-monitoring, Capture MIDI) — https://www.ableton.com/en/live-manual/12/recording-new-clips/
- [S14] Ableton, Live 11 release notes, 11.0 "Max for Live Improvements" (MIDI to/from M4L audio effects; MPE output) — https://www.ableton.com/en/release-notes/live-11/
- [S14b] Cycling '74, "What's New in Live 11, Part 1" (2 Mar 2021; correction on audio effects vs instruments; live.routing in Live 12) — https://cycling74.com/articles/what's-new-in-live-11-part-1
- [S22] Ableton Forum, "Midi Guitar - Jam Origin" (community, May 2014, may be outdated) — https://forum.ableton.com/viewtopic.php?t=204371
- [S22b] KVR forum, "jam origin software ?" (community, Jan–Mar 2021; Live 10 setup) — https://www.kvraudio.com/forum/viewtopic.php?t=559714

Logic / GarageBand / macOS

- [S12] Apple, "Logic Pro for Mac 11 release notes" (11.0 Internal MIDI In; 11.1.1; 11.2; page published 9 Apr 2026) — https://support.apple.com/en-kz/126835
- [S12b] Apple, "Logic Pro for Mac 10.8 release notes" (page date 13 May 2024) — https://support.apple.com/en-us/120134
- [S12c] Apple, "Logic Pro for Mac release notes" (12.0–12.4, latest 29 Sep 2026) — https://support.apple.com/en-us/109503
- [S13] Apple, Logic Pro User Guide, "Route MIDI internally to software instrument tracks in Logic Pro for Mac" (MIDI to Track / Instrument Input / Instrument Output; read via curl) — https://support.apple.com/guide/logicpro/route-midi-internally-software-instrument-lgcp1efa7c4d/mac ; companion page "Record MIDI plug-in output" (title only) — https://support.apple.com/guide/logicpro/record-midi-to-track-lgcp423beea5/mac
- [S15] Waves forum, "OVox MIDI out with Logic Pro 11" (community, May–June 2024) — https://forum.waves.com/t/ovox-midi-out-with-logic-pro-11/9281
- [S16] KVR forum, "Logic and AU instrument midi out" (community; Logic 10.8, External Instrument → IAC) — https://www.kvraudio.com/forum/viewtopic.php?t=591011
- [S16b] LogicProHelp, "How to record MIDI using IAC Bus" and MacProVideo, "Capturing the Arpeggiator Output in Logic Pro X" (community, pages blocked; titles and search summaries only, may be outdated) — https://www.logicprohelp.com/forums/topic/55099-how-to-record-midi-using-iac-bus/ ; https://macprovideo.com/article/audio-software/capturing-the-arpeggiator-output-in-logic-pro-x
- [S17] Apple, Audio MIDI Setup User Guide, "Transfer MIDI information between apps" (IAC Driver "Device is online") — https://support.apple.com/guide/audio-midi-setup/transfer-midi-information-between-apps-ams1013/mac
- [S47] Apple, GarageBand User Guide, "Use Audio Units plug-ins with GarageBand on Mac" — https://support.apple.com/guide/garageband/gbnde06a4e4d/mac
- [S48] Jam Origin, "MIDI Guitar for iOS and MIDI Guitar for GarageBand" (Virtual MIDI; from search summary, page not read) — https://www.jamorigin.com/docs/midi-guitar-for-ios/

Jam Origin / MIDI Guitar

- [S20] Jam Origin, "MIDI Guitar & MIDI Bass Support – DAW setup" (per-DAW guide: Live 8/9, Logic, GarageBand, Cubase 7–11, Reaper, FL, Studio One, Pro Tools, Reason, Bitwig…; written for MIDI Guitar 2, may be outdated) — https://www.jamorigin.com/docs/daw/
- [S21] Jam Origin, MIDI Guitar 3 modules documentation, "MIDI Output" module (AU auto virtual port; VST "Direct MIDI Output / Sending MIDI out to the track"; MPE ch 2–9; legacy single channel) — https://jam.live/modules/modules/
- [S23] Sound On Sound, "Jam Origin MIDI Guitar" review (May 2014, may be outdated) — https://www.soundonsound.com/reviews/jam-origin-midi-guitar
- [S31] Jamosapien (MIDI Guitar user forum), "Midi Guitar and Reaper Help" (community, Mar 2019) — https://jamosapien.com/t/midi-guitar-and-reaper-help/235
- [S49] Jam Origin, downloads page (MG3 3.0.x: Win standalone/VST2/VST3; mac standalone/AU/VST2/VST3; iOS AUv3; no AAX) — https://jam.live/downloads

Cubase

- [S24] Native Instruments, "Sending MIDI from the Komplete Kontrol plug-in in Cubase" (Cubase 8, may be outdated) — https://support.native-instruments.com/support/solutions/articles/69000879829-sending-midi-from-the-komplete-kontrol-plug-in-in-cubase
- [S25] Steinberg forum, "Routing Midi Output of Instrument Tracks" (community, 2021–2022) — https://forums.steinberg.net/t/routing-midi-output-of-instrument-tracks/728668
- [S26] Steinberg Help Center, "Using VST 2 plug-ins in Cubase 14 / Nuendo 14" — https://helpcenter.steinberg.de/hc/en-us/articles/22554401894162
- [S27] JUCE forum, "Midi generator plugin: … VST3 'Midi Effect' too (for Ableton, Cubase, FL Studio)?" (community, 2024–2025; per-host behaviour, AU variant for Logic, FL pitch-wheel issue) — https://forum.juce.com/t/midi-generator-plugin-im-distributing-a-vst3i-could-i-distribute-a-vst3-midi-effect-too-for-ableton-cubase-fl-studio/65814
- [S27b] JUCE forum, "VST3 Midi & Audio" (community, 31 Aug 2024; Reaper/Bitwig in-chain, Studio One input) — https://forum.juce.com/t/vst3-midi-audio/62624/14
- [S27c] Steinberg forum, "Clarification needed: how can you work with midi (events) in Cubase with VST3?" (Jan 2026; Steinberg developer: VST3 is a real-time block API, not a part editor) — https://forums.steinberg.net/t/clarification-needed-how-can-you-work-with-midi-events-in-cubase-with-vst3/1019866

REAPER

- [S28] Cockos, REAPER changelog `whatsnew.txt` (v0.967 19 Jun 2006 VST MIDI output; v0.979 record output (MIDI); v6.80 27 May 2023 CLAP; v7.55 27 Nov 2025 AUv3 MIDI output; v7.82 4 Oct 2026) — https://www.reaper.fm/whatsnew.txt
- [S29] KVR forum, "How to bounce midi to midi in Reaper?" (community, Mar 2016) — https://www.kvraudio.com/forum/viewtopic.php?t=458435
- [S30] Reaper Tips, "Instantly convert audio to MIDI in REAPER" (third-party, 24 Mar 2024) — https://reapertips.com/post/instantly-convert-audio-to-midi-in-reaper

Bitwig

- [S32] Bitwig User Guide, "Recording Clips → Track I/O Settings" (inputs, hardware Note Outputs, monitor modes) — https://www.bitwig.com/userguide/latest/recording_clips/
- [S33] Bitwig User Guide, "Routing" devices (Note Receiver) — https://www.bitwig.com/userguide/latest/routing/
- [S34] KVR Bitwig forum, "routing midi from one track to plugin on another track" (community, 1 Jan 2023) — https://www.kvraudio.com/forum/viewtopic.php?t=591491
- [S35] KVR Bitwig forum, "Record MIDI notes from Arpeggiator track" (community, Nov 2015, Bitwig 1.3; may be outdated) — https://www.kvraudio.com/forum/viewtopic.php?t=450058
- [S35b] KVR Bitwig forum, "Record midi output of note effects" (community, Jan 2021; may be outdated) — https://www.kvraudio.com/forum/viewtopic.php?t=558556

FL Studio

- [S36] Image-Line, FL Studio manual, "Wrapper" (MIDI Input port / Output port) — https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm
- [S37] Image-Line, FL Studio manual, "MIDI Out" plug-in (port matching, 256 ports) — https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/MIDI%20Out.htm
- [S38] codefn42, "FAQ – MIDI Routing in FL Studio" (vendor guide) — https://www.codefn42.com/faq_routing_flstudio.html
- [S39] Image-Line forum, "Recording MIDI notes via MIDI Output from VST Effect to MIDI Input on VST Instrument" (community, Feb 2017, may be outdated) — https://forum.image-line.com/viewtopic.php?t=168249
- [S40] Scaler forum, "MIDI Routing – FL Studio – VST3" (community, 2018, may be outdated) — https://forum.scalermusic.com/t/midi-routing-fl-studio-vst3/60
- [S40b] Image-Line forum, "Question about MIDI output from instrument plugins" (community, 17 Oct 2024, unanswered) — https://forum.image-line.com/viewtopic.php?p=1972894

Studio One

- [S41] KVR forum, "How do I record a vst's sequencer midi output in Studio One?" (community) — https://www.kvraudio.com/forum/viewtopic.php?t=528060
- [S42] Scaler forum, "Cannot send individual Scaler 3 tracks to instrument tracks in Studio One" (community, Mar 2025; S1 6.6.4 / 7; VST3 vs AU) — https://forum.scalermusic.com/t/cannot-send-individual-scaler-3-tracks-to-instrument-tracks-in-studio-one/20079
- [S43] = [S27b]
- [S44] PreSonus Knowledge Base, "Studio One Pro 7: Setting up a Song to Record With a MIDI Hardware Keyboard/Synthesizer" (updated 9 Oct 2024; read via the Zendesk API) — https://support.presonus.com/hc/en-us/articles/30740780799245-Studio-One-Pro-7-Setting-up-a-Song-to-Record-With-a-MIDI-Hardware-Keyboard-Synthesizer

Pro Tools

- [S45] Sound On Sound, "Pro Tools has upped its MIDI game" (May 2024; MIDI plug-ins on Instrument-track inserts only; MIDI Chain) — https://www.soundonsound.com/techniques/pro-tools-midi-plug-ins
- [S46] Avid press release, 7 Mar 2024, "Avid Pro Tools Delivers Powerful New Ways to Work with MIDI" (page blocked for automated reading; quoted from search summary) — https://www.avid.com/press-room/2024/03/avid-pro-tools-delivers-powerful-new-ways-to-work-with-midi ; Avid resource page (blocked) — https://www.avid.com/resource-center/midi-plugins
- [S46b] Blue Cat Audio, "Using MIDI FX in Pro Tools" (updated 26 Mar 2024) — https://www.bluecataudio.com/Blog/tip-of-the-day/using-midi-fx-in-pro-tools/

Virtual MIDI ports

- [S18] Tobias Erichsen, loopMIDI (v1.0.16.27) — https://www.tobias-erichsen.de/software/loopmidi.html
- [S19] Tobias Erichsen, virtualMIDI SDK/driver — https://www.tobias-erichsen.de/software/virtualmidi.html
- [S50] Microsoft, "Windows MIDI Services" (Windows 11 25H2+, built-in virtual/app-to-app MIDI 2.0) — https://microsoft.github.io/MIDI/
