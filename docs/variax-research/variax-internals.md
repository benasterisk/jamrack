# Line 6 Variax 300 — internals and connectors (research note, 11 Nov 2026)

Scope: what is inside a first-generation Variax 300 (2005), how the six piezo saddles reach
the main board, what each external connector carries, and what documentation exists.
Purpose: decide where a per-string ("hex") tap could be taken for JAMRACK.

Legend: **[V]** verified in a primary document read in full (official Line 6 PDF, Line 6
schematic, vendor instruction sheet); **[F]** forum/community statement read in full;
**[S]** search-snippet only (page could not be opened); **[?]** contradictory or unverified.

Local evidence files (same folder as this note):
- `Line-6-Variax-Service-Manual.pdf` — 76-page Line 6 "Variax" (MG1 = Variax 500, 2002)
  parts list + mechanical assembly instructions + 12 schematic/PCB sheets. Downloaded from
  SynthXL (community mirror). The schematics carry a Line 6 proprietary notice.
- `sm-pages/` — rendered schematic sheets and zoomed crops (piezo input stages, harness
  pinout, AES/MIDI block, RJ-45 pinout).
- `variax300-manual.pdf` / `.txt` — official Variax 300 Pilot's Handbook (Line 6 CDN).
- `variax500-700-manual.pdf`, `variax600-manual.pdf` — official handbooks, for comparison.
- `v300gu-x.pdf` — H&H Guitars V300GU-X Ghost upgrade instructions for the Variax 300
  bridge (the only document found that describes the 300's bridge wiring explicitly).

---

## 0. Short answer for the owner

1. **Yes, the six strings are physically separate inside the guitar.** Each saddle holds one
   piezo element; six individual wires go to a small bridge PCB, then a wire harness (300)
   or flex cable (500/700) carries six signal lines to the main board, where each line has
   its own op-amp buffer and its own ADC channel (three stereo 24-bit codecs = 6 channels).
   The DSP processes strings separately (that is how alternate tunings work).
2. **No connector exposes them.** The 1/4" jack carries the analogue *sum* of the modelled
   sound; the RJ-45 "VDI" carries a *stereo AES/EBU digital audio stream of the modelled
   sum* plus MIDI and +7 V power. Workbench edits models over MIDI; it never streams
   per-string audio. Line 6 confirms there is no feature to separate the strings.
3. **People have tapped the six signals anyway.** The documented precedent (2008, Variax
   300 donor) taps each channel *after* the on-board op-amp buffer and *before* the ADC,
   adds a 100 Ω series resistor per string, and brings the six lines to a Roland 13-pin
   jack. The lines sit on a +2.5 V DC offset (single-supply converters), so the receiving
   device must AC-couple. Line 6's service engineer calls this "very difficult, even for
   me", and warns about impedance, noise and pulling down the supply rails.
4. **No Variax 300 schematic is public.** The only service document found is for the
   Variax 500 (MG1, main board 35-00-0137). The 300 uses a different board (owner reports
   35-00-0047, 24-pin harness; a 300/600 main PCB is sold as Line 6 50-04-0020) but the
   same signal topology (piezo → op-amp → ADC → DSP) is confirmed by a 300 repair thread.

---

## 1. Bridge, saddles and the path to the main board

### 1.1 Production dates and models
- Line 6's own timeline: Variax 500 in 2002, Variax 700 in 2003, Variax 300 and Variax 600
  (and Workbench) in 2005, Variax 300 Vintage Sunburst and Acoustic 300 in 2006, James
  Tyler Variax (second generation) in 2009. **[V]** https://line6.com/timeline/
- Wikipedia: Variax 300 released 2005, limited supply by 2007, discontinued early 2009;
  budget model made in Indonesia, agathis body, fixed bridge. **[F]**
  https://en.wikipedia.org/wiki/Variax
- Variax 300 Pilot's Handbook part number 40-00-0064 Rev A. **[V]**
  https://l6c-acdn2.line6.net/data/6/0a060b316ac34f05943d62560/application/pdf/Variax%20300%20User%20Manual.pdf

### 1.2 Who made the saddles
- The Line 6 service document for the MG1 (Variax 500) lists the bridge as
  `50-04-0004 BRIDGE ASSMBLY with Flex Cable — LR BAGGS X-BRIDGE CHROME`. **[V]**
  (`Line-6-Variax-Service-Manual.pdf`, parts list, p. 24 and p. 34 of the text dump)
- For the Variax 300, the H&H Guitars V300GU-X instructions state: "the original LRBaggs
  piezo elements do not have a 'ground' wire, and connect the common of the circuit through
  the bridge and saddle, mechanically." **[V]**
  https://img1.wsimg.com/blobby/go/0079147b-b95d-419b-bef2-66012917280e/downloads/540cea35-4dda-4cea-9544-7779d8fa58b8/25-0914%20V300GU-X%20Instructions.pdf?ver=1757865112005
- MusicRadar's 300 review (snippet only): the 300 "used the LR Baggs X-bridge, which is
  non-vibrato". **[S]** https://www.musicradar.com/reviews/guitars/line-6-variax-300-24422
- Line 6 forum (Variax 600 thread): "It is LR Baggs that makes them"; an owner bought
  replacement "X-Insert" piezos directly from L.R. Baggs. **[F]**
  https://line6.com/support/topic/42972-variax-600-uneven-sound-levels-on-piezo-pickups/
- Graph Tech sells drop-in Ghost saddles for the "Strat-style Variax bridge" (PN-8000-00),
  "with Graph Tech's custom-made piezo sensor for the Variax". **[V]**
  https://help-center.graphtech.com/en-US/can-i-replace-the-saddles-on-my-line6-variax-with-ghost-saddles-758667
- **Conclusion:** one L.R. Baggs piezo element per saddle on 300/500/600/700. The 300 and
  500 use the same hardtail X-Bridge family; the 600/700-tremolo use a vibrato version.

### 1.3 One element per string, and how the six wires reach the main board
- Variax 300 (H&H, read in full): "the piezo elements (pickups) are connected to the Main
  Printed Circuit Board (MPCB) by the use of a Bridge PCB and discrete wiring. **Eight
  individual conductors** are used, as there are signals for 6 pickups and a 'ground'
  (audio circuit common) connection that uses 2 conductors. This is necessary to send
  **hexaphonic (6 individual audio) signals** to the MPCB." Each original saddle has a
  single (hot) wire soldered to the bridge PCB; the harness from the main board plugs into
  a **polarized connector on the bridge PCB**, unplugged "by pulling straight with the wire
  angle". The bridge PCB is bolted to the bridge plate with an M3 screw (that is the ground
  return for the saddles). **[V]** (V300GU-X PDF, pages 2–5)
- Line 6 sells the 300's bridge board as `50-02-0049 Bridge PCB for Variax 300`
  (Full Compass listing, currently out of stock). **[S]**
  https://www.fullcompass.com/prod/249525-line-6-50-02-0049-bridge-pcb-for-variax-300
- Variax 300 Pilot's Handbook: "Never attempt to remove the bridge saddles. If any of the
  wires attached to the piezo elements are broken or damaged, the Variax will not function
  properly." **[V]** (variax300-manual.txt, p. 22)
- Forum reports on the 300's wiring: the saddle wires are "soldered to a pc-board and then
  a separate wire-harness runs back the Variax hardware" (as opposed to the 500's mylar
  flex); "On the 300 there's a screw that connects the PCB to the bridge plate for ground".
  **[S]** https://line6.com/support/topic/4879-how-do-the-piezos-ground/ and
  https://line6.com/support/topic/15233-variax-circuit-boards
- Original saddle wire colours (forum, for Ghost transplants): stock piezo "had just a
  single wire, going to the motherboard"; one poster: silver = hot, blue = ground (colours
  vary). **[F]** https://line6.com/support/topic/69967-variax-300-piezo-died-in-australia-are-replacements-available/
- Variax 500 (MG1, official): the bridge assembly ends in a **10-conductor flex circuit**
  (`21-32-0001 CBL FLEX CIRCUIT 10-COND 0.5 mm pitch`) that plugs into a **rotary-lock ZIF
  header J4 (10P × 0.5 mm)** on the main PCB. **[V]** (service manual parts list + assembly
  instructions p. 5–6 of "VARIAX MECHANICAL ASSY INSTRUCTIONS Rev. B")
- Variax 700 (custom install write-up): a 12-pin connector wired to the Line 6 bridge PCB,
  which "connected to a flexi ribbon cable which then plugged into the Main PCB"; the author
  soldered 7 wires (6 strings + ground) directly to the main PCB at "the 6 places on the
  Line 6 Main PCB labeled A, B, C, D, E, and F", A being the high E. **[F]**
  https://thermalrelief.com/tta002/
- **Conclusion:** 300 = bridge PCB + 8-wire discrete harness + polarized connector;
  500 = flex circuit + ZIF; 700 = flex ribbon. All carry six separate signal lines.

### 1.4 What happens on the main board (verified on the Variax 500 schematic)
Sheet 1 of main PCB 35-00-0137 "MG1 ANALOG + SUPPLY" (Rev E, Aug 2002), file
`sm-pages/crop1-piezo-01.png`: **[V]**
- Connector **J4 "FROM PIEZOS"**, 10 pins: pins 3–8 are the six piezo lines, each with a
  silk-screened **test point TPA … TPF** right at the connector; pins 1, 2, 9, 10 are ground.
- Each line goes through a **10.0 kΩ series resistor** (R7, R43, R8, R9, R10, R11) into an
  **inverting charge amplifier** built on one quarter of an **OPA4340** (single-supply
  rail-to-rail quad, U3 and U7): 470 pF feedback capacitor in parallel with 10 MΩ, non-
  inverting input tied to a **+2.5 V** reference (U7-A buffer). Annotation: "Vout max =
  4.0 V, Vout min = 0.18 V".
- Each amplifier output passes a 1.00 kΩ / 1 nF low-pass and enters one input of an
  **AK4528** 24-bit, 48/96 kHz stereo audio codec ("5.6 Vpp max"). Three codecs:
  U8 = CH 1–2, U4 = CH 3–4, U1 = CH 5–6. Their serial data lines ADC_DATA_1/2/3 go to the
  **DSP56367** 24-bit DSP (U2); SRAM IS61LV6424, flash AM29LV400, 10 MHz master oscillator.
- The three codecs' DAC halves produce the output; only one stereo pair ends at the volume
  pot and the TRS output (sheet 1, "To Volume Pot"), plus the AES line driver.
- Power note on the sheet: "Power in from external supply via TRS or RJ45. Power is
  regulated 7.0 VDC at MGB output. 384 mV max cable drop at full power."
- Other ICs: TLV0838 8-channel 8-bit ADC reads the pots and 5-way switch (DC levels,
  table "5 WAY Switch Voltages": 1.10 / 1.98 / 1.65 / 2.48 / 2.20 V); 74HC4051 mux;
  MAX3100 SPI UART + two MAX3490 RS-422 drivers for MIDI and AES.
- Sound on Sound (2003), quoting Line 6's Marcus Ryle: the signal is "converted directly into
  digital signals through six 24-bit A-D converters". **[F]**
  https://www.soundonsound.com/reviews/line-6-variax
- Wikipedia: "Each of these 6 signals is converted individually to a digital signal …
  virtually altering the pitch of each individual string" (alternate tunings). **[F]**

**Is the 300 the same?** Not the same board, but the same topology:
- A Variax 300 repair thread: owner's board is **35-00-0047** (the only available schematic
  is 35-00-0137, i.e. the 500's); "many of the part numbers and pins don't match"; the
  schematic shows a 32-pin harness, "mine has a 24-pin"; but "Audio passes properly from
  the piezo input through the op-amps to the ADC", and the DACs receive a 10 MHz master
  clock. **[F]** https://line6.com/support/topic/37498-variax-300-repair-no-output/
- A builder who put a salvaged **Variax 300 processor board** in a pedal "bypassed the
  2 TL084's, simply wiring small cables direct to the ADC chips", reasoning that the TL084s
  "are used only to keep the impedance very low, to get low noise out of the piezos".
  **[S]** https://www.vguitarforums.com/smf/index.php?topic=5213.0 — so the 300 appears to
  use two quad TL084 op-amps in place of the 500's OPA4340s. [?] unverified beyond snippet.
- Line 6 sells `50-04-0020 Main PCB for Variax 300 and 600` (one part for both). **[S]**
  https://www.fullcompass.com/prod/210098-line-6-50-04-0020-main-pcb-for-variax-300-and-600
- Alternate tunings on a 300 are stored per model via Workbench; Line 6 Expert: "The Variax
  300 supports alternate tunings within the specific Variax models using Workbench." **[F]**
  https://line6.com/support/topic/24783-variax-alternate-tunings-work-with-all-variax-models
  — this proves the 300's DSP handles strings separately.

---

## 2. Every external connector and what it carries

### 2.1 1/4" jack (TRS)
- Handbook (300): "two output connectors: a familiar 1/4-inch jack and a second 'mystery'
  jack protected by a rubber cap." With a **mono cable** the guitar runs on battery; with
  the supplied **TRS cable** into the **XPS Mini**, the ring carries DC power up to the
  guitar and batteries are bypassed. "If the guitar is connected to the XPS and the XPS
  power is disconnected, any batteries installed in the Variax will be drained." Any
  high-quality TRS cable up to 20 ft works. **[V]** (variax300-manual.txt, p. 3–4)
- Schematic (500, "MG-1 OUTPUT" 35-00-0161): the jack is a 6-pin TRS; **tip = TRS_OUTPUT
  (analogue modelled sum), ring = TRS_PWR_IN, sleeve = ground**, with ferrite beads and a
  47 µF cap. **[V]** (`sm-pages/p-04.png`)
- Output level: the modelled stereo DAC pair is summed/attenuated to the volume pot; the
  XPS footswitch's 1/4" output is "+8 dBV max" (XPS schematic, 35-00-0138). **[V]**
- The 1/4" output is "just an analog output of the digital data being sent over the VDI"
  (forum member) — i.e. the same summed signal. **[F]**
  https://line6.com/support/topic/15416-has-anyone-come-up-with-a-work-around-for-the-circuit-loading-and-current-draw-using-vdi-and-14-inch-out

### 2.2 RJ-45 "Variax Digital Interface" (VDI)
- Handbook (300) only calls it the "mystery" connector "for future upgrades". The 600
  handbook (same generation) is explicit: "The 'mystery' connector provides digital audio to
  Line 6 products with a special Variax input, like PODXT Live or Vetta II, and also allows
  you to create your own personalized guitar models using the Variax Workbench." **[V]**
  (variax600-manual.txt, p. 3; 300 text p. 3)
- **Pinout, from the official RJ-45 PCB schematic 35-00-0160 (MG1, Rev A, 2002)**,
  `sm-pages/crop6-rj45-06.png` — jack is a 9-pin RJ-45 in an XLR/EtherCon shell
  (`21-16-0002`), each line through a ferrite bead: **[V]**

  | RJ-45 pin | Signal (guitar side) |
  |---|---|
  | 1 | RX− (MIDI in to guitar, −) |
  | 2 | TX− (MIDI out of guitar, −) |
  | 3 | GND / AES− |
  | 4 | PWR (+7 V in) |
  | 5 | GND (0 V) |
  | 6 | TX+ (MIDI out of guitar, +) |
  | 7 | PWR / AES+ |
  | 8 | RX+ (MIDI in to guitar, +) |
  | shell | shield |

  Internally the AES pair comes from a MAX3490 RS-422 driver fed by the DSP's AES output;
  MIDI in/out use a second MAX3490 and a MAX3100 UART (`sm-pages/crop2-aesmidi-02.png`).
  The 32-pin main harness (J2, 21-21-0032) carries MIDI_OUT±, AES±, MIDI_IN±, RJ_PWR_IN and
  ground to the jack-plate board (`sm-pages/crop2-harness-02.png`). **[V]**
  Note: a forum-derived pinout circulating online (pin 2 = +7 V, pins 4/5 = AES) **[?]**
  contradicts the schematic; the schematic is the authoritative source for the 500 and the
  cables are interchangeable across first-generation models, so pin 4/7 power is the
  reliable reading.
- **What the AES stream contains:** the schematic has a single AES transmitter driven by
  one DSP serial output; the codec outputs that reach the pot/TRS are one stereo pair. There
  is no six-channel path to the jack. Community descriptions agree: "VDI … = MIDI +
  AES/EBU" **[F]** https://blekenbleu.github.io/JTV89F/Variax ; "24 bit PCM Audio @ 48k"
  **[S]** (vguitarforums VDI thread, could not be opened:
  https://www.vguitarforums.com/smf/index.php?topic=13484.0); the 2026 reverse-engineering
  of a JTV-69 reads the VDI audio into a Teensy's S/PDIF input as a normal AES3 stereo
  stream and supplies "+7.5 V power for the guitar" from a modified phantom circuit **[F]**
  https://blog.brixit.nl/variax/ (summarised by Adafruit,
  https://blog.adafruit.com/2026/10/12/reverse-engineering-the-variax-guitar). On the JTV
  the two channels are "mag only" and "Variax mag/model" **[S]**; on a first-generation
  guitar (no magnetic pickups) only the modelled sum exists. **No per-string data on VDI.**
- **Power over VDI:** a PODxt Live / X3 Live / HD500 / Vetta powers the guitar through the
  cable; "With the VDI connection you do not also need an analog output cable from the
  V300" **[F]** https://line6.com/support/topic/53849-300-w-xtl/ ; Workbench USB interface
  does *not* power it ("the Variax still needs power from the XPS footswitch") **[V]**
  https://kb.line6.com/variax-workbench-faqs-and-v-1-72-release-notes
- VDI control is MIDI (channel 16 per blekenbleu; sysex vendor ID 00 01 0C per BrixIT).
  **[F]**

### 2.3 Power: batteries and XPS Mini
- Six AA cells, "approximately 10–12 hours"; emergency 9 V battery on the same clip,
  1–2 hours. **[V]** (300 handbook p. 3)
- XPS Mini (300) / XPS Footswitch (500/700): phantom-powers the guitar through the TRS
  ring. The XPS footswitch schematic (35-00-0138 "MGB") shows a regulated **+7 V** rail
  (7.02–7.06 V) fed from a 9 VAC PX-2 adapter, a power-on mute, and a 1/4" + balanced XLR
  (+19 dBu) A/B output. The XPS Mini has no XLR/footswitch. **[V]** (`sm-pages/p-03.png`;
  handbook texts)
- Measured by forum members: XPS output "a well-regulated, nominal 7 volts DC". **[F]**
  https://line6.com/support/topic/11406-cheaper-way-to-get-a-line-6-variax-cabled-power-kit/page/2/

### 2.4 Expansion header
- None documented beyond the RJ-45. The 500's main board has J1 (8-pin JTAG, "J1 8 PIN
  SIL" on sheet 2), J2 32-pin harness, J3 2-pin battery, J4 piezo flex. **[V]**

---

## 3. Service manuals, schematics, teardowns

- **Found and read: "Line 6 Variax Service Manual" (76 p.)** — actually the **MG1 =
  Variax 500** release package: parts lists (guitar, XPS, main PCB BOM with reference
  designators), mechanical assembly instructions Rev B, and schematic/PCB sheets:
  35-00-0137 main (2 sheets), 35-00-0138 XPS "MGB", 35-00-0159/0158/0157 pot breakaways,
  35-00-0160 RJ-45 PCB, 35-00-0161 output jack PCB, assembly top/bottom views.
  Mirrors: https://www.synthxl.com/wp-content/uploads/2022/02/Line-6-Variax-Service-Manual.pdf
  (read), https://www.scribd.com/document/344066269/line-6-variax-service-manual (not
  readable here), http://guitar-gear.ru/forum/topic/3413- (5.22 MB attachment),
  listed at https://music-electronics-forum.com/forum/schematic-requests/45489-links-to-line-6-schematics .
  Line 6 moderators remove links to it as "copyrighted and proprietary material". **[V]**
- **Where the six channels can be tapped (on the 500 board, by schematic):**
  (a) raw piezo lines at **TPA–TPF / J4 pins 3–8** (very high impedance, unbuffered —
  not usable without a charge amp and it would load the Variax input);
  (b) **op-amp outputs** U7-C/U7-D/U3-A…U3-D (buffered, ~0.18–4.0 V swing around a
  +2.5 V bias, 10 MΩ‖470 pF charge amp, low output impedance) — this is where the
  documented conversion tapped;
  (c) **codec inputs** LIN+/RIN+ of U8/U4/U1 after the 1 kΩ/1 nF filter (same signal).
  The 300 board has the same chain (piezo → op-amps → ADC) but different designators,
  which must be traced on the actual board (no 300 schematic found). **[V]/[F]**
- Variax 300 board photos exist in the repair thread (IMG_3080–3086 on line6.com, not
  downloadable here) **[F]**; a 2016 transplant thread says the 300 has three boards
  ("knob / main processor / selector switch" plus the bridge PCB) inside a shielded
  "coffin" under the pickguard **[F]**
  https://line6.com/support/topic/22343-ready-to-transplant-final-questions/ ;
  a VDI-socket repair on a 600 identifies the jack as Neutrik **NE8FAV** and shows the
  jack-plate PCB with an 8-pin and a 2-pin connector **[F]**
  https://panicacid.com/line-6-variax-300-500-600-and-700-ethercon-vdi-neutrik-port-repair/
- No YouTube teardown of a 300 main board was found.

---

## 4. 300 vs 500 / 600 / 700 — what matters for a hex tap

| Item | 300 | 500 | 600 | 700 |
|---|---|---|---|---|
| Year | 2005 | 2002 | 2005 | 2003 |
| Bridge | L.R. Baggs X-Bridge hardtail, bridge PCB + 8-wire harness **[V]** | X-Bridge hardtail, flex circuit **[V]** | tremolo, PCB with jumper wires **[F]** | hardtail or tremolo, flex ribbon **[F]** |
| Electronics location | boards in a shielded "coffin" under the pickguard **[F]** | body-mounted main PCB (assembly instructions) **[V]** | coffin under pickguard **[F]** | body cavity **[F]** |
| Main PCB | 35-00-0047 / 50-04-0020 (shared with 600), 24-pin harness **[S]/[F]** | 35-00-0137 (50-00-0137 stuffed) **[V]** | 50-04-0020 **[S]** | like the 500 but "NOT interchangeable" (Line 6 staff) **[F]** |
| Input chain | 2× TL084 (?) → ADCs **[S]** | 2× OPA4340 → 3× AK4528 → DSP56367 **[V]** | — | same family as 500 |
| Power options | 6 AA / XPS Mini (TRS) / VDI **[V]** | 6 AA / XPS footswitch / VDI **[V]** | same as 300 | same as 500 |

- "The modelling engine is identical to that in both the 500 and 700"; the 300's
  "electronic guts are loaded on to the scratchplate". **[S]** (MusicRadar)
- Line 6 staff: 500 and 700 main boards "are NOT interchangeable. Different hardware and
  different program." **[F]** https://line6.com/support/topic/61154-variax-700-board-in-a-500
- Forum consensus: 300/600 share one board generation (coffin), 500/700 another. **[F]**
  https://line6.com/support/topic/15233-variax-circuit-boards
- The 500/700 reportedly use "three separate A/D converter ICs — each handle a specific
  pair of strings" **[S]** (instituteofnoise.com, unreachable) — consistent with the three
  AK4528 codecs on the 500 schematic.
- Practical consequence: the published schematic helps to understand the 300 but every
  designator must be re-identified on the 300 board; the 300's discrete 8-wire bridge
  harness is actually *easier* to intercept than the 500's 0.5 mm flex.

---

## 5. Workbench, firmware, USB: any raw string access? (expected: no — confirmed)

- Workbench is "an Editor/Librarian for the Variax 300/500/600/700 …"; it edits body,
  pickups, tone controls and tuning of models already in the guitar; "The Workbench USB
  device is not an audio interface"; the 300 connects via the Workbench USB interface
  (needs XPS power) or a PODxt Live / X3 / HD500 / Vetta. **[V]**
  https://kb.line6.com/variax-workbench-faqs-and-v-1-72-release-notes
- Workbench string-volume faders are a global per-string setting (six faders) — another
  proof of per-string processing, but still no audio out. **[F]** (600 thread above)
- Firmware: owners report the 300 needing firmware 3.10 and "the older version of WB";
  Workbench HD (JTV) does not support the 300. **[F]**
  https://line6.com/support/topic/7482-variax-300-and-workbench-any-success/
- Third-party tooling (phulst/variax, BrixIT) speaks sysex/CC to the guitar over the USB
  interface or VDI; tuning offsets are six bytes per string in the sysex dump. None exposes
  audio. **[F]** https://github.com/phulst/variax , https://blog.brixit.nl/variax/
- Line 6 position: "No there is no feature in Variax that allows you to separate the string
  outputs." (Line 6 Expert) and, from the Service Engineer moderator, tapping before the
  processor board "would disturb input impedance … The insides of these guitars are _not_
  able or designed to handle such modifications"; "Very difficult, even for me"; risks:
  "impedance, signal and noise issues, pulling down supply voltages". **[F]**
  https://line6.com/support/topic/23735-splitting-piezo-string-outputs/

---

## 6. Documented precedents for extracting six signals

1. **Variax 300 donor → Roland 13-pin hex output (David Ryan, May 2008, midiguitar list;
   same build discussed as "Variax Conversion (W/ GK13 Pin Output)" on vguitarforums):**
   "I tapped into the piezo signals at the point after they are buffered by the Variax
   electronics" and before the A/D; "I only had to add a 100 Ohm resistor to each signal
   before being fed to the 13 pin din connector"; "the signals going into the AtoD's have a
   2.5V offset since the Variax converters are not using a bipolar supply"; "You must
   connect the Variax supply ground to the Hex device ground"; "The input circuit of the Hex
   device must have some form of DC blocking" (GR-30 / GI-10 inputs are AC-coupled); kept
   the unbuffered piezo runs as short as possible. **[F]**
   https://midiguitar.yahoogroups.narkive.com/bl9IUc2N/left-handed-steinberger-spirit-variax-conversion
   (vguitarforums thread https://www.vguitarforums.com/smf/index.php?topic=761.0 could not be
   opened; snippets match).
2. **Reverse direction (GK/13-pin into a Variax DSP board):** commercial adapter modules
   existed ("GK 13-pin to Variax Adapter Module for Variax 300, 600 & JTV", RackVax) that
   plug between the bridge connector and the main board; "you cannot simply go straight
   from the GK to the input of the Variax DSP board" because of level/impedance. **[S]**
   https://www.vguitarforums.com/smf/index.php?topic=17228.0 ,
   https://www.vguitarforums.com/smf/index.php?topic=3927.0
3. **Passive splitting at the saddles:** Graph Tech's FAQ (relayed) says Hexpander MIDI and
   the Variax DSP cannot share the same saddles because of impedance; a passive RMC VAR001
   split board was judged unable to isolate properly, an active op-amp splitter was planned.
   **[S]** https://www.vguitarforums.com/smf/index.php?topic=36676.0 ;
   https://line6.com/support/topic/23735-splitting-piezo-string-outputs/ **[F]**
4. **Ghost saddle swap with per-saddle ground wires** (H&H V300GU-X; Line 6 forum): makes the
   six channels cleaner and removes the ground-through-bridge problem; Ghost saddles are
   "a bit hotter" and may need string volumes lowered in Workbench. **[V]/[F]**

Implication for JAMRACK: the realistic tap on a Variax 300 is six wires soldered at the
buffered op-amp outputs (or at the bridge-PCB side with an added charge-amp board), fed
through series resistors into an external AC-coupled 6-channel preamp/ADC (e.g. a
13-pin-to-6×TRS breakout or a Roland GK/GI-style input), while the guitar keeps running
its own modelling. This is a soldering job inside the coffin, unsupported by Line 6, and
nothing in the connectors, firmware or Workbench offers an alternative.

---

## 7. Unverified / contradictory / not reachable

- VDI pinout circulating on forums (pin 2 = +7 V; pins 4/5 = AES) contradicts the official
  500 RJ-45 schematic (pin 4 = PWR, 5 = GND, 3/7 = AES, 1/8 = MIDI in, 2/6 = MIDI out). The
  schematic is dated 2002; a 300-specific jack-plate schematic was not found.
- Whether the 300's op-amps are TL084s (one snippet) — not verified on a board or schematic.
- Variax 300 main-board number: 35-00-0047 (one owner) vs sales listing 50-04-0020 ("Main
  PCB for Variax 300 and 600"); Line 6 staff note that 35-xx are unstuffed-board numbers and
  50-xx stuffed assemblies, so both may be right.
- "Three separate A/D converter ICs" on the 500/700 (instituteofnoise.com, DNS failure).
- Pages that returned 403/503 and were used only via search snippets: vguitarforums.com
  (all threads), harmonycentral.com, musicradar.com, fullcompass.com, chrisgeorgeguitars.com,
  scribd.com, ultimate-guitar.com.
- No photos of the 300 main board could be downloaded; the line6.com repair-thread images
  (IMG_3080–3086) were not fetched.
- The Variax 300 Acoustic has no VDI jack (Line 6 KB) — irrelevant to the electric 300 but
  worth knowing when reading forum posts.

---

## Sources

Official Line 6
- Variax 300 Pilot's Handbook (40-00-0064 Rev A): https://l6c-acdn2.line6.net/data/6/0a060b316ac34f05943d62560/application/pdf/Variax%20300%20User%20Manual.pdf
- Variax 500/700 Pilot's Handbook (40-00-0007): https://line6.com/data/6/0a060b316ac34f05943ed7907/application/pdf/Variax%20500/700%20User%20Manual%20-%20English%20.pdf
- Variax 600 Pilot's Handbook (Rev A): https://l6c-acdn2.line6.net/data/6/0a060b316ac34f05944058588/application/pdf/Variax%20600%20User%20Manual%20(%20Rev%20A%20).pdf
- Line 6 timeline: https://line6.com/timeline/
- Workbench FAQ / v1.72 notes: https://kb.line6.com/variax-workbench-faqs-and-v-1-72-release-notes
- Line 6 "Variax Service Manual" (MG1/Variax 500 parts list, assembly, schematics; community mirror): https://www.synthxl.com/wp-content/uploads/2022/02/Line-6-Variax-Service-Manual.pdf (listing page https://www.synthxl.com/line-6-variax/ ; other mirrors: https://www.scribd.com/document/344066269/line-6-variax-service-manual , http://guitar-gear.ru/forum/topic/3413- , index at https://music-electronics-forum.com/forum/schematic-requests/45489-links-to-line-6-schematics )

Vendors / parts
- H&H Guitars V300GU-X Ghost upgrade instructions (Variax 300 bridge wiring): https://img1.wsimg.com/blobby/go/0079147b-b95d-419b-bef2-66012917280e/downloads/540cea35-4dda-4cea-9544-7779d8fa58b8/25-0914%20V300GU-X%20Instructions.pdf?ver=1757865112005
- Graph Tech Ghost saddles for Variax: https://help-center.graphtech.com/en-US/can-i-replace-the-saddles-on-my-line6-variax-with-ghost-saddles-758667
- Full Compass 50-04-0020 Main PCB (300/600): https://www.fullcompass.com/prod/210098-line-6-50-04-0020-main-pcb-for-variax-300-and-600
- Full Compass 50-02-0049 Bridge PCB (300): https://www.fullcompass.com/prod/249525-line-6-50-02-0049-bridge-pcb-for-variax-300

Line 6 Community threads
- Splitting piezo string outputs: https://line6.com/support/topic/23735-splitting-piezo-string-outputs/
- Variax 300 repair (no output), board 35-00-0047: https://line6.com/support/topic/37498-variax-300-repair-no-output/
- How do the piezos ground: https://line6.com/support/topic/4879-how-do-the-piezos-ground/
- Variax 300 piezo died, replacements: https://line6.com/support/topic/69967-variax-300-piezo-died-in-australia-are-replacements-available/
- Variax 600 uneven levels (L.R. Baggs X-Insert): https://line6.com/support/topic/42972-variax-600-uneven-sound-levels-on-piezo-pickups/
- Variax circuit boards (300/600 vs 500/700): https://line6.com/support/topic/15233-variax-circuit-boards
- Variax 700 board in a 500 (staff: not interchangeable): https://line6.com/support/topic/61154-variax-700-board-in-a-500
- Ready to transplant (300 has three boards, coffin): https://line6.com/support/topic/22343-ready-to-transplant-final-questions/
- 300 with PODxt Live (VDI carries audio, control and power): https://line6.com/support/topic/53849-300-w-xtl/
- VDI + 1/4" simultaneously: https://line6.com/support/topic/15416-has-anyone-come-up-with-a-work-around-for-the-circuit-loading-and-current-draw-using-vdi-and-14-inch-out
- Cabled power kit (XPS ≈ 7 V measured): https://line6.com/support/topic/11406-cheaper-way-to-get-a-line-6-variax-cabled-power-kit/page/2/
- Alternate tunings on a 300: https://line6.com/support/topic/24783-variax-alternate-tunings-work-with-all-variax-models , https://line6.com/support/topic/1082-alternate-tuning-variax-300
- Variax 300 and Workbench: https://line6.com/support/topic/7482-variax-300-and-workbench-any-success/
- JTV to guitar synth via existing piezos ("not possible as is"): https://line6.com/support/topic/9614-tyler-variax-can-it-be-hooked-up-to-a-guitar-synth-using-exisiting-hexpiezo-pickups/
- VDI hacking thread: https://line6.com/support/topic/25390-variax-interface-recommendations-for-hacking/
- Built a better VDI cable: https://line6.com/support/topic/440-built-a-better-variax-vdi-cable/
- Variax 300 transplant with Ghost bridge: https://line6.com/support/topic/3569-variax-300-transplant-using-ghost-bridge-problems/

Other
- Steinberger/Variax 300 conversion with 13-pin hex output (David Ryan, 2008): https://midiguitar.yahoogroups.narkive.com/bl9IUc2N/left-handed-steinberger-spirit-variax-conversion
- vguitarforums (snippets only): https://www.vguitarforums.com/smf/index.php?topic=761.0 , https://www.vguitarforums.com/smf/index.php?topic=5213.0 , https://www.vguitarforums.com/smf/index.php?topic=13484.0 , https://www.vguitarforums.com/smf/index.php?topic=17228.0 , https://www.vguitarforums.com/smf/index.php?topic=3927.0 , https://www.vguitarforums.com/smf/index.php?topic=36676.0
- Thermal Relief TTA002 (Variax 700 custom install, A–F pads): https://thermalrelief.com/tta002/
- Panicacid VDI socket repair (NE8FAV): https://panicacid.com/line-6-variax-300-500-600-and-700-ethercon-vdi-neutrik-port-repair/
- BrixIT "Reverse engineering the Variax guitar" (Oct 2026): https://blog.brixit.nl/variax/ ; Adafruit summary: https://blog.adafruit.com/2026/10/12/reverse-engineering-the-variax-guitar
- blekenbleu JTV notes (VDI = MIDI + AES/EBU): https://blekenbleu.github.io/JTV89F/Variax
- CME wireless VDI article (handshake needs AES lines): https://www.cme-pro.com/crowd-creation-2-0-line6-variax-wireless-vdi-by-christian-graffe/
- phulst/variax (USB interface protocol, per-string tuning CCs): https://github.com/phulst/variax
- Sound on Sound Variax review 2003: https://www.soundonsound.com/reviews/line-6-variax
- MusicRadar Variax 300 review (snippet): https://www.musicradar.com/reviews/guitars/line-6-variax-300-24422
- Unique Guitar blog (per-string conversion, VG Strat comparison): https://uniqueguitar.blogspot.com/2012/04/line-6-variax-and-fender-vg-stratocaster.html
- Wikipedia: https://en.wikipedia.org/wiki/Variax
