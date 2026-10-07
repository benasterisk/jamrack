# Existing products: real-time guitar (and audio) to MIDI inside a DAW

Research note for the JAMRACK "DAW plugin" feasibility study. Topic: what already
exists. Written 7 October 2026 from web sources dated 2013-2026; every factual
claim carries a source URL. Things I could not verify are listed at the end.
Prices are the ones displayed on the cited pages on the day of research and can
change at any time.

## 1. Short answer to the owner's question

**Yes, the idea already exists, and it has existed since 2014.** Jam Origin's
*MIDI Guitar* (version 2, and version 3 in beta) is exactly what the owner
describes: an audio plug-in that you load on the guitar's audio track inside the
DAW, that listens to a normal guitar through a normal audio interface (no special
pickup), and that outputs MIDI which the DAW routes to any instrument track, or
that the standalone app sends through a virtual MIDI port
([Jam Origin DAW docs](https://www.jamorigin.com/docs/daw/),
[Ableton forum thread, May 2014](https://forum.ableton.com/viewtopic.php?t=204371)).
It is polyphonic, closed-source, USD 149.95 per platform licence
([jam.live shop](https://jam.live/shop/)), and it is the reference everybody
compares against on KVR and guitar forums
([KVR thread, Aug-Sep 2026](https://www.kvraudio.com/forum/viewtopic.php?t=613216),
[KVR thread, Sep 2020](https://www.kvraudio.com/forum/viewtopic.php?t=552408)).

**What does not exist (as far as I could find):** a free, open-source,
actively maintained, real-time, polyphonic guitar-to-MIDI plug-in with a
published benchmark on a public dataset. The open-source real-time plug-ins I
found are either monophonic voice trackers (Warf, voice2midi, Synodeia), tiny
unmaintained experiments with zero GitHub stars (Guit2Mid, MonolithMaestro),
or offline transcribers (NeuralNote, Basic Pitch). None publishes GuitarSet
numbers. Section 6 details where JAMRACK would stand.

## 2. How a plug-in's MIDI reaches the DAW (what the products rely on)

Three mechanisms are used by the products below; the owner's "output MIDI to
tracks or racks loaded in the DAW, or directly to another MIDI output" maps to
all three.

1. **Plug-in MIDI output routed by the DAW.** The plug-in sits on the guitar's
   audio track and emits MIDI; another track selects that plug-in as its MIDI
   input. Ableton documents this for VST plug-ins: load the plug-in on a track,
   then on the receiving MIDI track choose that track under "MIDI From" and the
   plug-in in the channel chooser, with monitoring set to In
   ([Ableton help: Accessing the MIDI output of a VST plug-in](https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in)
   — the page is behind a bot wall and I could only read it through search
   snippets; the quoted text is "Live merges all MIDI channels to one channel
   when being routed internally from track to track. It is therefore not
   possible to send MIDI to separate tracks via separate MIDI channels" and
   "The Audio Unit (AU) plug-in standard does not support a direct MIDI out").
   Live 10.1 added VST3 support and "internal MIDI routing for VST3 plug-ins"
   ([XLR8R, Live 10.1 release](https://xlr8r.com/news/ableton-releases-live-10-1/)).
   Jam Origin's own Ableton guide: "load the MIDI Guitar 2 VST plugin and use the
   direct midi output (which is enabled by default)", "VST is the preferred
   plugin format: unless noted otherwise always choose the VST format instead
   of the AU version", buffer 256 or 128 samples
   ([Jam Origin DAW docs](https://www.jamorigin.com/docs/daw/)).
   Reaper: "Record: MIDI Output" on the track. Cubase: direct MIDI output to
   instrument tracks (same page).
2. **Virtual MIDI port from a standalone app.** Jam Origin standalone exposes
   "MIDI Guitar Virtual MIDI Out"; this is the route for GarageBand and Pro
   Tools ("you need to use MIDI Guitar standalone and a virtual midi driver",
   [Jam Origin DAW docs](https://www.jamorigin.com/docs/daw/)). imitone and
   Dubler 2 started life this way (standalone app, any DAW that takes MIDI
   input). On Windows this needs a loopback driver such as loopMIDI; on macOS
   the IAC bus.
3. **AU MIDI output in Logic.** Logic Pro 10.8 (late 2023) started to support
   MIDI out from AU plug-ins; Apple's notes mention "Audio Units that output
   MIDI" and the 10.8.1 fix "Recorded MIDI notes are no longer duplicated with
   certain AUv3 instrument plug-ins"
   ([Apple, Logic Pro 10.8 release notes](https://support.apple.com/120134)).
   Users describe it as "clearly a work in progress as it is buggy" in late 2023
   ([KVR thread "Logic and AU instrument midi out"](https://www.kvraudio.com/forum/viewtopic.php?p=8798091)).
   Before that, Jam Origin's Logic guide relied on the AU instrument plus
   Logic's low-latency mode, or on the standalone and the virtual port
   ([Jam Origin DAW docs](https://www.jamorigin.com/docs/daw/)). Jam Origin
   now lists a separate product named "MIDI Guitar 3 for Logic"
   ([jam.live products](https://jam.live/products/)); its product page returned
   404, so how it routes MIDI inside Logic is unverified.

Caveat: the Ableton "AU does not support a direct MIDI out" sentence and the
Blue Cat note "VST3 or AU MIDI out is probably not supported by Live yet"
([Blue Cat KB](https://www.bluecataudio.com/HelpDesk/knowledgebase.php?article=49))
are undated and may be outdated for Live 12; the DAW-technology topic of this
study should confirm the current state with Live 12.

## 3. Real-time software products (plug-in or standalone)

### 3.1 Jam Origin MIDI Guitar 2 (released) and MIDI Guitar 3 (beta)

- **What it is.** "The world's first low latency, polyphonic software solution"
  for guitar to MIDI, no special pickup, standalone (Windows/Mac) plus VST/AU
  plug-ins and an iOS app ([jamorigin.com](https://www.jamorigin.com/)).
- **Formats, per the current downloads page** (fetched 7 Oct 2026):
  MG3 beta Windows 3.0.68 "App, VST2/3"; macOS 3.0.74 "App, AU, VST2/3";
  iOS/iPadOS 3.0.75 "App, AUv3" via TestFlight; stable legacy MG2 2.2.1
  (desktop) and 2.6.0 (iOS) ([jam.live downloads](https://jam.live/downloads/)).
  No AAX ("There is not yet an AAX version", Sound on Sound 2017; Pro Tools
  users go through the standalone and a virtual MIDI driver, Jam Origin docs).
  No CLAP mentioned anywhere.
- **Price.** "MIDI Guitar" USD 149.95, "MIDI Guitar Hex" add-on USD 149.95,
  "MIDI Bass" USD 149.95, "MIDI Cello" USD 149.95, each "Pay once = Lifetime
  license + Lifetime free updates", "there are never any discounts"; iOS is
  sold through in-app purchases ([jam.live shop](https://jam.live/shop/)).
  Historical: MG2 cost USD 99.95 in 2017
  ([Sound on Sound review, Sept 2017](https://www.soundonsound.com/reviews/jam-origin-midi-guitar-2))
  and "$99 including VAT" per Audiofanzine
  ([Audiofanzine](https://en.audiofanzine.com/jamorigin/midi-guitar-2/)); the
  KVR listing shows "$10 - $149.95" across platforms
  ([KVR product page](https://www.kvraudio.com/product/midi-guitar-by-jamorigin)).
  MG2 owners were told "the upgrade to MG3 will be for free"
  ([jamosapien, MG3 introduction, 29 Feb 2024](https://jamosapien.com/t/midi-guitar-3-introduction-a-first-look-inside/5960)).
- **Polyphony.** MG2: polyphonic, "handles chords and rapid arpeggios"
  (SOS 2017). MG3: "multidimensional polyphonic expressive MIDI guitar (MPE
  MIDI / MIDI 2.0)", four MPE dimensions "strike, pressure, brightness, and
  pitch", "modular and extensible" ([jam.live MG3](https://jam.live/products/MG3/)).
- **Status of MG3.** Beta since February/March 2024, Mac first
  ([KVR, Mar 2024](https://www.kvraudio.com/forum/viewtopic.php?t=607769)),
  opened to all in March 2025
  ([jamosapien, MG3 download](https://jamosapien.com/t/midi-guitar-3-download/6217)),
  and on 7 Oct 2026 the vendor still writes "we are still beta testing version
  3" and "Beta testing phase... refer to MIDI Guitar 2 for a production ready
  solution" ([jam.live downloads](https://jam.live/downloads/),
  [jam.live MG3](https://jam.live/products/MG3/)). So the flagship has been
  in beta for about two and a half years.
- **Latency claims and user reports.** SOS 2017: the standalone adds 3-6 ms,
  the plug-in is preferred for minimum latency, DAW buffer 128 or below at
  44.1/48 kHz, "I never felt the response to be sluggish"
  ([SOS 2017](https://www.soundonsound.com/reviews/jam-origin-midi-guitar-2)).
  MG2 release notes: "Latency was reduced, especially for the monophonic
  setting and generally on hollow body and half-acoustic guitars"
  ([MG2 release notes](https://www.jamorigin.com/midi-guitar-2-release-notes/)).
  One beta tester reported about 40 ms end-to-end with MG2 and hoped MG3 would
  be "ground breaking in terms of latency" (jamosapien, Feb 2024, above). A
  KVR poster explains the physical floor: detecting the lowest octave needs a
  full cycle, above 10 ms
  ([KVR thread, May 2016](https://www.kvraudio.com/forum/viewtopic.php?t=462790)).
- **Tracking, what users say.** SOS 2017: "once you get the trigger threshold
  set up for your playing style, the reliability of tracking both on single
  notes and chords is uncanny". KVR 2020: single notes "pretty spot on",
  chords "far from flawless", "double note triggering and notes picked up by
  unwanted string ringing", play "within the limitations"
  ([KVR, Sep 2020](https://www.kvraudio.com/forum/viewtopic.php?t=552408)).
  Loopy Pro forum (iOS users, 2020): "it struggled with chords, especially
  where close intervals", "requires fairly clean playing", better on electric
  than acoustic, but "MG2 in mono mode really is very expressive - more so than
  FTP [Fishman TriplePlay]"
  ([Loopy Pro forum](https://forum.loopypro.com/discussion/40935/pros-and-cons-of-different-guitar-to-midi-solutions-ios-midi-guitar-2-fishman-triple-play)).
  KVR 2026: MG3 "is guitar oriented and is probably the only way to have a good
  audio-to-MIDI plugin (if not the best)"
  ([KVR, Aug-Sep 2026](https://www.kvraudio.com/forum/viewtopic.php?t=613216)).
- **Published benchmark.** None found. Jam Origin publishes no accuracy
  figures on any dataset.

### 3.2 Vochlea Dubler 2 (voice-oriented)

- Real-time voice to MIDI (pitch, beatbox triggers, chords). Standalone plus
  "AU 64-Bit, VST3 64-Bit", Windows 10+ / macOS 10.13+, plug-in version
  "available since October 2024"
  ([Thomann listing](https://thomann.ae/vochlea_dubler_2.htm)).
- Price: USD 99 one-time on the vendor site (shown as reduced from 119)
  ([vochlea.com](https://vochlea.com/)); it launched at USD 249 / GBP 189 in
  2021 ([MusicTech review, 30 Sep 2021](https://musictech.com/reviews/software-instruments/vochlea-dubler-2-review/)).
- Review verdict (8/10): "extremely low latency", "fast and accurate beatbox
  recognition", but for pitch "virtually all MIDI that Dubler 2 spits out
  requires editing" (MusicTech, same). Vendor states "10-12 ms of latency for
  triggers" per search summary of the help center
  ([Vochlea help center](https://help-center.vochlea.com/)) — not re-verified
  on the page itself.
- Pitch tracking is single-voice (vocal); no guitar claims on the vendor site.
  Closed source.

### 3.3 imitone (voice-oriented, standalone + Windows VST)

- "imitone" USD 29, "imitone + VST" USD 59, "imitone studio + VST" USD 99;
  Windows and macOS 10.15+; standalone app, VST on Windows; standard edition
  tracks 1 voice, studio 8+ voices; "can respond in under 30 milliseconds for
  notes in the low tenor range", lower at higher pitch; still "beta
  (work-in-progress)" ([imitone.com](https://imitone.com/)). Latest itch.io
  devlog 0.13.1b in May 2024
  ([itch.io devlog](https://itch.io/devlog/725240/imitone-0131b-vst-fixes-and-improvements.amp)).
  Closed source.

### 3.4 Dodo MIDI 2 (free, voice or instruments)

- Free real-time audio-to-MIDI plug-in, version 2.1 released 6 June 2025,
  VST3 and AU, Windows, Mac (Intel) and Linux (x86 and ARM64); "imperceptible
  latency", legato/slides via pitch bend (set the synth's bend range to 12 or
  24 semitones) ([dodobirdmusic.com](https://dodobirdmusic.com/dodo-midi/));
  first released October 2021 as VST2/VST3
  ([Bedroom Producers Blog, Oct 2021](https://bedroomproducersblog.com/2021/10/25/dodo-audio-midi-converter/)).
  Polyphony not stated (behaviour described is monophonic with pitch bend; a
  KVR user: "works well for sliding notes (uses pitchbend)",
  [KVR 2026](https://www.kvraudio.com/forum/viewtopic.php?t=613216)).
  Free but not open source (no source repository found).

### 3.5 MeldaProduction MTuner (free tuner with MIDI out)

- Free, VST/VST3/AU/AAX, Windows and macOS; can "feed the detected note (as
  well as the pitchbend data, if 'Emit Pitchbend' is marked) as MIDI data into
  any synthesizer or sampler"; monophonic for that purpose; version 17.10
  ([KVR product page](https://www.kvraudio.com/product/mtuner-by-meldaproduction)).
  KVR user: "does somewhat low latency pitch to midi" but emits many spurious
  notes, needs a MIDI filter ([KVR 2026](https://www.kvraudio.com/forum/viewtopic.php?t=613216)).

### 3.6 KlangLabs Synodeia 2 (free, Windows VST2)

- "A real-time audio to MIDI triggering plugin that recognizes pitch from a
  monophonic source", free, VST (2), Windows, version 2.0; user review:
  "detects the pitch at the start and holds that note even if you vary it"
  ([KVR product page](https://www.kvraudio.com/product/synodeia-2-by-klanglabs)).
  Old and no longer developed as far as I can tell (no date on the page).

### 3.7 WIDI Audio to MIDI VST, Migic (legacy)

- WIDI Audio To MIDI VST did real-time recognition; last update 1.10 on
  8 Oct 2012 ([Softpedia changelog](https://www.softpedia.com/progChangelog/WIDI-Audio-to-MIDI-VST-Changelog-28605.html)).
  Migic was named as an alternative in 2016
  ([KVR 2016](https://www.kvraudio.com/forum/viewtopic.php?t=462790)); I
  found no current page for it (unverified status).

### 3.8 Open-source real-time plug-ins on GitHub

| Project | What it is | Formats / OS | Mono / poly | Latency stated | Licence, activity |
|---|---|---|---|---|---|
| [Guit2Mid (CGFrog)](https://github.com/CGFrog/Guit2Mid) | JUCE plug-in: clean guitar DI to MIDI, chords, hammer-ons, optional bends; Klapuri-style harmonic salience, bottom-up note selection; MPE output "every note its own channel" | VST3, AU, standalone | Poly ("Chords & Notes") or single-note mode | "about 27 ms for single notes, about 45 ms for chord bass notes" (Fast mode), ~10-11 % of one core | No LICENSE file (raw URL 404); 0 stars, 5 commits; README says the code was rewritten with an LLM; self-reported synthetic test "0.77 F1 ... 0.90 precision" on 620 synthetic notes, no public-dataset benchmark |
| [Warf (Str8b33fcak3)](https://github.com/Str8b33fcak3/warf) | JUCE 8 VST3, YIN-style monophonic tracker; two MIDI paths: VST3 MIDI output bus and a direct MIDI device picker | VST3, Windows x64 (also Tauri app and PWA) | Mono only; "Feed it a chord ... it will report whichever note the algorithm judges dominant" | "approximately 46 ms / 23 ms hop" at 44.1 kHz | Beta, LICENSE file present but contents not retrievable (404 on raw URL); 0 stars |
| [MonolithMaestro](https://github.com/MonolithOfficial/MonolithMaestro) | VST3 real-time pitch display, 2048-point FFT, up to 4 notes | VST3, Win/mac/Linux | Up to 4 notes, display only | not stated | "provided as-is", **no MIDI output** (planned); 0 stars |
| [voice2midi (Alexgmatosc)](https://github.com/Alexgmatosc/voice2midi) | JUCE 8 YIN voice to MIDI | Standalone, VST3, AU | Mono | "low-latency", buffer 128-256 advised | Licence not stated; 1 star |
| [aubionotes / mod-utilities LV2](https://lists.linuxaudio.org/archives/linux-audio-dev/2014-May/034863.html) | aubio's command-line note tracker (JACK in, MIDI out) and an LV2 wrapper | CLI (JACK), LV2 | Mono | not stated | aubio is GPL; "pitch detection can be error-prone" per the LAU list |

Other repositories in the usual "audio to MIDI" list are offline tools
(Python, piano models) ([natowi gist](https://gist.github.com/natowi/d26c7e97443ec97e8032fb7e7596f0b0)).
GitHub's search API was not reachable from this session, so star counts come
from the rendered pages and could be slightly stale.

## 4. Offline audio-to-MIDI (not real-time, for context)

| Product | Mode | Poly | Formats | Price / licence | Source |
|---|---|---|---|---|---|
| NeuralNote v2.0.0 (DamRsn) | Record or drop a file in the plug-in, transcribe, then drag the MIDI onto a track or save a .mid; "No native MIDI out; files only". v2 replaced Spotify Basic Pitch with the MuScriptor transformer (103 M-1.4 B params, three downloadable sizes, 0.5x-3.5x real time on an M1 Pro) | Yes, multi-instrument | VST3, AU, standalone; macOS Apple Silicon, Windows x64, Linux from source | Code Apache-2.0; v2 model weights CC BY-NC 4.0 (non-commercial) | [GitHub](https://github.com/DamRsn/NeuralNote), [v2.0.0 release](https://github.com/DamRsn/NeuralNote/releases/tag/v2.0.0) (dated "October 4", year not shown; the MuScriptor paper is [arXiv 2607.08168](https://arxiv.org/pdf/2607.08168), i.e. 2026) |
| Spotify Basic Pitch | Offline model, "up to 10x faster than real time", pitch-bend detection; also a browser demo | Yes | Python library, web demo | Open source (licence not re-verified this session) | [GitHub](https://github.com/spotify/basic-pitch), [basicpitch.spotify.com](https://basicpitch.spotify.com/) |
| Celemony Melodyne 5 | Analysis of recorded audio via ARA (an extension of VST/AU/AAX that lets the DAW hand the whole audio file to the plug-in instead of streaming it in real time); "Export audio notes as MIDI notes ... same position, length and pitch", velocity from amplitude; DNA polyphonic editing in Editor and above | Yes (Editor/Studio) | VST3, AU, AAX, ARA | Commercial, several editions | [Celemony help: Audio to MIDI](https://helpcenter.celemony.com/M5/doc/melodyneAssistant5/en/M5tour_ExportMIDI_otherDAWs), [KVR: ARA available](https://www.kvraudio.com/news/19536) |
| Ableton Live, Convert Melody / Harmony / Drums to New MIDI Track | Offline commands on a selected audio clip (Create menu / clip context menu); manual advises "clear attacks", "isolated instruments", uncompressed files | Harmony: yes; Melody: mono | Built in | Included in Live (edition not stated in the manual) | [Live 12 manual, ch. 13](https://www.ableton.com/en/live-manual/12/converting-audio-to-midi/) |

## 5. Hardware alternatives (context)

| Device | How | Mono/poly | Price (page date) | Latency / tracking reports | Source |
|---|---|---|---|---|---|
| Fishman TriplePlay Wireless / Connect / Express | Magnetic hexaphonic pickup clipped to the guitar, one tracker per string; USB-C (Express, announced 25 Sep 2024) or wireless; ships with TriplePlay Host, a VST2/VST3 host | Poly, per-string MIDI channels possible | Express USD 199.95; Connect USD 229.95; Wireless about USD 350 (forum figure) | "average of about 10 ms latency" (2013 review); 2019 test: few spurious notes, bends and bar captured; forum: "much more accurate" on chords than MG2, "FTP all the way" for chords; "latency is noticeable over Bluetooth" | [KVR news, Express](https://www.kvraudio.com/news-print.php?id=61752), [Cream City Music, Connect](https://www.creamcitymusic.com/fishman-tripleplay-connect-wired-midi-guitar-controller), [NZ Musician 2013](https://nzmusician.co.nz/features/fishman-tripleplay/), [bonedo 2019](https://www.bonedo.de/artikel/fishman-tripleplay-test/), [Loopy Pro forum](https://forum.loopypro.com/discussion/40935/pros-and-cons-of-different-guitar-to-midi-solutions-ios-midi-guitar-2-fishman-triple-play), [fishman.com](https://www.fishman.com/tripleplay/) |
| BOSS GM-800 + GK-5 (Serial GK) | Divided pickup, digital serial link; the unit is a ZEN-Core synth and also does "Guitar-to-MIDI Operation" over USB MIDI and 5-pin MIDI out | Poly | GM-800 USD 749.99, GK-5 USD 249.99, GKC-AD adapter USD 199.99 | SOS Nov 2023: "fast and accurate pitch tracking", fewer finger-noise artefacts, no ms figure; forum: TriplePlay "a few ms" faster but "barely perceptible", GM-800 tracking "meet or exceed" TriplePlay; "very unforgiving" of bum notes | [Sound on Sound review](https://www.soundonsound.com/reviews/boss-gm-800), [Adorama](https://www.adorama.com/bsgm800.html), [Elektronauts thread](https://www.elektronauts.com/t/boss-gm-800/197725?page=2), [Thomann reviews](https://www.thomann.de/ie/boss_gm_800_guitar_synthesizer_reviews.htm) |
| BOSS SY-1000 | GK 13-pin, USB audio/MIDI interface, guitar-to-MIDI into a DAW | Poly | not researched | SOS: guitar-to-MIDI "fast and clean, following perfectly when recording MIDI into DAW software with little needing cleanup" | [Sound on Sound](https://www.soundonsound.com/node/4921062?page=2) |
| Sonuus G2M V3 | Pedal, normal guitar jack in, 5-pin MIDI out, V3 since 2016 | **Mono** | USD 99.99 | Vendor: "Near zero latency" (no number), pitch-bend or chromatic mode | [sonuus.com](https://www.sonuus.com/products_g2m.html) |
| Sonuus i2M musicport | USB version of the same idea plus Hi-Z audio interface | Mono | about USD 99-149 | — | [Adorama](https://www.adorama.com/soi2mmscprt.html) |
| Jamstik Studio | A guitar with a built-in MIDI pickup and processor, USB | Poly | about USD 800 | "latency ranges from 6 to 15 milliseconds" wired | [MusicTech review](https://musictech.com/reviews/software-instruments/jamstik-studio-creator-best-midi-guitar/) |
| Roland GK-3 (older 13-pin) | Hex pickup into GR/GI units | Poly | — | Forum consensus: "does not track nearly as well" than TriplePlay | [Loopy Pro forum](https://forum.loopypro.com/discussion/40935/pros-and-cons-of-different-guitar-to-midi-solutions-ios-midi-guitar-2-fishman-triple-play) |

## 6. Comparison table (real-time only)

"Latency" below is what the vendor or reviewer states, not a measurement by me.
JAMRACK's own line uses the repo's measured numbers.

| Product | Real-time | Mono / poly | Plug-in formats | Platforms | Price | Open source | Latency stated | Published accuracy |
|---|---|---|---|---|---|---|---|---|
| Jam Origin MIDI Guitar 2 (stable 2.2.1) | Yes | Poly (+ mono mode) | Standalone, VST2, AU; no AAX; iOS app | Win, mac, iOS | USD 149.95 (was 99.95) | No | +3-6 ms standalone, buffer <=128 advised; users ~40 ms end to end | None |
| Jam Origin MIDI Guitar 3 (beta 3.0.68-3.0.75) | Yes | Poly, MPE / MIDI 2.0 | App + VST2/VST3 (Win), App + AU + VST2/VST3 (mac), App + AUv3 (iOS); no AAX/CLAP | Win, mac, iOS | USD 149.95, free for MG2 owners | No | "blazing fast", no number | None |
| Vochlea Dubler 2 | Yes | Single voice (voice) | Standalone, VST3, AU (since Oct 2024) | Win, mac | USD 99 | No | "10-12 ms" triggers (help center, unverified) | None |
| imitone | Yes | 1 voice (8+ in studio) | Standalone; VST on Windows | Win, mac | USD 29 / 59 / 99 | No | "<30 ms low tenor" | None |
| Dodo MIDI 2.1 | Yes | Not stated (mono + bend) | VST3, AU | Win, mac Intel, Linux | Free | No | "imperceptible" | None |
| Melda MTuner | Yes | Mono (MIDI out) | VST, VST3, AU, AAX | Win, mac | Free | No | "somewhat low latency" (user) | None |
| Synodeia 2 | Yes | Mono | VST2 | Win | Free | No | "low latency" (user) | None |
| Guit2Mid | Yes | Poly, MPE | VST3, AU, standalone | JUCE, any | Free | Source public, **no licence** | 27 ms single / 45 ms chord bass | Synthetic only (F1 0.77) |
| Warf | Yes | Mono | VST3 | Win | Free | Source public, licence unclear | ~46 ms | None |
| MonolithMaestro | Yes (display) | Up to 4 notes | VST3 | Win, mac, Linux | Free | No licence; **no MIDI out** | — | None |
| voice2midi | Yes | Mono | VST3, AU, standalone | mac (CMake) | Free | Licence unclear | — | None |
| Fishman TriplePlay (hardware) | Yes | Poly, hex pickup | n/a (USB MIDI device) | Win, mac, iOS | USD 199.95-350 | No | ~10 ms average (2013) | None |
| BOSS GM-800 (hardware) | Yes | Poly, hex pickup | n/a (USB/5-pin MIDI) | any | USD 749.99 + 249.99 pickup | No | "hardly any noticeable" (users) | None |
| Sonuus G2M V3 (hardware) | Yes | Mono | n/a (5-pin MIDI) | any | USD 99.99 | No | "near zero" (vendor) | None |
| **JAMRACK today (browser)** | Yes (AudioWorklet, hop 64 samples / 2.67 ms) | Mono tracker + POLY beta (sparse beta-NMF) | None yet (Web Audio page) | Any browser; HTTPS page | Free | **MIT** | Detection 1-2 periods (12-24 ms on low E) + browser buffers ~10-25 ms (`docs/guitar-to-midi.md`) | **GuitarSet solo, mono: precision ~72 % / recall ~84 % with the asymmetric window, 68.5 % / 75.5 % with mir_eval +-50 ms, F1 71.8 %; POLY prototype: isolated notes F1 81 %, ghosts <=1 %, doubles 55.8 %, triads 53.5 %** (`docs/polyphonic-plan.md`) |

## 7. What users consistently say about latency and tracking (all products)

- The latency floor is physics, not CPU: a tracker must see one or two periods
  of the note; the low E (82 Hz) is 12 ms per period, and KVR regulars state
  "over 10 milliseconds" for the low octave as unavoidable
  ([KVR 2016](https://www.kvraudio.com/forum/viewtopic.php?t=462790)). Hex
  pickups do not change this; they only avoid separating strings in software.
  This matches JAMRACK's own analysis in `docs/guitar-to-midi.md`.
- Software (MG2) is praised for single notes and expressiveness in mono mode;
  chords are where everyone, including hardware, has trouble; close intervals,
  string ringing and double triggers are the recurring complaints
  ([KVR 2020](https://www.kvraudio.com/forum/viewtopic.php?t=552408),
  [Loopy Pro](https://forum.loopypro.com/discussion/40935/pros-and-cons-of-different-guitar-to-midi-solutions-ios-midi-guitar-2-fishman-triple-play)).
- Hex-pickup hardware (TriplePlay, GM-800) is reported to track chords better
  than MG2, at the cost of a pickup to mount, and even then "anything more
  complicated, it was a no go" for one TriplePlay user
  ([Elektronauts](https://www.elektronauts.com/t/boss-gm-800/197725?page=2)).
- Every tool asks the player to set a threshold or sensitivity per guitar and
  to play cleanly; "play within the limitations" is the common advice.

## 8. Where a free, open-source, web-plus-plugin project with a GuitarSet benchmark would stand

1. **Category.** It would be the open-source equivalent of Jam Origin MIDI
   Guitar: same deployment model (plug-in on the audio track, DAW routes the
   MIDI; standalone with a virtual MIDI port as fallback), same "no special
   pickup" promise. That category has had exactly one serious commercial
   product for twelve years, whose next version has been in beta since early
   2024, and no credible open-source competitor: the two open-source
   polyphonic attempts (Guit2Mid, MonolithMaestro) have zero stars, no licence
   or no MIDI output, and the maintained open-source plug-ins are monophonic
   voice trackers (Warf, voice2midi) or offline (NeuralNote, whose v2 weights
   are non-commercial).
2. **Benchmark.** Nobody in the table publishes accuracy on a public dataset.
   Jam Origin publishes none; hardware vendors publish none; Guit2Mid reports
   a synthetic F1 only. JAMRACK's GuitarSet numbers (mono F1 71.8 % solo, POLY
   isolated notes F1 81 %, doubles 55.8 %, triads 53.5 %) would be the only
   measured figures in the category. Two caveats to state next to them:
   GuitarSet is an acoustic-guitar dataset recorded with a hexaphonic pickup
   ([GuitarSet paper, ISMIR 2018](https://archives.ismir.net/ismir2018/paper/000188.pdf)),
   while most users play electric guitars, and offline academic systems reach
   F-measures around 88 % on GuitarSet
   ([GAPS, ISMIR 2024](https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2024/RileyEtAl-ISMIR-2024.pdf)),
   so the honest pitch is "measured and real-time", not "best".
3. **Latency.** A native plug-in removes the browser input/output buffers
   (roughly 10-25 ms today) but not the detection floor (1-2 periods), so a
   plug-in would land in the same 15-45 ms band users report for MG2 and in the
   10-15 ms band of hex hardware only on high strings. Guit2Mid's self-reported
   27 ms single-note figure and Warf's 46 ms show what naive implementations
   get; JAMRACK's 64-sample hop is already finer than both.
4. **Honest positioning versus MG3.** MG3 offers MPE with four expression
   dimensions, a plug-in host and effects, iOS and a decade of tuning; a
   JAMRACK plug-in would offer: free, MIT, a published benchmark, a browser
   version that needs no install, a calibration assistant with named profiles,
   and per-note bend. "Notes seules mieux que MONO, doubles et triades au
   mieux" remains the right wording; nobody should claim parity with hex
   hardware on chords.
5. **Formats that matter for the owner.** Ableton Live on Windows: VST3 (Live
   routes VST plug-in MIDI between tracks; Live merges all MIDI channels when
   routing track to track, so per-string channels / MPE zones need checking in
   the DAW-technology topic). CLAP is not supported by Live, Logic or Cubase
   per the seed; AAX is not offered by any software competitor either. A
   standalone build with a virtual MIDI port (loopMIDI on Windows) covers every
   DAW, as Jam Origin does for Pro Tools and GarageBand.

## 9. Unverified or possibly outdated

- Ableton help article text: read through search-engine snippets only (the
  page blocks non-browser clients); the AU-has-no-MIDI-out sentence may
  predate Live 12 and Logic 10.8-era AU MIDI output.
- MIDI Guitar 3 for Logic: product listed, page 404; routing in Logic unknown.
- MG2 "around 40 ms" end-to-end is a single forum user's figure.
- Fishman: a "7 ms (high notes) to 14 ms (low notes)" pitch-detection figure
  appeared in a search summary without a readable source; only the "about
  10 ms average" (NZ Musician, 2013) and bonedo's 1.9 ms interface figure were
  verified. TriplePlay Wireless "about USD 350" is a forum figure.
- Vochlea "10-12 ms" trigger latency: from the help center via search summary.
- NeuralNote v2.0.0 year (2026) is inferred from the MuScriptor paper date.
- Basic Pitch licence not re-verified (GitHub API blocked in this session).
- Guit2Mid and Warf licences: LICENSE raw files returned 404 / unknown; star
  counts read from rendered pages.
- Migic status, Melodyne edition prices, SY-1000 price: not researched.
- Reddit could not be searched (domain blocked for the search tool); user
  opinions come from KVR, Loopy Pro, Elektronauts, jamosapien and Thomann
  reviews instead.
- Logic Pro 10.8 date: Apple's page shows 13 May 2024 as page date while user
  threads place 10.8.1 in late November 2023.

## 10. Source list

- Jam Origin: https://jam.live/products/MG3/ , https://jam.live/products/ ,
  https://jam.live/shop/ , https://jam.live/downloads/ ,
  https://www.jamorigin.com/ , https://www.jamorigin.com/docs/daw/ ,
  https://www.jamorigin.com/midi-guitar-2-release-notes/ ,
  https://www.kvraudio.com/product/midi-guitar-by-jamorigin ,
  https://www.soundonsound.com/reviews/jam-origin-midi-guitar-2 ,
  https://en.audiofanzine.com/jamorigin/midi-guitar-2/ ,
  https://jamosapien.com/t/midi-guitar-3-introduction-a-first-look-inside/5960 ,
  https://jamosapien.com/t/midi-guitar-3-download/6217 ,
  https://www.kvraudio.com/forum/viewtopic.php?t=607769 ,
  https://forum.ableton.com/viewtopic.php?t=204371
- DAW routing: https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in ,
  https://xlr8r.com/news/ableton-releases-live-10-1/ ,
  https://www.bluecataudio.com/HelpDesk/knowledgebase.php?article=49 ,
  https://support.apple.com/120134 ,
  https://www.kvraudio.com/forum/viewtopic.php?p=8798091
- Forums on tracking/latency: https://www.kvraudio.com/forum/viewtopic.php?t=462790 ,
  https://www.kvraudio.com/forum/viewtopic.php?t=613216 ,
  https://www.kvraudio.com/forum/viewtopic.php?t=552408 ,
  https://forum.loopypro.com/discussion/40935/pros-and-cons-of-different-guitar-to-midi-solutions-ios-midi-guitar-2-fishman-triple-play ,
  https://www.elektronauts.com/t/boss-gm-800/197725?page=2
- Vochlea: https://vochlea.com/ , https://thomann.ae/vochlea_dubler_2.htm ,
  https://musictech.com/reviews/software-instruments/vochlea-dubler-2-review/ ,
  https://help-center.vochlea.com/
- imitone: https://imitone.com/ , https://itch.io/devlog/725240/imitone-0131b-vst-fixes-and-improvements.amp
- Dodo MIDI: https://dodobirdmusic.com/dodo-midi/ , https://bedroomproducersblog.com/2021/10/25/dodo-audio-midi-converter/
- MTuner: https://www.kvraudio.com/product/mtuner-by-meldaproduction
- Synodeia 2: https://www.kvraudio.com/product/synodeia-2-by-klanglabs
- WIDI: https://www.softpedia.com/progChangelog/WIDI-Audio-to-MIDI-VST-Changelog-28605.html
- Open source: https://github.com/CGFrog/Guit2Mid , https://github.com/Str8b33fcak3/warf ,
  https://github.com/MonolithOfficial/MonolithMaestro , https://github.com/Alexgmatosc/voice2midi ,
  https://gist.github.com/natowi/d26c7e97443ec97e8032fb7e7596f0b0 ,
  https://lists.linuxaudio.org/archives/linux-audio-dev/2014-May/034863.html
- Offline: https://github.com/DamRsn/NeuralNote , https://github.com/DamRsn/NeuralNote/releases/tag/v2.0.0 ,
  https://arxiv.org/pdf/2607.08168 , https://github.com/spotify/basic-pitch ,
  https://basicpitch.spotify.com/ ,
  https://helpcenter.celemony.com/M5/doc/melodyneAssistant5/en/M5tour_ExportMIDI_otherDAWs ,
  https://www.kvraudio.com/news/19536 ,
  https://www.ableton.com/en/live-manual/12/converting-audio-to-midi/
- Hardware: https://www.kvraudio.com/news-print.php?id=61752 ,
  https://www.creamcitymusic.com/fishman-tripleplay-connect-wired-midi-guitar-controller ,
  https://www.fishman.com/tripleplay/ , https://nzmusician.co.nz/features/fishman-tripleplay/ ,
  https://www.bonedo.de/artikel/fishman-tripleplay-test/ ,
  https://www.soundonsound.com/reviews/boss-gm-800 , https://www.adorama.com/bsgm800.html ,
  https://www.thomann.de/ie/boss_gm_800_guitar_synthesizer_reviews.htm ,
  https://www.soundonsound.com/node/4921062?page=2 ,
  https://www.sonuus.com/products_g2m.html , https://www.adorama.com/soi2mmscprt.html ,
  https://musictech.com/reviews/software-instruments/jamstik-studio-creator-best-midi-guitar/
- Benchmarks: https://archives.ismir.net/ismir2018/paper/000188.pdf ,
  https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2024/RileyEtAl-ISMIR-2024.pdf ,
  JAMRACK `docs/guitar-to-midi.md` and `docs/polyphonic-plan.md` (commit bcbb30d)
