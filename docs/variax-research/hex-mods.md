# Extracting the six piezo signals from a Line 6 Variax (300 first) — documented mods and the piezo-hex ecosystem

Research note, 11 November 2026. Scope: what people have actually done to get
six per-string signals out of a Variax, what the manufacturers say, which
off-the-shelf hex preamps a six-saddle set can feed, and the electrical facts a
soldering plan needs. Every claim carries a source; the legend below says how
much weight each one bears.

Legend used throughout:

- **[DONE]** — reported by a named person who built it and used it.
- **[MFR]** — manufacturer document or official support answer.
- **[FORUM]** — forum statement by someone who did not report building it
  (opinion, proposal or second-hand).
- **[SNIPPET]** — page I could not open (HTTP 403 / blocked host); the fact
  comes only from a search-engine excerpt and should be re-read by a human.
- **[MY ANALYSIS]** — my own reasoning from the facts above; not sourced.

---

## 0. Short answer

1. **Nobody has published a step-by-step, reproducible "Variax 300 → six
   buffered string outputs" mod with photos and schematic.** The closest is one
   builder's description from 2008 (§2.1) plus several proposals.
2. **The one documented working approach taps the six signals *after* the
   Variax's own per-string input buffers and *before* its A/D converters.**
   Those points carry a +2.5 V DC offset (single-supply converters); the builder
   added one 100 Ω series resistor per string, tied the Variax ground to the
   hex-device ground and relied on the DC-blocking capacitors in the receiving
   Roland unit. The Variax kept working. [DONE, one report, 2008]
3. **Passively Y-splitting the raw saddles between the Variax board and a
   Hexpander does not work when both are powered**; Graph Tech says so
   officially and two builders confirmed it. [MFR] [DONE]
4. **RMC sold a small splitter board ("VAR-01 / VAR001") for exactly this**:
   hex-to-MIDI side works, but two builders report the Variax side loses its
   low end (everything under ≈ 200 Hz) and sounds bad. [DONE ×2]
5. **There is a factory precedent that hex preamp → Variax works**: the Parker
   Adrian Belew Signature Fly used six RMC piezo saddles feeding an RMC
   Poly-Drive I, whose outputs go both to the 13-pin jack and to a Variax
   module. [MFR, via second-hand copies of the owner's manual; I could not open
   the PDF itself.]
6. **Line 6's own service engineer says: "Not recommended, so don't do it",
   "Very difficult, even for me"**, citing input impedance, noise and supply
   loading. [MFR-staff, forum]
7. A raw piezo saddle **must see a very high impedance (≥ 1 MΩ, preferably
   ≥ 10 MΩ) or it loses its bass**; a GK-style 13-pin breakout expects
   *buffered, low-impedance* signals. So a passive tap of the saddles into a GK
   breakout **without a preamp will not work properly** (§6.4).

---

## 1. What is inside a Variax 300 (and the 1st-generation family)

### 1.1 Saddles

- The stock piezo elements are **L.R. Baggs "X-inserts"**, one per saddle. They
  have **a single HOT wire only; the ground return goes mechanically through
  the saddle / set screws / bridge**. [FORUM, codamedia, Line 6 Community,
  2 Nov 2018: "The piezos are called 'x-inserts' and they are made by LR
  Baggs" … "only have a HOT wire while the ground connects through the set
  screws"] (S1)
- Same fact stated by the maker of a Variax 300 upgrade kit: "the original
  LRBaggs piezo elements do not have a 'ground' wire, and connect the common of
  the circuit through the bridge and saddle, mechanically. This can (and does)
  lead to intermittent output". [MFR, H&H Guitars V300GU-X instructions] (S2)
- Line 6 replacement part for the 300/500/700 electric: **11-11-0001** (third
  party listings). [SNIPPET] (S3)

### 1.2 Bridge PCB and harness (Variax 300e hardtail)

- "the piezo elements (pickups) are connected to the Main Printed Circuit Board
  (MPCB) by the use of a Bridge PCB and discrete wiring. **Eight individual
  conductors are used, as there are signals for 6 pickups and a 'ground' (audio
  circuit common) connection that uses 2 conductors.** This is necessary to send
  hexaphonic (6 individual audio) signals to the MPCB." [MFR, H&H Guitars] (S2)
- The bridge is removed with 4 screws (not the 5th, rear-center one); the piezo
  wiring connector is unplugged "by pulling straight with the wire angle"; the
  connector is polarized. [MFR, H&H] (S2)
- A Line 6 forum poster says the 300 has an actual wire harness while the
  500/600/700 use a flexi ribbon cable to the main PCB. [SNIPPET] (S4)
- leesteel (Variax 300 Ghost install, 4 May 2016) soldered a copper-strip
  ground plane to "a small exposed ground area beside the **8-pin connector**"
  on the bridge PCB. [DONE] (S5)
- On a Variax 700 (not 300), the per-string inputs on the Line 6 main PCB are
  labelled **A–F, A = high e**; a tech soldered six Ghost signal wires plus one
  ground directly to those pads, bypassing the flex connector: "those 7 wires (6
  strings plus a ground)". [DONE, thermalrelief.com TTA002] (S6)

### 1.3 Analog front end and conversion

- Line 6 / Sound On Sound (June 2003 review of the original Variax): "The string
  vibrations are detected by the hexaphonic bridge pickup" and are "converted
  directly into digital signals through **six 24-bit A-D converters**". [MFR-ish,
  SOS review] (S7)
- Per-string buffer exists on the board: "Piezos require a charge amplifier to
  generate a signal so no you can't take a signal right from the Piezo. You would
  have to take it after the amplifier that is on the board. There is one for each
  piezo." [FORUM, Charlie_Watt, Nov 2016] (S8); "The piezos need a very high
  impedance charge amp buffer before being summed together." [FORUM,
  Charlie_Watt, Oct 2013] (S9)
- The converters run from a **single supply, so the buffered signals sit on a
  +2.5 V offset**: "the A/D inputs have a 2.5V offset since the Variax
  converters are not using a bipolar supply". [DONE, Dave Ryan 2008] (S10)
- A Variax 300 repair thread confirms the chain "piezo input → op-amps → ADC":
  "Audio passes properly from the piezo input through the op-amps to the ADC".
  The schematic the poster had was for board **35-00-0137** while his board was
  **35-00-0047** with a 24-pin (not 32-pin) harness, so there are at least two
  main-board revisions; the schematic link was removed by moderators as
  copyrighted. [DONE-diagnostic, Line 6 Community, 2018] (S11)
- Supply: 6 × AA (9 V), or 9 V on the ring of the TRS jack from the XPS box, or
  over the RJ-45 digital cable. [MFR/FORUM] (S7, S12)
- Line 6 does not publish schematics; service documentation goes to authorized
  service centers only. (S11)

---

## 2. Documented attempts to get six signals out of a Variax

### 2.1 Tap after the Variax buffers, before the ADCs — Dave Ryan, 2008 [DONE]

Source: midiguitar Yahoo group post, 9 May 2008, "Left Handed Steinberger Spirit
Variax conversion" (S10). Donor: **a Variax 300**. Verbatim key points:

- "I used a Variax 300 for the donor."
- He "tapped into the piezo signals at the point after they are buffered by the
  Variax electronics" and before the A/D converters.
- "the A/D inputs have a 2.5V offset since the Variax converters are not using a
  bipolar supply."
- "I only had to add a 100 Ohm resistor to each signal" before the 13-pin DIN.
- "You must connect the Variax supply ground to the Hex device ground at some
  point."
- The hex device input "must have some form of DC blocking"; the Roland GR-30
  and GI-10 he used "both use bipolar electrolytic capacitors".
- The guitar was "playable"; the stock magnetic pickups were kept as a fallback
  "if the Variax electronics ever go down" (implying the Variax side stayed in
  use). No measurements, no photos, no board location of the tap points.

The same approach is described in vguitarforums thread 761 "Variax Conversion
(W/ GK13 Pin Output)" (poster Elantric): tap "after the Variax buffering and
before the A-to-D converters", "100 ohm resistor per line", "2.5V offset on those
signals, because the Variax converters don't use a bipolar supply"; it also
mentions the **RMC VAR-01** as "a rare buffered dual-output interface for old
Variax bridge piezo cables". [SNIPPET — vguitarforums returns HTTP 403 to this
session; I could not read the thread] (S13)

What this gives you [MY ANALYSIS]: six already-buffered, low-ish impedance
signals (the Variax's own op-amps do the hard part), at the cost of opening the
guitar, finding six test points on an undocumented board (two revisions), and
accepting a 2.5 V DC offset that every downstream input must block. Reversible
only if you solder to pads/vias you can clean. Not tested on the 300 beyond one
2008 report.

### 2.2 Proposal on the Line 6 support archive (thread 23857) [SNIPPET, unverified]

Search excerpts from a Line 6 Support Archive thread (300/600/700 section)
describe: take "all these six piezo signal plus a ground (a good place to take
tap the ground is the negative leg of the biggest capcitor - **C94**) & feed
them on to your own circuit on breadboard"; first component "150R(ish)
resistor, then an electrolytic DC blocking capacitor", then a simple op-amp
buffer per channel. Another excerpt says a modder "bypassed the two op-amps and
wired small cables direct to the ADC chips and added a 13-pin **input**" (the
reverse direction: feeding a GK pickup into the Variax). The archive URL now
redirects to the forum index, so none of this could be verified; component
references may not match your board revision. (S14)

### 2.3 Passive Y-split of the raw saddles to a Hexpander — does not work [MFR] [DONE]

- Graph Tech, official FAQ "Can I run both the Variax and the Hexpander off the
  same set of saddles?": customers who tried a simple Y-connection found the two
  circuits interfere when both are powered; if either is off the other works
  fine; cause given as loading / impedance mismatch; a buffer between pickups and
  circuits might solve it "but no such solution had been found". (S15)
- Lazlotronic (JTV-69S "Hexstainocaster", 2021 blog): "There was an apparent
  impedance and loading mismatch so that the piezos could only drive one circuit
  or the other." He then considered an active six-channel buffer: "It would use
  too much power to run off the Variax power buss" and "there simply wasn't room
  in the guitar cavity"; he abandoned the shared-piezo idea and installed an
  internal Roland GK-3 kit instead ("far more elegant than hanging a GK3 off
  the guitar"). [DONE] (S16)

### 2.4 RMC VAR-01 / VAR001 splitter board [DONE ×2, negative on the Variax side]

- Lazlotronic: "RMC actually sold an inexpensive passive buffer circuit that
  should - in theory - allow me to split" the piezo outputs; "The RMC board
  seemed to remove everything below about 200hz and that made everything sound
  terrible"; "the hex system worked reasonably well". [DONE] (S16)
- vguitarforums thread 36676 "Piezo splitting buffer board design": "RMC has a
  small board called the VAR001 … It didn't work for me. The hex to midi worked
  fine, but the sonics of the Variax side was suboptimal"; another poster guessed
  it is passive. [SNIPPET] (S17)
- Line 6 Community (cags12, 31 May 2023): "RMC pickups sell a part VAR001 which
  is a buffer to split the signal to the Variax and a GK type pre-amp"; it was
  used on Adrian Belew's guitar. codamedia (7 Jun 2023): "The blogger said the
  RMC degraded the model tone of the JTV too much to continue." [FORUM] (S18)
- Contradiction to note: one description calls it "active buffered dual
  outputs" (S13 snippet), another "passive" (S16, S17). RMC's current products
  page does not list it. (S19)

### 2.5 Factory precedent: Parker Adrian Belew Signature Fly [MFR, second-hand]

- Owner's manual (copies on flyclone.com and manuals.plus, both HTTP 403 to this
  session; facts from search excerpts): six **RMC piezo transducers** on the
  bridge feed an **RMC Poly-Drive I**, which drives synths / VG units through
  the **13-pin output**; a custom external power regulator sends **+8 VDC down
  pin 9** (otherwise unused) to power the **Variax module**; only the Variax
  output depends on that supply. [SNIPPET of MFR] (S20)
- Fly Clone forum: signal path "Piezo saddles > RMC PolyDrive I > Variax
  circuitry > …"; "the Poly-Drive preamp drives the acoustic, 13-pin, and Variax
  circuits without separate buffers". [FORUM] (S21)
- Line 6 service engineer psarkissian: Belew's Fly has "an early Variax 500 or
  700 processor board", so JTV boards "might not be compatible". [MFR-staff]
  (S18)

Why it matters [MY ANALYSIS]: Parker/RMC/Line 6 shipped a product in which a
13-pin hex preamp's buffered outputs are what the Variax board sees. So the
Variax input stage *can* be driven by a Poly-Drive-class buffered signal — but
the level/offset matching they used is undocumented, and it was a 500/700 board,
not a 300.

### 2.6 Reverse direction, for information: RackVax GK→Variax adapter [FORUM/SNIPPET]

A small company sold a module that plugs **in place of the Variax bridge
connector** ("disconnect your Variax guitar's bridge connector cable and install
in its place the appropriate jumper cable") and conditions a Roland GK pickup's
six signals for the Variax DSP, because "the voltages from the piezo bridge
differ from the GK output" and a direct connection could damage the board; it
has a GK THRU to drive a VG-99 simultaneously; it was built from donor Variax
500 DSP boards. (S22, S23) This confirms the bridge connector is the hex entry
point and that GK-level signals are *not* a drop-in match for what the Variax
front end expects.

### 2.7 Switch the raw saddles between Variax and a 1/4" jack [FORUM proposal only]

johnnyayyy (Line 6 Community, 8 Oct 2013) proposed a 6PDT slide switch or
relay: saddle hot wires to the common poles, one side to "the normal
connections on the variax board", the other side summed to the hot of the 1/4"
jack. snhirsch warned that ordinary relays are a poor choice for raw piezo and
to use hermetically sealed units. Nobody reported building it; it is not
simultaneous and it sums the strings. (S9)

### 2.8 Line 6's position [MFR-staff]

- silverhead (Line 6 Expert): "No there is no feature in Variax that allows you
  to separate the string outputs."
- psarkissian (Line 6 Service Engineer Moderator): tap "before it gets to
  processor board, and not mess up the input impedance in that signal line … Not
  recommended, so don't do it."; "Very difficult, even for me."; "there are
  impedance issues, along with signal and noise issues to overcome", risk "of
  pulling down supply voltages"; "It's touchy stuff, easy to make it all FUBAR."
  (Nov 2016 – Apr 2018) (S8)
- On GK-3 + JTV (2023–2024): "GK3 on JTV's seem to work fine … They can't be
  incorporated into the JTV circuit or signal path." (S18)

---

## 3. Replacing the Variax 300 saddles with Graph Tech Ghost saddles

- Graph Tech, official: "You can replace the saddles on your Strat-style Variax
  bridge with Graph Tech Ghost saddles (**PN-8000-00**)", which contain "Graph
  Tech's custom-made piezo sensor". [MFR] (S24)
- Graph Tech, official: Variax saddles are "electrically compatible with the
  Ghost preamp and MIDI interface" but lack the plug-in connectors; use
  extension harness **BE-0511-00** and solder. [MFR] (S25) — i.e. stock Baggs
  inserts can feed a Hexpander (one system at a time, see §2.3).
- Ghost electric saddles: insulated wires, **blue = ground, silver = signal**;
  do not file the notches (piezo 0.040" below). [MFR, Ghost saddles FAQ] (S26)
- **H&H Guitars V300GU-X** (Texarkana, AR): purpose-made kit for the 1st-gen
  Variax 300e: a replacement **bridge PCB (V300S) with a ground pad per
  saddle**, instructions, options from $17.50 (PCB only) to $250 (complete
  assembled replacement bridge, 6-month warranty). Instructions describe cutting
  the Ghost 2-pole connectors, separating the coaxial shield (ground) and centre
  (signal), soldering to the new PCB, re-using the original 8-conductor harness
  connector. [MFR] (S2)
- Community installs: leesteel did a Variax 300 (4 May 2016) and a 500;
  Miserable-sax (Dec 2019, 500) measured on a scope **"output 3 to 4 times the
  stock level with no clipping"**; Charlie_Watt: Ghost "produce much more
  output … lower the gain in Workbench to avoid saturating the DSP". [DONE] (S5)
- Price: Graph Tech lists 6-piece Strat sets around **$137.45**. [SNIPPET] (S27)

Relevance for a hex tap [MY ANALYSIS]: Ghost saddles give you a proper ground
wire per string and a cleaner path to a Hexpander/Poly-Drive; they do not by
themselves solve the "two circuits on one saddle" problem.

---

## 4. The piezo-hex ecosystem a six-saddle set can feed

### 4.1 Graph Tech Ghost **Hexpander** [MFR]

- Function: six-channel piezo preamp that puts buffered string signals on a
  13-pin jack for Roland/Axon-type converters. Saddle wires go into "the pin
  pairs labeled 1, 2, 3, 4, 5, 6"; a separate connector for **S1 and S2**
  (program up/down); volume pot on **VO and VI** pins, 250 kΩ linear. (S28,
  S29)
- **Power**: "The Hexpander does not require a 9v battery because it will get
  power from the MIDI converter through the 13-pin jack." (S28) → it needs the
  ±7 V from a 13-pin host; without a host you must supply those rails yourself.
- **Traktion switch** on the jack plate: Roland or Axon output optimisation;
  "If your converter is a Yamaha or another brand, try both positions". (S28)
- **Mag-to-pin-7 harness** (optional): sends the magnetic/acoustic signal down
  pin 7. (S28)
- 13-pin jack: separate board, rectangular routed hole + 4 screws, ribbon
  cable to the Hexpander, keyed header. (S28)
- Board 1-5/8" × 1-1/8", ≈ 3/8" tall. (S29)
- Not published: output level, output impedance, pinout of the 13-pin board
  (assumed Roland-standard, §6.3).
- **Prices (Nov 2026)**: Basic kit PE-0440-00 **$208.95** (Graph Tech),
  $177.61–229.99 elsewhere; Advanced kit PK-0440-00 **$296.95** (Graph Tech,
  shown sold out), $252.41 Sweetwater special order; UK £209–249; thomann.ae
  ≈ €293. [SNIPPET] (S30)

### 4.2 RMC **Poly-Drive I** (internal) / II / IV [dealer listing]

- Poly-Drive I: "super-compact" 0.9" × 1.25" × 1.5", mounts inside the guitar;
  each of six string signals "preamplified & equalized separately"; **buffered
  string signals on DIN-13 pins 1–6, mono guitar on pin 7**; Saddles Mix summed
  signal; mag buffer; **phantom power via the 13-pin jack or onboard battery**;
  ≈ 90 dB S/N; controls on ribbon cable (blend, volume, tone, synth volume,
  Sw1/Sw2, Synth/Mix/Guitar). Price **$380** (Sound Island Music). Designed for
  RMC saddles; Graph Tech saddles are electrically similar piezo elements
  (unverified compatibility). (S31)
- Poly-Drive II: external box, 6 channels, 9 V battery or **±7 V bipolar
  phantom**, 4.5" × 3.5" × 2.25". (S32)
- Poly-Drive IV (PD403): onboard, 1/4" + DIN-13, battery box. (S32)
- Forum claim: Poly-Drive string buffers are low impedance with crosstalk
  "well below −90 dB up to 1 kHz on the DIN-13 cable". [SNIPPET] (S17)
- RMC's own site lists the products but no specs or prices; contact
  info@rmcmusic.com. (S19)

### 4.3 Cycfi Research **Nu Multi** + **Nexus-GK** (magnetic alternative) [MFR]

- Nu Multi: active, low-impedance coils (400 Ω DC), one differential low-noise
  preamp per coil, permalloy ring shields against crosstalk, 20 Hz–20 kHz;
  Nu Multi 6 **$199.99**. [SNIPPET of cycfi.com; product pages render empty to
  this session] (S33)
- Nexus-GK (breakout box, v2.5 manual read in full): 19-pin LEMO guitar
  connector, 12 audio outputs, MIDI DIN, and a **13-pin Roland-compatible
  DIN**; "the audio coming from the Nu Multichannel pickup is attenuated by
  **−6 dB** before going to the GK 13-Pin DIN to match the lower input-level
  requirements of Roland compatible devices"; supports SW1/SW2 and SY-VOL (not
  MIX); powered by a 12–18 V supply; open-source firmware (MSP430). GK Set
  **$135.99**. (S34, S35)
- For our purpose the interesting part is the 12 individual analog outputs
  (no 13-pin host needed) — but it is magnetic, not the Variax piezos.

### 4.4 Roland **GK-3** (reference) and **GK-5**

- GK-3 owner's manual: "Power to the GK-3 is supplied by the connected
  GK-compatible device via the GK cable. No battery or other power source is
  required." Connectors: GK connector, normal pickup input jack; controls GK
  volume, S1/S2, select switch; 60 g. (S36) Street price ≈ $250. [SNIPPET]
  (S37)
- Roland support: GK-5 vs GK-3 — communication **digital (serial GK, TRS
  cable BGK-x)** vs **analog (13-pin)**; GK-5 is designed for the GM-800 /
  newer BOSS units and is not compatible with 13-pin cables. [SNIPPET; Roland
  support page HTTP 403] (S38) → GK-5 gives no analog per-string outputs; GK-3
  (or an internal GK-KIT-GT3) does.

### 4.5 Fishman **TriplePlay** (hex magnetic, MIDI out)

- Fishman support page: hexaphonic magnetic pickup, requires metal strings;
  **pitch-detection latency 7 ms (high notes) to 14 ms (low notes)**; overall
  10–17 ms on Mac/PC, 18–25 ms on iOS; MIDI over USB (Express/Connect) or
  proprietary wireless; each string on its own MIDI channel in Mono Mode (ch 1–6);
  no audio or per-string analog output is described; no API/raw data. (S39)
- Express USB-C ≈ **$219.95**; Wireless ≈ $419–470. [SNIPPET] (S40)
- What it would make of our software [MY ANALYSIS]: nothing to process — it
  replaces the tracker and emits MIDI directly. Its value to Jamrack is as a
  **benchmark** (latency and accuracy per string) and as the commercial reference
  the plugin study already compares against; it cannot feed the Variax piezos or
  our DSP.

### 4.6 Prices at a glance (Nov 2026, USD unless noted, all [SNIPPET])

| Item | Price | Note |
|---|---|---|
| Graph Tech Ghost saddles PN-8000-00 (6) | ≈ 137 | fits Strat-style Variax bridge (S27) |
| H&H V300GU-X PCB only / full bridge | 17.50 / 250 | Variax 300e specific (S2) |
| Graph Tech Hexpander basic / advanced | 209 / 297 | needs ±7 V from host (S30) |
| RMC Poly-Drive I | 380 | internal, phantom or battery (S31) |
| Cycfi Nu Multi 6 + Nexus-GK set | 200 + 136 | magnetic (S33, S35) |
| Roland GK-3 | ≈ 250 | magnetic, host-powered (S37) |
| Fishman TriplePlay Express | ≈ 220 | MIDI only (S40) |

---

## 5. Where each mod taps, in one table

| Approach | Tap point | Variax keeps working? | Simultaneous? | Status |
|---|---|---|---|---|
| Dave Ryan 2008 (§2.1) | after Variax buffers, before ADC; +2.5 V offset; 100 Ω series | yes (reported) | yes | DONE, 1 report, no schematic |
| Line 6 archive proposal (§2.2) | same points; 150 Ω + electrolytic + op-amp buffer; ground at C94 | presumably | yes | SNIPPET, unverified |
| Passive Y of saddles to Hexpander (§2.3) | raw saddle wires | **no** while Hexpander powered | **no** | MFR + DONE: fails |
| RMC VAR-01 splitter (§2.4) | raw saddle wires → board → both | degraded (< 200 Hz lost) | yes | DONE ×2, negative |
| Belew Fly design (§2.5) | RMC saddles → Poly-Drive I → 13-pin AND Variax | yes (factory) | yes | MFR, 500/700 board |
| 6PDT switch (§2.7) | raw saddle wires | yes, when switched back | no | proposal only |
| Separate GK-3 / internal GK kit (§2.3) | none (magnetic) | yes | yes | DONE, Lazlotronic + others |

---

## 6. Electrical facts a soldering plan needs

### 6.1 The piezo element and its buffer

- A piezo pickup behaves as a **capacitor**: measured values **1.5 nF** (Fender
  "Viper" bridge piezo) and **450 pF** (Adamas SMT); with the amplifier input
  resistance it forms a first-order high-pass. [Zollner, *Physics of the Electric
  Guitar*, ch. 6, measured] (S41)
- Numbers: "With C = 1,5 nF and R = 1 MΩ, we obtain fg = 106 Hz … For R = 50 kΩ
  (line input), the cutoff frequency would rise to 2,1 kHz, corresponding to a
  complete loss of the lows." A 500 kΩ load on the Viper piezo gave a 220 Hz
  corner; 2 MΩ on the SMT gave 177 Hz. (S41)
- Rod Elliott (ESP Project 202): input impedance "needs to be somewhere between
  'high' (1 MΩ or so) to exceptionally high (greater than 100 MΩ)"; a 200 pF
  element needs "at least 20 MΩ to get a −3 dB frequency of 40 Hz"; FET-input
  stage "almost without exception" (OPA2134 suggested; TL072 not for a single
  9 V); charge-amplifier alternative whose gain is set by piezo-C / feedback-C,
  input near 0 Ω so cable capacitance and hum are largely removed. (S42)
- **Cable capacitance**: for a passive piezo it acts as a capacitive divider →
  **level loss, not treble loss** ("The ubiquitous assumption that a long cable
  would attenuate in particular the treble range does not hold for the piezo
  pickup", Zollner; "does not normally affect the frequency response", Elliott);
  it also lowers the high-pass corner slightly. Keep raw piezo leads short and
  buffer inside the guitar. (S41, S42)
- **Signal level**: tens of mV for light playing up to > 1 V peak on hard
  attacks (Elliott's bench numbers: ≈ 2 mV light tap, "over 1V peak is easy to
  achieve"). Ghost saddles gave 3–4× the stock Baggs level in a Variax. (S42, S5)
- Stock Variax saddles have **no ground wire** (§1.1); any buffer you add must
  take its reference from the bridge/ground plane, and the H&H PCB or Ghost
  saddles give you a real return per string.

### 6.2 What the Variax front end presents

- Per-string high-impedance buffer/charge amp, then single-supply ADC with a
  **+2.5 V** bias on the buffered node (S8, S10). Nothing published on gain or
  swing; forum guess "the piezo's outputs are amplified a lot" (brue58ski, S18).
- Loading a buffered op-amp output with a 100 Ω series resistor into a Roland
  GK input worked (S10); loading the *raw saddle* with a second circuit did not
  (S15, S16).

### 6.3 Roland 13-pin (GK) connector pinout

Primary-quality sources: S. Flamisch's 2014 pinning sheet (S43) (which also
gives Roland cable colours and the newer GK-3 pin-9 voltages) and johnp.net's
table (S44), which cites Roland service manuals via Mark Glinsky / Wayne Joness.
Both agree; the vguitarforums FAQ (S45, snippet) agrees and adds current.

| Pin | Function | Notes |
|---|---|---|
| 1 | String 1 (high E) | buffered, low-impedance analog |
| 2 | String 2 (B) | |
| 3 | String 3 (G) | |
| 4 | String 4 (D) | |
| 5 | String 5 (A) | |
| 6 | String 6 (low E) | |
| 7 | Normal guitar signal | grounded when mode switch = "Synth only" (Flamisch) |
| 8 | Synth (MIDI) volume | 0–5 V DC control voltage; grounded = volume off |
| 9 | GK-3 mode-switch CV | 0.521 V Guitar / 1.042 V Mix / 1.563 V GK (±5 %); "often left unconnected"; Belew Fly repurposes it for +8 V to the Variax |
| 10 | S1 / Down | active low (grounded when pressed) |
| 11 | S2 / Up | active low |
| 12 | **+7 V** | FAQ: ≈ 100 mA [SNIPPET] |
| 13 | **−7 V** | FAQ: ≈ 100 mA [SNIPPET] |
| Shell | **Ground** | the only ground path; no numbered pin is ground |

- "Analog signals on Pins 1 to 6 and Pin 7 are buffered by preamps and feed the
  cable with low impedance." [Flamisch] (S43)
- Caution from the FAQ: forcing a misaligned plug shorts the ±7 V rails and
  has "fried" processors; Flamisch suggests steering diodes on pins 12/13 in
  adapters. [SNIPPET] (S45)
- GK-1 era used ±15 V on a 24-pin connector; irrelevant here but explains some
  old schematics. (S44)
- **Not found**: an official Roland figure for the GK-IN input impedance or
  nominal per-string level. The Nexus-GK manual's −6 dB pad "to match the lower
  input-level requirements of Roland compatible devices" (S34) is the only
  manufacturer hint. VG-99 publishes only its 1/4" guitar input (−10 dBu,
  2.2 MΩ), not the GK input. (S46)

### 6.4 Would a passive tap of the Variax saddles into a GK-style breakout work without a preamp?

**No, not usefully.** [MY ANALYSIS from sourced facts]

1. A GK host or breakout expects *buffered, low-impedance* lines (S43); a bare
   piezo into a low-impedance input loses its bass (50 kΩ → 2.1 kHz corner,
   S41). Even a 1 MΩ audio-interface instrument input gives ≈ 100 Hz corner on
   a 1.5 nF element and higher on smaller elements — borderline for low E
   (82 Hz) and worse after cable capacitance divides the level.
2. If the Variax board stays connected to the same saddles, the second load
   interferes with the Variax (S15, S16) — manufacturer and two builders agree.
3. The stock saddles have no ground wire (S1, S2); a passive breakout would
   rely on the bridge for return, with the intermittent-contact problem H&H
   describes.
4. Therefore every working or factory solution inserts a buffer: the Variax's
   own op-amps (S10), a Poly-Drive I (S20), a Hexpander (S28), or a DIY FET/op-amp
   stage (S42). The cheapest *documented* path that keeps the Variax alive is
   §2.1; the cleanest *documented* path is Belew's (hex preamp first, Variax
   second), which nobody has reproduced on a 300.

### 6.5 Receiving six analog channels on the Jamrack side [MY ANALYSIS]

The project does not need a 13-pin host; it needs six AC-coupled line/instrument
inputs. Points to settle before soldering: whether the chosen interface inputs
are AC-coupled (the 2.5 V offset of §2.1 otherwise appears as DC); the Gigcaster
5 does not have six analog inputs, so a 6-in interface or a 13-pin → 6 × 1/4"
breakout (DIY designs exist on vguitarforums, e.g. "Bill Bax GK13 Breakout box",
S47 [SNIPPET]) would be required; and a ±7 V supply if a Hexpander or
Poly-Drive is used without a Roland host.

---

## 7. What I could not read (and what a human should open)

- **vguitarforums.com** returns HTTP 403 to this session for every thread
  (761, 2359, 6212, 3927, 11582, 12657, 36676, 5213, 19481, 1969); web.archive.org
  is blocked. Threads 761 (Elantric's Variax→GK13 conversion), 6212 (Les Paul
  Tyler Variax with 13-pin), 36676 (buffer board design) and 2359 (GK DIN
  pinouts) are the ones most worth reading in full.
- **support.graphtech.com** does not resolve (not in the allowed network list);
  the same FAQ was read from help-center.graphtech.com.
- **flyclone.com** PDF and forum, **manuals.plus**, **gearspace.com**,
  **Roland support** (GK-5/GK-3 page) and the **Cycfi product pages** (empty
  render) returned 403/empty; their facts come from search excerpts.
- The **Line 6 Support Archive thread 23857** (C94 / 150R proposal) now
  redirects to the forum index.
- No Variax 300 schematic or board photo could be obtained; the one poster who
  had a schematic (board 35-00-0137) was not allowed to share it.

---

## Sources

- S1 — Line 6 Community, "300 on the way... what's going to work with it?" (codamedia, 2 Nov 2018): https://line6.com/support/topic/36781-300-on-the-way-whats-going-to-work-with-it/
- S2 — H&H Guitars, "V300GU-X Graph Tech 'Ghost' Piezo Upgrade for the 1st Generation Line6 Variax 300e — Assembly Instructions / Options" (PDF, doc. V300GU-XAI25-0914): https://img1.wsimg.com/blobby/go/0079147b-b95d-419b-bef2-66012917280e/downloads/540cea35-4dda-4cea-9544-7779d8fa58b8/25-0914%20V300GU-X%20Instructions.pdf?ver=1757865112005 (local text copy: raw/v300gu-x.txt)
- S3 — Full Compass listing, Line 6 11-11-0001 piezo for Variax 300/500/700: https://www.fullcompass.com/prod/114557-line-6-11-11-0001-piezo-pickup-for-variax-300-500-700 [snippet]
- S4 — Line 6 Community, "Variax 300 piezo died (in Australia)…": https://line6.com/support/topic/69967-variax-300-piezo-died-in-australia-are-replacements-available/ [snippet]
- S5 — Line 6 Community, "Ghost Installation Step-By-Step Variax 500" (leesteel, Charlie_Watt, Miserable-sax, 2015–2019): https://line6.com/support/topic/12834-ghost-installation-step-by-step-variax-500/
- S6 — Thermal Relief, "TTA002" (Variax 700 rewire, main PCB pads A–F): https://thermalrelief.com/tta002/
- S7 — Sound On Sound, "Line 6 Variax" review, June 2003: https://www.soundonsound.com/reviews/line-6-variax
- S8 — Line 6 Community, "Splitting piezo string outputs" (2016–2018, incl. psarkissian): https://line6.com/support/topic/23735-splitting-piezo-string-outputs/
- S9 — Line 6 Community, "Raw Piezo Sound" (2013–2016): https://line6.com/support/topic/3119-raw-piezo-sound/
- S10 — midiguitar Yahoo group (narkive mirror), Dave Ryan, "Left Handed Steinberger Spirit Variax conversion", 9 May 2008: https://midiguitar.yahoogroups.narkive.com/bl9IUc2N/left-handed-steinberger-spirit-variax-conversion
- S11 — Line 6 Community, "Variax 300 Repair (no output)" (2018–2023): https://line6.com/support/topic/37498-variax-300-repair-no-output/
- S12 — S. Flamisch, "Connector replacements and pinnings for Roland GK-Kit and Line 6 – Variax" (2014, PDF; Variax TRS/XLR power wiring section): http://www.flamisch.at/joomla/images/downloads/Connectors%20and%20Pinnings%20for%20GK-System%20and%20Variax.pdf (local text copy: raw/flamisch.txt)
- S13 — vguitarforums, "Variax Conversion (W/ GK13 Pin Output)": https://www.vguitarforums.com/smf/index.php?topic=761.0 [HTTP 403; snippet only]
- S14 — Line 6 Support Archive thread 23857: https://uk.line6.com/supportarchive/thread/23857 [redirects to index; snippet only]
- S15 — Graph Tech Help Center, "Can I run both the Variax and the Hexpander off the same set of saddles?": https://help-center.graphtech.com/en-US/can-i-run-both-the-variax-and-the-hexpander-off-the-same-set-of-saddles-758672
- S16 — Lazlotronic blog, "Studio upgrade 2021" (Variax Hexstainocaster project): https://lazlotronic.com/blog/studio-upgrade-2021
- S17 — vguitarforums, "Piezo splitting buffer board desing": https://www.vguitarforums.com/smf/index.php?topic=36676.0 [HTTP 403; snippet only]
- S18 — Line 6 Community, "Fitting a GK3 to aJTV89F" (2023–2024): https://line6.com/support/topic/66521-fitting-a-gk3-to-ajtv89f/
- S19 — RMC Pickup Co., products page: https://www.rmcpickup.com/products.html
- S20 — Parker "Adrian Belew Signature Fly Owner's Manual": https://flyclone.com/_files/Manuals/Parker_Belew_Fly_Handbook.pdf and https://manuals.plus/m/e81d781320a8f59d2f82bc27e3372c940a794e93c07f12ebe130e8fd56e19787 [both HTTP 403; snippets only]
- S21 — Fly Clone Project forum, "Adrian Belew Signature Fly Variax troubleshooting" and "Adrian Belew Project - Help Everyone": https://flyclone.com/viewtopic.php?t=1035 , https://flyclone.com/viewtopic.php?t=441 [HTTP 403; snippets only]
- S22 — vguitarforums, "RackVax GK 13-pin to Variax Adapter Module": https://www.vguitarforums.com/smf/index.php?topic=3927.0 [HTTP 403; snippet only]
- S23 — Line 6 Community, "Rackvax - Variax 13pin conversion module" and "Variax 600, Rackvax Gk12 In And Thru Home Build": https://line6.com/support/topic/66850-rackvax-variax-13pin-conversion-module/ , https://line6.com/support/topic/3248-variax-600-rackvax-gk12-in-and-thru-home-build/
- S24 — Graph Tech Help Center, "Can I replace the saddles on my Line6 Variax with ghost saddles?": https://help-center.graphtech.com/en-US/can-i-replace-the-saddles-on-my-line6-variax-with-ghost-saddles-758667
- S25 — Graph Tech Help Center, "Can I plug the pickup saddles from my Variax Line 6 into the ghost preamp?": https://help-center.graphtech.com/en-US/can-i-plug-the-pickup-saddles-from-my-variax-line-6-into-the-ghost-preamp-758671
- S26 — Graph Tech Help Center, "Ghost saddles FAQ": https://help-center.graphtech.com/en-US/articles/ghost-saddles-faq-206891
- S27 — Graph Tech, Ghost pickup systems (saddle sets, price): https://graphtech.com/collections/ghost-pickup-systems-guitar-saddle [snippet]
- S28 — Graph Tech, "Hexpander MIDI Interface / Acousti-Phonic Pre-amp User Guide" (PDF mirror at WD Music): https://www.wdmusic.com/media/specs/graphtech/HEX_INSTALL.pdf (local text copy: raw/hex-install.txt)
- S29 — Graph Tech Help Center, "Hexpander FAQ": https://help-center.graphtech.com/en-US/articles/hexpander-faq-206894
- S30 — Graph Tech store pages, Hexpander basic PE-0440-00 and advanced PK-0440-00: https://graphtech.com/products/ghost-hexpander-basic-preamp-w-traktion , https://graphtech.com/products/ghost-hexpander-preamp-kit ; Sweetwater: https://www.sweetwater.com/store/detail/HexpanderKAd--graph-tech-hexpander-midi-interface-kit-advanced ; Ivor Mairants (UK): https://ivormairants.co.uk/products/graphtech-ghost-preamp-kit-pe-0440-00 ; thomann.ae: https://thomann.ae/graph_tech_ghost_hexpander_preamp_kit_reviews.htm
- S31 — Sound Island Music, RMC Poly-Drive I listing: https://stores.soundislandmusic.com/rmc-poly-drive-i/
- S32 — Reverb listings, RMC Poly-Drive II and IV: https://reverb.com/item/2932428-rmc-poly-drive-ii-6-channel-polyphonic-midi-preamp , https://reverb.com/item/9826934-rmc-poly-drive-iv-pd403
- S33 — Cycfi Research, Nu Series / Nu Multi 6: https://www.cycfi.com/projects/nu-series/ , https://www.cycfi.com/product/nu-multi-6/ [pages render empty to this session; snippets]
- S34 — Cycfi Research, "Nexus-GK" manual v2.5 (PDF): https://www.cycfi.com/wp-content/uploads/2020/06/Nexus-GK.pdf (local text copy: raw/nexus-gk.txt)
- S35 — Cycfi Research, Nexus-GK GK Set and 2020 announcement: https://www.cycfi.com/product/nexus-gk-gk-set/ , https://www.cycfi.com/2020/06/announcing-the-nexus-gk/ [snippets]
- S36 — Roland, GK-3 Owner's Manual (PDF): https://static.roland.com/assets/media/pdf/GK-3_OM.pdf (local text copy: raw/gk3-om.txt)
- S37 — Sweetwater, Roland GK-3: https://www.sweetwater.com/store/detail/GK3--roland-gk-3-divided-pickup [snippet]
- S38 — Roland Support, "GK-5, GK-3: What are the differences…": https://support.roland.com/hc/en-us/articles/17092362697755-GK-5-GK-3-What-are-the-differences-between-GK-5-and-GK-3 [HTTP 403; snippet only]
- S39 — Fishman, TriplePlay Support: https://fishman.com/tripleplay-support/
- S40 — Sweetwater, Fishman TriplePlay Express: https://www.sweetwater.com/store/detail/TriplePlayExp--fishman-tripleplay-express-usb-c-midi-guitar-pickup [snippet]
- S41 — M. Zollner, *Physics of the Electric Guitar*, ch. 6 "Piezo pickups" (GITEC English translation, PDF): https://www.gitec-forum-eng.de/wp-content/uploads/2019/03/poteg-6-piezo-pickups.pdf (local text copy: raw/zollner-piezo.txt)
- S42 — R. Elliott (ESP), Project 202 "Piezo Pickup Preamplifiers": https://sound-au.com/project202.htm
- S43 — S. Flamisch, 2014 pinning sheet (same file as S12), "Roland GK-2/3 pin list (DIN 13-p)".
- S44 — johnp.net, "Roland guitar synth cable pinouts": http://johnp.net/projects/guitar-synth/roland-pinouts.html
- S45 — vguitarforums, "GK-13 Pin Cable FAQ" and "GK DIN Pinouts and DIY info": https://www.vguitarforums.com/smf/index.php?topic=19481.0 , https://www.vguitarforums.com/smf/index.php?topic=2359.0 [HTTP 403; snippets only]
- S46 — Roland VG-99 Owner's Manual, specifications (manualsdir mirror): https://www.manualsdir.com/manuals/195561/roland-vg-99.html?page=184 [snippet]
- S47 — vguitarforums, "Bill Bax GK13 Breakout box": https://www.vguitarforums.com/smf/index.php?topic=1969.25 [HTTP 403; snippet only]
- Also consulted: Line 6 Community "Piezo retrofit for electric guitars / 13 pin MIDI" https://line6.com/support/topic/19799-piezo-retrofit-for-electric-guitars-13-pin-midi-helix-variax-model-pack/ ; "Midi Ready Jtv?" https://line6.com/support/topic/3246-midi-ready-jtv/ ; Kemper forum thread https://forum.kemper-amps.com/forum/thread/16543-modifying-a-guitar-with-piezo-pickup-with-variax-normal-controls/ (no additional technical content).
