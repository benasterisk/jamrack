# Virtual MIDI ports, audio drivers and latency outside the browser

Research note for the JAMRACK "guitar-to-MIDI as a DAW plugin" feasibility study.
Written 2026-10-07. Every factual claim carries a numbered source `[n]` (list at the end).
Items marked **UNVERIFIED** could not be checked against a primary source.

Scope of this note: (1) virtual MIDI ports per OS, (2) a standalone application that
opens the audio interface itself and publishes a virtual MIDI port, (3) MIDI output
timing inside a plugin and what the host buffer means for our detection delay, plus
measured browser vs DAW latency so the owner can see what a plugin would actually gain.

---

## 0. Executive summary (numbers first)

| Question | Answer | Sources |
|---|---|---|
| Does the browser cost us latency a plugin would remove? | Yes. Chrome on Windows runs WASAPI shared mode with ~10 ms periods on both capture and render; Chromium's own header documents a "typical total delay of 35 ms" on the output side alone (10 ms period + 5 ms stream + 20 ms endpoint buffer). A DAW at 64 samples / 48 kHz with a native ASIO/Core Audio driver measures a full analog round trip of about 4–6 ms (RME) to ~12 ms (Focusrite Forte). | [27] [28] [20] [21] [22] |
| Measured browser round trip, default settings | Chrome: 62.8 ms (Windows 10), 52.3 ms (macOS, MacBook Pro 2021), 64.5 ms (Ubuntu 22.04). Firefox on macOS: 38.9 ms. (WAC 2025 study, MLS method, 100 runs.) | [30] |
| Measured browser round trip, tuned (`latencyHint: 0`, no EC/NS/AGC) | ~19 ms Chrome / ~14 ms Firefox on a 2016 MacBook (2021 test); 19–23 ms Chrome/Firefox on macOS with a Focusrite Gen3 at 128 samples. No tuned Windows figure found. | [29] [31] |
| What our tracker keeps regardless of platform | 1–2 periods of the note (12–24 ms on low E) plus the 64-sample hop. The plugin only removes the browser's I/O buffering, not the physics. | project data (not a web source) |
| Estimated gain of a native path (plugin or standalone) on Windows | Roughly 20–40 ms of input-to-MIDI delay removed vs default Chrome, i.e. from ~30–40 ms of I/O overhead down to ~1.5–6 ms (one host block of 64–256 samples at 48 kHz, plus driver). Estimate derived from [27] [28] [30] [20]; not a direct measurement of JAMRACK. | derived |
| Do hosts compensate plugin-reported latency for MIDI output? | Not reliably. On the Steinberg SDK forum: "MIDI events that are output from an effect plug-in do not have their sample position corrected for the plug-in's own latency/delay" (consistent across several DAWs incl. Cubase 10). JUCE forum (Aug 2025): `setLatencySamples()` with negative values had no effect; Reaper measured 11–15 ms, Digital Performer VST3 150 ms. Use the DAW's track delay / recording offset instead. | [36] [37] |
| Virtual MIDI on Windows today | Historically no built-in port: loopMIDI (free, built on the proprietary teVirtualMIDI driver; SDK not freeware, redistribution needs clearance). Since Feb 2026 Windows 11 24H2/25H2/26H1 ship Windows MIDI Services with built-in loopback endpoints and an app-to-app virtual-device API; the consumer "MIDI Loopback Setup" tool is announced for November 2026 – January 2027. The rollout broke third-party virtual ports (loopMIDI, teVirtualMIDI, Dubler) with a fix rolling out from 30 April 2026. | [5] [6] [7] [8] [9] [10] [11] [12] |
| Virtual MIDI on macOS | Built into CoreMIDI: an app calls `MIDISourceCreate`/`MIDISourceCreateWithProtocol` and every DAW sees the port; the IAC Driver bus exists for app-to-app without code. Jam Origin's standalone exposes "MIDI Guitar Virtual MIDI Out" this way. | [1] [2] [3] [16] |
| ASIO SDK licence | Since 15–29 October 2025 ASIO is dual-licensed: GPLv3 or the proprietary agreement (which still has to be signed; Steinberg staff pointed a closed-source DAW developer to the proprietary SDK page in June 2026). FlexASIO (PortAudio over WASAPI exclusive) and ASIO4ALL remain universal alternatives for users whose interface has no ASIO driver. | [23] [24] [25] [26] [18] [19] |

---

## 1. Virtual MIDI ports per operating system

### 1.1 macOS: CoreMIDI virtual sources and the IAC driver

- **API.** `MIDISourceCreate(client, name, outSrc)` "creates a virtual source in a client";
  after creation the app calls `MIDIReceived` to transmit MIDI "to any clients connected to
  the virtual source". Apple recommends re-assigning the same unique ID at every launch so
  other apps keep persistent references. The classic function is listed as deprecated in
  favour of `MIDISourceCreateWithProtocol` (MIDI 1.0 or 2.0 protocol, UMP). [1] [2]
- **No driver, no installer.** A standalone app can publish a port that Ableton Live, Logic,
  etc. see immediately. Jam Origin's documentation: "Mac users can skip this step as Apple
  operating systems already include a virtual midi standard"; the standalone exposes
  "MIDI Guitar Virtual MIDI Out" which the DAW selects as MIDI input. [16]
- **IAC Driver** (Audio MIDI Setup > MIDI Studio > IAC Driver > "Device is online", add
  buses): Apple's built-in app-to-app bus for apps that do not create their own port. [3]

### 1.2 Windows: no built-in virtual port until 2026

**Historical situation (Windows 7–11 up to early 2026).**

- The classic WinMM API has no call to create a virtual port; third-party kernel drivers
  fill the gap. **loopMIDI** (Tobias Erichsen) is a "virtual loopback MIDI cable for
  Windows 7 up to Windows 10" and "uses the virtualMIDI driver to actually create the
  ports". [5]
- **teVirtualMIDI SDK licensing** (for bundling a port inside our own app): "This software
  is NOT freeware or shareware. This software is copyrighted by Tobias Erichsen. Software
  linking to this SDK MAY NOT BE DISTRIBUTED in any way without prior clearance with me."
  Licensees get an MSI merge module to embed the driver in their installer; contact by
  e-mail with a project description. Pricing is not published. [6]
- Consequence for products: Jam Origin tells Windows users "you need a virtual midi loop
  driver, such as loopMidi or loopBe1 (both freeware)" and to select it as MIDI Output in
  the standalone; the DAW enables the same device as input. [16] A 2026 user thread on
  MG3 + Ableton Live 12 on Windows still routes through loopMIDI. [17]
- Vochlea **Dubler 2** ships its own virtual MIDI driver ("make sure the virtual MIDI driver
  is set to on" in the app; the DAW sees "Dubler 2" / "Dubler 2 MIDI"). [15] [14]

**Windows MIDI Services (2026).**

- Rollout: "began rolling out in February 2026 and applies to Windows 11 retail 24h2, 25h2,
  and 26h1" (phased / controlled feature rollout). [8] Microsoft's blog of 17 Feb 2026:
  "All of your existing MIDI 1.0-aware software just got even better, without needing any
  app updates"; MIDI 2.0 devices are "automatically translated and made available to classic
  MIDI 1.0 APIs". [7]
- Backwards compatibility: "the existing WinMM and WinRT MIDI 1.0 APIs have been repointed
  to the new Windows Service", so every DAW that uses WinMM participates in multi-client
  access and can use ports created from new MIDI 2.0 endpoints. The legacy APIs however
  "do not provide access to ... new features like creating loopback endpoints and virtual
  devices": to create a virtual device an app must use the new WinRT SDK. [9] [10]
- **Built-in loopback endpoints**: "Windows now has built-in MIDI loopback support ... no
  external drivers needed". Both loopback types "create MIDI 1.0 API ports", "show up in
  your DAW and your other MIDI apps like any other MIDI device", and persist across
  restarts by default. The consumer-facing *MIDI Loopback Setup* app "will be released to
  consumers in November 2026. It's currently available for developers"; "for most customers
  the new loopback features land between November 2026 and January 2027". [11]
- **App-created virtual devices** (what a standalone JAMRACK app would do): the
  `MidiVirtualDeviceManager.CreateVirtualDevice(config)` API creates the device endpoint;
  Windows then "constructs a second application-visible multi-client endpoint which
  applications use to talk to the device app". Requires the **WMS App SDK Runtime**
  (separate download, ~219 MB per the SDK docs). [12] Whether that app-visible endpoint is
  also exposed as a WinMM MIDI 1.0 port to today's DAWs is implied by [9]/[10] ("can use
  ports created from new MIDI 2.0 endpoints") but **UNVERIFIED** on the virtual-device page
  itself.
- **Known issue that matters to us**: in the 2026 release "dynamic ports including loopMIDI,
  loopBE, virtualTE / teVirtualMIDI / rtpMIDI / NI Service" had visibility problems; a fix
  started rolling out on 30 April 2026 via controlled feature rollout. [8] Vochlea's support
  page confirms: "This update contains a bug that breaks how third-party virtual MIDI
  ports work", with Dubler 2 relying on such a port; Microsoft "targeted a permanent fix ...
  by the end of April 2026". [13] Jam Origin's forum moderator likewise notes "a recent
  update which causes issues with loopmidi" on Windows 11. [17]
- **Which DAWs use the new API?** Microsoft tested with Cubase 15, Pocket MIDI, MIDI-OX and
  hardware partners (Yamaha, Roland, Steinberg, JUCE) [7]; Cubase 15 is shown receiving
  notes from a MIDI 2.0 device through Windows MIDI Services [7]; PACE announced JUCE
  support for Windows MIDI Services [40]. I found **no evidence that Ableton Live uses the
  new WinRT API** (UNVERIFIED either way) but it does not need to: a WinMM-based DAW sees
  the loopback / virtual ports through the repointed legacy API. [9] [10]

### 1.3 Linux: ALSA sequencer and JACK

- The ALSA sequencer lets any application create its own ports and users connect them
  with `aconnect`; the `snd-virmidi` module bridges raw-MIDI and sequencer ports. [4]
- `a2jmidid` bridges ALSA sequencer ports into JACK MIDI ("one JACK MIDI port for every
  ALSA sequencer port"). [4b]
- Practically: a standalone app on Linux just opens an ALSA sequencer client; Bitwig,
  Reaper, Ardour see it. No licensing question.

---

## 2. A STANDALONE application (opens the interface directly + virtual MIDI port)

### 2.1 Audio APIs and the ASIO licence question

| OS | Low-latency API | Licence / constraints | Sources |
|---|---|---|---|
| Windows | **ASIO** (vendor driver, exclusive by design) | ASIO SDK: since Oct 2025 "dual-licensed under GPL3 or the existing proprietary license"; "both options granting access to the same SDK". A closed-source app still signs the proprietary agreement (Steinberg staff, June 2026, pointed to steinberg.net/developers/prorietary-sdk/). The old regime (SDK sources not redistributable, hence Audacity's "NON-DISTRIBUTABLE" ASIO builds) only matters now if you refuse both the GPLv3 and the agreement. | [23] [24] [25] [26] |
| Windows | **WASAPI shared, IAudioClient3** | No licence. Default engine period is 10 ms; with a driver that supports small buffers, `IAudioClient3::InitializeSharedAudioStream` can request down to the driver minimum (e.g. the inbox HDAudio driver: 128–480 frames, 2.66–10 ms at 48 kHz). | [28] |
| Windows | **WASAPI exclusive** | No licence; bypasses the engine; only one app on the endpoint. | [28] |
| Windows | **FlexASIO / ASIO4ALL** (universal ASIO) | FlexASIO: open-source universal ASIO driver built on PortAudio (WASAPI shared/exclusive, WDM-KS, MME, DS); suggested low-latency config: WASAPI, `bufferSizeSamples = 480`, `wasapiExclusiveMode = true`. ASIO4ALL/ASIO2KS use Kernel Streaming. Ranking in the field: vendor driver > FlexASIO > ASIO4ALL. | [18] [19] |
| macOS | **Core Audio** | No licence; one driver model for all apps; Core Audio "is a software timer based approach" needing a "Safety Offset" buffer on record and playback (RME: 24 samples → 8 samples in a 2017 driver), i.e. a few tens of samples more than ASIO. | [22] |
| Linux | ALSA / JACK / PipeWire | No licence. | [4] |

**Multi-client ASIO caveat (important for the Gigcaster 5).** ASIO is exclusive by
design; a standalone tracker that opens the Gigcaster's ASIO driver may prevent Ableton from
opening it at the same time unless the vendor driver is multi-client. Vochlea's Dubler guide
states the same constraint from the user's side: Windows needs "an audio interface with
multi-client ASIO Drivers or ASIO4ALL", and "only one application can use an input or output
in ASIO4ALL at a time". [14] [15] Whether the BOSS Gigcaster 5 driver is multi-client is
**UNVERIFIED** (BOSS only states that a dedicated driver is needed for multitrack mode [41]).
A plugin inside the DAW avoids this problem entirely.

### 2.2 Typical round-trip and input latency (native drivers)

RTL = full analog round trip (input ADC → driver → app → driver → output DAC), as measured
with loopback tools (RTL Utility, Reaper). Our use case only needs the *input* half plus
the MIDI path, so roughly half of these numbers plus the hop.

| Interface / driver | OS | Rate | Buffer | Measured RTL | Source |
|---|---|---|---|---|---|
| RME Fireface UCX II, ASIO (Reaper) | Windows | 48 kHz | 48 / 64 / 96 / 128 | 4.1 / 5.23 / 7.1 / 9.16 ms | [20] |
| RME Fireface UCX II, lowest buffer 32 | Windows | 48 kHz | 32 | ~3 ms | [20b] |
| RME Babyface Pro FS, Core Audio | macOS (M1) | 48 kHz | 64 | 4.4 ms | [21] |
| Behringer X32 USB, Core Audio | macOS (M1) | 48 kHz | 64 | 8.9 ms | [21] |
| RME UFX+, Thunderbolt driver 1.07 | macOS | 96 kHz | 32 | 1.3 ms (RME staff) | [22] |
| RME ADI-2 Pro FS, native ASIO | Windows | 44.1 kHz | 64 | 5.3 ms (232 samples) | [26b] |
| RME ADI-2 Pro FS, native ASIO | Windows | 44.1 kHz | 256 | ~14.3 ms | [26b] |
| Focusrite Forte, native ASIO | Windows | 44.1 kHz | 64 | 11.8 ms | [26b] |
| RME via ASIO4ALL (WDM wrapper) | Windows | 44.1 kHz | 64 | 15.8 ms | [26b] |
| RME via FlexASIO (WASAPI backend) | Windows | 44.1 kHz | — | 9.6 ms (~2× native) | [26b] |
| RME via Windows WDM, min buffer 132 | Windows | 44.1 kHz | 132 | < 15 ms | [26b] |
| RME via DirectSound, min buffer 1023 | Windows | 44.1 kHz | 1023 | 122 ms | [26b] |
| Inbox HDAudio driver, WASAPI (Microsoft) | Windows 10+ | 48 kHz | 128–480 | buffer range 2.66–10 ms per direction (Microsoft's own chart, no exact RTL numbers in text) | [28] |

Takeaways: at 64 samples / 48 kHz a good native driver yields an RTL of ~4–6 ms (RME) and
input-side latency of ~2–3 ms; at 128 samples ~9 ms RTL; at 256 samples ~14 ms RTL. Cheap
interfaces or wrapper drivers (ASIO4ALL) easily double that. No measurement of the BOSS
Gigcaster 5 was found (**UNVERIFIED**; the owner can measure it with RTL Utility, cf. [26b]).

### 2.3 How Jam Origin and Vochlea actually do it

- **Jam Origin MIDI Guitar 2 / 3**: shipped as "App, VST2/3" on Windows, "App, AU, VST2/3"
  on macOS, "App, AUv3" on iOS (MG3 3.0.68 Win / 3.0.74 Mac, still labelled beta). [16b]
  The standalone hosts VST/AU instruments itself and, for DAW use, sends MIDI to a port:
  on macOS its own "MIDI Guitar Virtual MIDI Out", on Windows a loopMIDI/loopBe1 port. The
  plugin variant is the recommended path "for most DAWs". Recommended settings: "run your
  DAW at 44100Hz using 128 or 256 samples pr audio buffer"; "Running at too high
  samplerates will cause latency and unresponsive tracking". [16] On Windows MG3 relies on
  "an audio interface with an ASIO driver". [16c]
- **Vochlea Dubler 2** (voice-to-MIDI, standalone + plugin): the standalone app "acts as a
  virtual MIDI input device within your operating system"; the DAW selects "Dubler 2" as a
  MIDI input; the guide asks for a "Device Block Size to 128 samples or below"; Windows
  users without a vendor ASIO driver are sent to ASIO4ALL. Vochlea publishes no ms figure
  ("latency is below the audible range"). [14] [15]
- Both therefore use the **same two-track strategy** JAMRACK would: a plugin for in-DAW
  use, a standalone with a virtual port for everything else; on Windows the virtual port is
  the weak spot (third-party driver, licensing, and the 2026 Windows MIDI Services
  breakage), on macOS it is free.

---

## 3. INSIDE a plugin: MIDI output timing, host buffer, compensation

### 3.1 Sample-accurate MIDI output within the block

- **VST3**: an output `Event` carries `sampleOffset` = "sample frames related to the current
  block start sample position"; note-on/off/polyPressure/noteExpression and
  `LegacyMIDICCOutEvent` are the event types. [32]
- **CLAP**: `clap_event_header.time` is the "sample offset within the buffer for this event";
  "the plugin must insert events in sample sorted order when inserting events". [33]
- **AU / AUv3**: MIDI output goes through the `MIDIOutputEventBlock` with an
  `AUEventSampleTime`; developers report ambiguity between absolute and block-relative
  times and that adding offsets to it "cannot be reliably used to schedule MIDI events for
  the host". [34] [35]
- So within one host block a note detected at hop *k* can be stamped at the exact sample.
  The host collects the plugin's output events at the end of the block and dispatches them
  to the destination track; in practice this costs **zero to one host block** depending on
  the host's routing graph. The Cubase report of Aug 2026 (live VST3 MIDI-out routing at a
  4096-sample ASIO buffer producing "attacks tied to the processing-block period") shows
  that this block granularity is real and that some hosts handle it badly at large
  buffers. [36b]

### 3.2 What the host buffer size means for our detection delay

Our worklet already runs at hop 64 samples (2.67 ms at 24 kHz internal rate). In a plugin:

| Host block @ 48 kHz | Block duration | Our hop granularity inside it | Worst-case added scheduling delay |
|---|---|---|---|
| 64 samples | 1.33 ms | 1 hop | ≤ 1 block (1.33 ms) |
| 128 samples | 2.67 ms | 2 hops | ≤ 1 block (2.67 ms) |
| 256 samples | 5.33 ms | 4 hops | ≤ 1 block (5.33 ms) |

The tracker's own delay (1–2 periods of the note: 12–24 ms on the low E, ~3–6 ms on the
high E) is unchanged; the physics are the same in Chrome, in a standalone and in a plugin.
The plugin removes only the browser I/O buffering (section 3.4), which on Windows is the
larger of the two terms.

### 3.3 Do hosts compensate plugin latency for MIDI output?

- Steinberg SDK forum: "MIDI events that are output from an effect plug-in do not have their
  sample position corrected for the plug-in's own latency/delay"; shifting the sample
  position by hand "sometimes results in negative sample locations", behaviour consistent
  across several DAWs including Cubase 10. [36]
- JUCE forum, 11–12 Aug 2025 ("How do you compensate for latency in a MIDI-generating
  plugin?"): `setLatencySamples()` with negative values had no effect; measured end-to-end
  MIDI latency differed by host (Reaper on macOS 11–15 ms, Digital Performer VST3 ~150 ms);
  advice: "measure ... real latency through the DAW - and not just one DAW". [37]
- Reaper: PDC is driven by the latency a plugin reports, and the per-track option
  "Preserve PDC delayed monitoring in recorded items" decides whether MIDI is recorded
  "in time with key presses" or shifted by the monitoring latency. [38] (third-party
  write-up; the Reaper manual itself was not fetched: **UNVERIFIED wording**)

**What this means for recording MIDI in time.** When JAMRACK-as-plugin emits a note-on it is
*already* 10–25 ms late relative to the pick (tracker delay + one block). The host does not
know this (and would not move MIDI earlier even if told), so recorded notes land late by
that amount. The fixes are the DAWs' own offsets:

- **Ableton Live**: incoming MIDI is timestamped by the driver; when the track monitor is
  In/Auto, Live "adds an additional delay to the timestamp of the event based on the buffer
  size of your audio hardware" (records "at the time you hear them — not the time you play
  them"); Live's doctrine: "latency is preferable to jitter". [39] Live 12 adds the per-track
  option "Keep Monitoring Latency in Recorded Audio" which "adjusts the timing of the
  recording to match what is heard through Live's monitoring". [39b] *Driver Error
  Compensation* (Preferences > Audio) offsets recorded audio **and MIDI**, but only when the
  recording track's monitor is **Off**. [39c] A **negative Track Delay** on the receiving MIDI
  track is the usual way to pull late MIDI earlier (several Ableton forum threads; see
  [39d], forum, UNVERIFIED as an official statement). "Reduced Latency When Monitoring"
  bypasses the delay compensation added for *other* tracks but not latency inside the
  monitored chain. [39e]
- **Logic Pro**: *Recording Delay* slider (Settings > Audio > Devices) "delays the recording
  of audio by a certain fixed value, helping you to compensate for any delays that are caused
  by the audio driver" ("You should not normally need to touch this setting"); Logic shows
  the "roundtrip latency" under the I/O Buffer Size menu; *Low Latency Mode* routes around
  latency-inducing plug-ins up to a ms limit. [42] [43]
- **Cubase**: plugin MIDI-out via VST3 is routed live between instruments; the Aug 2026
  report shows block-period artefacts at large buffers. [36b]

Recommendation for the plugin: report **0 samples** of latency (a MIDI effect has no audio to
compensate), document the measured pick-to-MIDI delay per buffer size, and tell users to
enter it as a negative track delay (Live) or recording delay (Logic) if they need grid-tight
recordings. That is what the JUCE thread converges on. [37]

### 3.4 Browser I/O vs DAW I/O: measured figures

Round-trip (input → output) measurements are what exists in the literature; the input-only
share is roughly half.

| Platform / path | Settings | Measured RTL | Source |
|---|---|---|---|
| Chrome, Windows 10 (Lenovo) | browser defaults, MLS method, 100 runs | **62.8 ms** (σ 2.4) | [30] |
| Edge, Windows 10 | same | 60.8 ms | [30] |
| Firefox, Windows 10 | same | 104.7 ms | [30] |
| Chrome, macOS (MacBook Pro 2021) | same | **52.3 ms** (σ 1.1) | [30] |
| Firefox, macOS | same | 38.9 ms | [30] |
| Safari, macOS | same | 100.0 ms | [30] |
| Chrome / Chromium, Ubuntu 22.04 | same | 64.5 / 64.2 ms | [30] |
| Chrome, macOS 11 (2016 MacBook) | defaults | ~67 ms | [29] |
| Chrome, macOS 11 (2016 MacBook) | `latencyHint: 0`, EC/NS/AGC off | **~19 ms** | [29] |
| Firefox, macOS 11 | same tuned settings | ~14 ms | [29] |
| Chrome & Firefox, macOS, Focusrite Gen3 | 128 samples @ 44.1 kHz | 19–23 ms | [31] |
| Chrome, Windows, tuned | — | **no published measurement found** (UNVERIFIED) | — |
| DAW, RME UCX II ASIO, Windows | 64 @ 48 kHz | 5.23 ms | [20] |
| DAW, RME Babyface Pro FS, macOS M1 | 64 @ 48 kHz | 4.4 ms | [21] |
| DAW, Focusrite Forte ASIO, Windows | 64 @ 44.1 kHz | 11.8 ms | [26b] |
| DAW, RME via ASIO4ALL, Windows | 64 @ 44.1 kHz | 15.8 ms | [26b] |

Why Chrome on Windows is slow: Chromium's WASAPI implementation documents "A total typical
delay of 35 ms" on output: "Audio endpoint device period (~10 ms)", "Stream latency between
the buffer and endpoint device (~5 ms)", "Endpoint buffer (~20 ms to ensure glitch-free
rendering)"; exclusive mode exists only behind an experimental command-line switch, where
Chromium picks 256 frames (~5.33 ms). [27] Microsoft confirms that "by default all
applications in Windows 10 and later will use 10-ms buffers to render and capture audio"
unless they call `IAudioClient3` for a smaller period. [28] JAMRACK already uses
`latencyHint: 0` and raw capture (no EC/NS/AGC) per the project notes, so its real figure on
the owner's PC is probably between the "tuned" and "default" rows above; it has not been
measured on Windows (**UNVERIFIED**).

### 3.5 Summary table: browser vs standalone vs plugin, per OS

Input-side I/O overhead only (what the platform adds *before* our tracker sees the samples
and *after* it emits MIDI), excluding the tracker's 1–2 periods, which is identical in all
three. Figures are derived from the sources above; rows marked "est." are estimates, not
measurements.

| Path | Windows (owner's case) | macOS | Linux | Notes |
|---|---|---|---|---|
| **Browser (Chrome, AudioWorklet)** | ~30–60 ms RTL by default [30]; input share ≈ 15–30 ms (est., WASAPI shared 10 ms periods + WebRTC capture path [27] [28]) | ~19–23 ms RTL tuned [29] [31], ~52 ms default [30]; input share ≈ 10 ms tuned (est.) | ~64 ms RTL default [30] | MIDI stays inside the page (Web MIDI out to a loopback port adds the port's own latency, unmeasured) |
| **Standalone app + virtual MIDI port** | ASIO at 64/48 kHz: 5.2 ms RTL (RME) [20] → input share ≈ 2–3 ms; + virtual port (loopMIDI / Windows MIDI Services loopback, latency unmeasured, sub-ms expected: UNVERIFIED); needs multi-client ASIO to coexist with the DAW [14] | Core Audio at 64/48 kHz: 4.4 ms RTL (RME) [21]; CoreMIDI virtual source free [1] | ALSA/JACK, similar to macOS/Windows native (no figure found) | Licensing: ASIO GPLv3 or signed proprietary [23] [25]; teVirtualMIDI needs clearance [6]; Windows MIDI Services virtual device needs the WMS runtime [12] |
| **Plugin inside the DAW (VST3/AU/CLAP)** | DAW's own buffer: 64–256 samples = 1.33–5.33 ms per block; MIDI stamped sample-accurately [32] [33]; ≤ 1 block routing delay; no virtual port, no driver sharing issue | same | same | Host does not compensate MIDI-out latency [36] [37]: document the offset, let users set track delay / recording delay [39] [42] |

Net effect for the owner (Windows, Gigcaster 5, Ableton Live): moving from Chrome to a
plugin at 128 samples removes on the order of **20–40 ms** of platform overhead (default
Chrome) or **~10–15 ms** (if Chrome's tuned path is already achieved), leaving the
tracker's intrinsic 12–24 ms on the low strings as the dominant term. Measuring the current
browser figure on his PC (loopback through the Gigcaster with RTL Utility [26b]) would turn
these estimates into a number.

---

## 4. Open points / could not verify

1. Ableton's help article "Accessing the MIDI output of a VST plug-in" and the "Driver Error
   Compensation FAQ" are behind a Cloudflare challenge for automated fetching; their content
   above is taken from search-engine summaries consistent with the seed description
   ([39c], [39f]). Read them in a browser before quoting.
2. Whether Ableton Live (any version) uses the new Windows MIDI Services WinRT API (vs. the
   repointed WinMM) — no source found either way; functionally irrelevant for seeing
   loopback ports [9] [10].
3. Whether a Windows MIDI Services *virtual device* created by an app is exposed as a WinMM
   port to legacy DAWs — implied by [9]/[10], not stated on the virtual-device page [12].
4. BOSS Gigcaster 5 ASIO driver: multi-client or not, and its measured RTL — no source [41].
5. A measured Chrome-on-Windows round trip with `latencyHint: 0` and raw capture — none
   published; the Chromium issue 40629664 ("Low latency audio processing (< 20 ms) on
   Chrome / Windows 10") requires sign-in and could not be read; the "10 ms minimum" and
   "3 ms baseLatency reported by some users" claims come from the search summary only.
6. The claim that Live merges all MIDI channels when routing track to track (seed) is
   outside this note's scope; the Live 12 manual describes an Output Channel chooser for
   track-to-track routing [39b] but I did not verify channel merging.
7. Jam Origin MG3's Windows "Direct MIDI Output" option: unclear whether it creates its own
   port (via teVirtualMIDI) or still requires loopMIDI; the 2026 forum thread routes through
   loopMIDI [17].
8. Reaper's "Preserve PDC delayed monitoring in recorded items" exact wording is from a
   third-party note [38], not the Reaper manual.

---

## Sources

[1] Apple Developer, `MIDISourceCreate(_:_:_:)` — https://developer.apple.com/documentation/coremidi/midisourcecreate(_:_:_:)
[2] Apple Developer, `MIDISourceCreateWithProtocol(_:_:_:_:)` — https://developer.apple.com/documentation/coremidi/midisourcecreatewithprotocol(_:_:_:_:)
[3] Apple Support, Audio MIDI Setup: "Transfer MIDI information between apps on Mac" — https://support.apple.com/guide/audio-midi-setup/transfer-midi-information-between-apps-ams1013/mac
[4] Linux MIDI-HOWTO, virtual MIDI / snd-virmidi / aconnect — https://www.iitk.ac.in/LDP/HOWTO/MIDI-HOWTO-10.html
[4b] a2jmidid (ALSA sequencer ↔ JACK MIDI bridge) — https://github.com/linuxaudio/a2jmidid
[5] Tobias Erichsen, loopMIDI — https://www.tobias-erichsen.de/?p=45
[6] Tobias Erichsen, virtualMIDI SDK (licence terms) — https://www.tobias-erichsen.de/?p=343
[7] Windows Experience Blog, "Making music with MIDI just got a real boost in Windows 11", 17 Feb 2026 — https://blogs.windows.com/windowsexperience/2026/02/17/making-music-with-midi-just-got-a-real-boost-in-windows-11/
[8] Windows MIDI and Music dev blog, "Windows MIDI Services 2026 release – known issues and workarounds" — https://devblogs.microsoft.com/windows-music-dev/windows-midi-services-rollout-known-issues-and-workarounds/
[9] Windows MIDI Services, "About" (WinMM/WinRT repointed; supported releases 25h2+) — https://microsoft.github.io/MIDI/
[10] Windows MIDI Services, "Overview" (legacy APIs cannot create loopback/virtual devices) — https://microsoft.github.io/MIDI/overview/
[11] Windows MIDI Services, "MIDI Loopback Setup" (MIDI 1.0 API ports; consumer release Nov 2026 – Jan 2027) — https://microsoft.github.io/MIDI/tools/midiloopbacksetup/
[12] Windows MIDI Services SDK, Virtual transport / `MidiVirtualDeviceManager` — https://microsoft.github.io/MIDI/sdk-reference/Transports/Virtual/ and https://microsoft.github.io/MIDI/sdk-reference/Transports/Virtual/MidiVirtualDeviceManager/
[13] Vochlea, "Live MIDI not working on Windows 11" — https://vochlea.com/learn/live-midi-not-working-on-windows-11
[14] Vochlea, "Setting up with Studio One" (virtual MIDI driver on, block size ≤ 128, ASIO4ALL) — https://vochlea.com/learn/setting-up-with-studio-one
[15] Vochlea, "Setting up ASIO4ALL" / "Dubler not showing as a MIDI input" — https://vochlea.com/learn/setting-up-asio4all , https://vochlea.com/learn/dubler-not-showing-as-a-midi-input-2
[16] Jam Origin docs, "DAW" (loopMIDI/loopBe1 on Windows, "MIDI Guitar Virtual MIDI Out" on macOS, 44.1 kHz at 128–256 samples) — https://www.jamorigin.com/docs/daw/
[16b] Jam Origin, Downloads (formats and versions of MG2/MG3) — https://jam.live/downloads/
[16c] Jam Origin, MIDI Guitar 3 product page (beta status; ASIO on Windows) — https://jam.live/products/MG3/
[17] Jamosapien forum, "MG3 virtual MIDI output to LoopMIDI ... Windows / Ableton Live 12" — https://jamosapien.com/t/mg3-virtual-midi-output-to-loopmidi-channel-disabled-after-working-initially-windows-ableton-live-12/15719
[18] FlexASIO (GitHub, README) — https://github.com/dechamps/FlexASIO
[19] FlexASIO CONFIGURATION.md (WASAPI exclusive, bufferSizeSamples 480) — https://github.com/dechamps/FlexASIO/blob/flexasio-1.0/CONFIGURATION.md
[20] RME forum, "Fireface UCX II Latencies" (Reaper, 48 kHz, 48/64/96/128 samples) — https://forum.rme-audio.de/viewtopic.php?id=36957
[20b] MusicTech, RME Fireface UCX II review (3 ms RTL at 32 samples, 48 kHz) — https://musictech.com/reviews/rme-fireface-ucx-ii-review/
[21] RME forum, "Another Latency Question (RME – X32)" (Babyface Pro FS 4.4 ms, X32 8.9 ms at 64/48 kHz on Mac M1) — https://forum.rme-audio.de/viewtopic.php?id=39427
[22] RME forum, Matthias Carstens (RME) on Core Audio Safety Offset, 2017 — https://forum.rme-audio.de/viewtopic.php?id=24884
[23] KVR Audio, "Steinberg Moves VST 3 SDK to MIT Open Source License, ASIO Now GPLv3", 29 Oct 2025 — https://www.kvraudio.com/news/steinberg-moves-vst-3-sdk-to-mit-open-source-license-asio-now-gplv3-65179
[24] Libre Arts, "VST3 becomes open-source, ASIO goes GPL-compatible", 4 Nov 2025 — https://librearts.org/2025/11/steinberg-relicenses-vst3-and-asio/
[25] Steinberg forums, "Where do I submit the Proprietary ASIO SDK License Agreement?" (staff answer, June 2026) — https://forums.steinberg.net/t/where-do-i-submit-the-proprietary-asio-sdk-license-agreement/1035889
[26] Audacity manual, "ASIO Audio Interface" (non-distributable builds, licence conflict) — https://manual.audacityteam.org/man/asio_audio_interface.html
[26b] Archimago's Musings, "RTL Utility: a look at audio interface round-trip latency" (Nov 2021; ASIO vs ASIO4ALL vs FlexASIO vs WDM vs DirectSound numbers) — https://archimago.blogspot.com/2021/11/rtl-utility-look-at-audio-interface.html
[27] Chromium source, `media/audio/win/audio_low_latency_output_win.h` (typical 35 ms delay breakdown) and `audio_manager_win.cc` (exclusive-mode switch, 256 frames) — https://chromium.googlesource.com/chromium/src/media/+/main/audio/win/audio_low_latency_output_win.h , https://chromium.googlesource.com/chromium/src/media/+/main/audio/win/audio_manager_win.cc
[28] Microsoft Learn, "Low Latency Audio" (Windows drivers; 10 ms default, IAudioClient3, inbox HDAudio 128–480 frames) — https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio
[29] Jeff Kaufman, "Browser Audio Latency", Feb 2021 — https://www.jefftk.com/p/browser-audio-latency
[30] gilpanal/weblatencytest (WAC 2025 paper: "A Maximum Length Sequence–Based Method for Robust Round-Trip Latency Estimation in online Digital Audio Workstations", doi 10.5281/zenodo.17642262) — https://github.com/gilpanal/weblatencytest
[31] micbuffa/WAlatencyCompensation (19–23 ms RTL, macOS, Focusrite Gen3, 128 samples) — https://github.com/micbuffa/WAlatencyCompensation
[32] VST3 SDK docs, `Steinberg::Vst::Event` (`sampleOffset`) — https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1Event.html
[33] CLAP, `include/clap/events.h` (`time` = sample offset within the buffer) — https://github.com/free-audio/clap/blob/main/include/clap/events.h
[34] JUCE forum, "AUv3 – clarification of MIDI Event times" — https://forum.juce.com/t/auv3-clarification-of-midi-event-times/50010
[35] Apple Developer Forums, "Audio Unit V3 Logic Pro Losing MIDI Events" — https://developer.apple.com/forums/thread/85882
[36] Steinberg SDK forum, VST3 MIDI output not corrected for plug-in latency — https://sdk.steinberg.net/viewtopic.php?f=4&p=2019
[36b] Steinberg forums, "VST3 MIDI-out timing breaks at large ASIO buffer", Aug 2026 — https://forums.steinberg.net/t/vst3-midi-out-timing-breaks-at-large-asio-buffer/1039971
[37] JUCE forum, "How do you compensate for latency in a MIDI-generating plugin?", Aug 2025 — https://forum.juce.com/t/how-do-you-compensate-for-latency-in-a-midi-generating-plugin/66839
[38] "How to fix early MIDI recording in Reaper" (third-party note on "Preserve PDC delayed monitoring in recorded items") — https://publish.obsidian.md/arendleejessurun/Atlas/Music+production/Reaper/How+to+fix+early+MIDI+recording+in+Reaper
[39] Ableton Reference Manual 12, "MIDI Fact Sheet" — https://www.ableton.com/en/live-manual/12/midi-fact-sheet/
[39b] Ableton Reference Manual 12, "Routing and I/O" (Keep Monitoring Latency in Recorded Audio; track-to-track MIDI; External Instrument) — https://www.ableton.com/en/live-manual/12/routing-and-i-o/
[39c] Ableton Help, "Driver Error Compensation FAQ" (not fetchable by script; see §4) — https://help.ableton.com/hc/en-us/articles/115000234830
[39d] Ableton forum, "Track Delay vs. Driver Error Compensation? Differences?" — https://forum.ableton.com/viewtopic.php?p=1361752
[39e] Ableton Help, "Reduced Latency When Monitoring FAQ" — https://help.ableton.com/hc/en-us/articles/209072249
[39f] Ableton Help, "Accessing the MIDI output of a VST plug-in" (not fetchable by script; see §4) — https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in
[40] Sonicstate, "Windows11 MIDI Update Adds 2.0, Multi-Clients" (JUCE/PACE support; Cubase and Ableton named as DAW examples) — https://sonicstate.com/news/2026/04/02/windows11-midi-updates-add-20-multi-clients/
[41] Roland Support, "Gigcaster 5: How to Send DAW Tracks to the Gigcaster 5 Channels" (dedicated driver for multitrack mode) — https://support.roland.com/hc/en-us/articles/17771652080539-Gigcaster-5-How-to-Send-DAW-Tracks-to-the-Gigcaster-5-Channels
[42] Apple Support, "Manage input monitoring latency in Logic Pro for Mac" — https://support.apple.com/105040
[43] Apple, Logic Pro Devices preferences ("Recording Delay") — https://help.apple.com/logicpro/mac/10.1/en.lproj/lgcpbb81aca5.html
[44] JUCE forum, "MIDI CC from VST3 in Ableton Live 11" (VST3 note output works in Live 11; undefined CCs fixed in Live 11.3.25 / 12.0.10, July 2024) — https://forum.juce.com/t/midi-cc-from-vst3-in-ableton-live-11/51107
