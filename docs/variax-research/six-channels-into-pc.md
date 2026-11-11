# From six signals to six channels in a Windows PC

Research note for JAMRACK, written 11 November 2026. Scope: once six per-string
signals exist (from a Variax 300 tap or any 13-pin hex pickup), how do they get
into a Windows PC as six simultaneous audio channels, and what can then read
them (browser, DAW, plugin). Every factual claim carries a source URL; items
marked **[unverified]** or **[contradictory]** could not be confirmed from a
readable primary source. Pages on vguitarforums.com, thegearpage.net,
musicstore.de, galaxus.de and web.archive.org returned 403/402/503 to this
session, so those are cited from search snippets only and flagged.

## 0. Two facts that reframe the question

1. **The Variax 300 electric does have one piezo per string.** Wikipedia: "The
   bridge of a Variax electric guitar has an individual piezoelectric pickup for
   each string" and "the instrument's electronics converts each of these six
   signals to a digital signal" ([Wikipedia, Variax](https://en.wikipedia.org/wiki/Variax)).
   The H&H Guitars V300GU-X instructions for the "1st Generation Line6 Variax
   300e" describe unsoldering "the individual piezo wiring from PCB", six
   saddles with their own wires, and checking "continuity between the 'Signal'
   and 'Ground' pads for each string (channel)" ([H&H Guitars PDF](https://img1.wsimg.com/blobby/go/0079147b-b95d-419b-bef2-66012917280e/downloads/540cea35-4dda-4cea-9544-7779d8fa58b8/25-0914%20V300GU-X%20Instructions.pdf?ver=1757865112005)).
   **[contradictory]** A search snippet attributed to an old Line 6 support
   thread said the 300 "has one long piezo strip for all six strings"; that
   archive page no longer renders (https://line6.com/supportarchive/thread/26294/
   now shows only a forum index) and the claim matches the *Variax Acoustic 300*
   (MusicRadar: "no need for a complex six-way (hexaphonic) pickup on this
   model, so it's a straightforward (mono) under-saddle piezo transducer",
   [MusicRadar](https://www.musicradar.com/reviews/guitars/line-6-variax-acoustic-300-steel-25771)).
   Treat the electric 300 as hexaphonic.

2. **The six signals leave the saddles as raw piezo signals, not as buffered
   GK-style signals.** Piezos need a buffer: Graph Tech says "Piezos are
   capacitive devices, meaning their sound changes significantly with the input
   impedance of your amplifier. A pre-amp is often used to buffer the pickup from
   these variations" ([Graph Tech FAQ](https://help-center.graphtech.com/en-US/faq-understanding-piezo-pickups-for-guitar-758673)).
   Inside the Variax there is one buffer per string: "You would have to take it
   after the amplifier that is on the board", "there is one for each piezo"
   (Charlie_Watt, 6 Nov 2016, [Line 6 Community](https://line6.com/support/topic/23735-splitting-piezo-string-outputs/)).
   A documented Variax-to-13-pin conversion tapped "after the Variax's buffering
   stage and before the A/D converters", added a 100 ohm series resistor per line,
   and noted a **2.5 V DC offset** that the receiving device must block, plus a
   shared ground ([vguitarforums topic 761](https://www.vguitarforums.com/smf/index.php?topic=761.0),
   search snippet only, page returns 403). Line 6's own service engineer called
   the mod "Possible, difficult,... again, there are impedance issues, along with
   signal and noise issues to overcome" (psarkissian, 9 Jan 2017, same Line 6
   thread). So what reaches the PC side from a Variax tap is six **buffered,
   DC-offset** signals of roughly line/instrument level, not six high-impedance
   piezo outputs. Standard AC-coupled interface inputs block the DC offset;
   that AC coupling is the normal case for audio inputs but was not verified
   per interface **[unverified]**.

## 1. 13-pin breakout: from one DIN plug to six TS jacks

### 1.1 The Roland GK 13-pin pinout

Consistent across johnp.net, the vguitarforums cable FAQ and the Cycfi Nexus-GK
manual ([johnp.net pinouts](http://johnp.net/projects/guitar-synth/roland-pinouts.html),
[vguitarforums GK-13 Pin Cable FAQ](https://www.vguitarforums.com/smf/index.php?topic=19481.0),
[vguitarforums GK DIN pinouts](https://www.vguitarforums.com/smf/index.php?topic=2359.0)):

| Pin | Function |
|---|---|
| 1-6 | String 1 (high E) to string 6 (low E), buffered analogue audio |
| 7 | Normal (magnetic) pickup, buffered |
| 8 | Synth volume, 0-5 V control voltage |
| 9 | Guitar/Mix/GK selector; 0.5 / 1.0 / 1.5 V measured on a GR-20 (johnp.net); "unused" on some units **[contradictory between sources]** |
| 10, 11 | S1 / S2 switches (down/up), switched to ground |
| 12 | +7 V DC from the host |
| 13 | -7 V DC from the host |
| shell | Ground ("the Only Ground path connection", vguitarforums FAQ snippet) |

Signal character: the string pins "are analog like the normal pickup, and after
the GK preamp, are sent at a low impedance of 100 ohms" (vguitarforums FAQ,
search snippet). The FAQ also gives "±2.5 Vpp" as the maximum analogue swing an
AXON converter accepts (snippet). Consequence for interface choice: a GK-3-type
hex output is **low impedance**, so line inputs are electrically fine; instrument
(1 MOhm) inputs are not required for impedance, only convenient for gain. A
Variax tap is also buffered (section 0). The task's premise "hex signals are
high impedance" holds only for unbuffered piezos, which no 13-pin system emits.

### 1.2 DIY cable or box

- Wiring: a 13-pin inline or panel socket; pins 1-6 to six TS jacks, shell to
  all sleeves; pins 12/13 to a bipolar supply if the pickup needs power. A 2024
  build log uses Mogami W2368 per channel so that "each channel will have it's
  own shield for optimal channel separation", a Neutrik NP2X-B per string and a
  separate W2697 power lead (ElectroFuzz, 26-28 Oct 2024,
  [jamosapien.com](https://jamosapien.com/t/lets-build-a-gk-3-break-out-cable/7892)).
  Separate Strings' Bill recommends "a small diecast box", a 13-pin panel socket
  for GK IN and a D-sub 9 for strings out (same thread).
- Power for a hex preamp that expects +7/-7 V: "Two PP3 9V battery will only
  last about nine hours with GK3 use", two 1N4148 protection diodes and LEDs per
  rail, and "Most v-guitar units supply at least 100mA per rail" (Bill, 28 Oct
  2024, same thread). Rail-dropout warning: an op-amp "can be damaged if one
  rail drops below its minimum operating voltage", so switch both rails
  together. Alternative: two 1 Spot-type supplies in "+9v / -9v configuration"
  (ElectroFuzz). The GK-3's NJM2068 op-amps are rated "±4V~±18V" and a GK-3
  was found running from ±18 V in a commercial breakout (selfconstruct and
  nix808, Aug 2012, [KVR thread](https://www.kvraudio.com/forum/viewtopic.php?t=355708)).
  A vguitarforums thread reports 200 mAh PP3s lasting "about six hours before
  the LEDs dim" ([topic 38265](https://www.vguitarforums.com/smf/index.php?topic=38265.0), snippet).
  Warning from the same jamosapien thread: "Using XLR here for bipolar -7+7
  power looks kinda dangerous" (ignis32); pick a connector nobody can plug a mic
  into.
- Passive option: "It is absolutely possible to run the Hex PU in passive mode
  with no power supplied to the op-amps" (Dutti67), but Bill adds that direct
  strings then need a high-pass "below 80Hz" and "at least 25dB of gain"
  (jamosapien thread). Not relevant to a Variax tap, which is already buffered.
- **Variax case:** the Variax powers its own buffers from its battery/XPS, so a
  Variax tap does not need the ±7 V rails at all; a plain 6-wire + ground fan-out
  with DC-blocking is the whole "breakout". Only if the owner later fits a
  GK-3 or Graph Tech Hexpander do the rails matter (the Hexpander "draws power
  from the GK processor rather than from a battery", vguitarforums 38265
  snippet; Graph Tech basic Hexpander PE-0440-00 $208.95, "Sold out" on
  graphtech.com, [Graph Tech](https://www.graphtech.com/products/ghost-hexpander-basic-preamp-w-traktion)).

### 1.3 Commercial breakout products (status checked November 2026)

| Product | What it is | Price | Status / source |
|---|---|---|---|
| Separate Strings GKP-2 | 13-pin in, two 13-pin out, six TS string outs + six string returns, pin-7 I/O; passive, powered by the GK host on the centre socket | £200 / $269 + shipping | Order by e-mail; [product page](https://www.separate-strings.co.uk/GKP2%20Breakout%20Box.html), [SOS review May 2013](https://www.soundonsound.com/node/4903810) (active version "driven by two 9V PP3 batteries") |
| Separate Strings GKP-1 and cable | GKP-1 box £160 / $207; "GK Mini Breakout Cable" 13-pin to string jacks £95 / $128; custom low-noise dual PSU parts ~£30 | | Bill, Oct 2024 on [jamosapien](https://jamosapien.com/t/lets-build-a-gk-3-break-out-cable/7892); [cables page](https://www.separate-strings.co.uk/breakout%20cables%20and%20more.html). One buyer: "it works great" with a GK-5 + Boss DA box (digimidi, 29 Oct 2024) |
| Ubertar breakout boxes | Six 1/4" outs; standard input is 7-pin DIN; "13 pin input box with battery hookup $175.00 USD" | $85-175 | Listed, PayPal ordering ([ubertar.com](https://www.ubertar.com/hexaphonic/products.html)) |
| RMC Fanout Box | 13-pin in, six 1/4" outs | not found | Mentioned on a Gibson forum ([forum.gibson.com](https://forum.gibson.com/topic/9390-audio-connection-details/)); current availability **[unverified]** |
| Cycfi Nexus-GK | Breakout for Cycfi's own 19-pin multichannel guitar cable: six TRS jacks = 12 unbalanced channels, plus a 13-pin **output** via the GK-MIDI module; "powered using a well regulated 12-18 Volt power supply" | Basic Kit $199.99, GK-MIDI module $135.99, 12 V PSU $11.99, cable extra | In stock ([cycfi.com](https://www.cycfi.com/product/nexus-gk-gk-set/), [Nexus-GK manual](https://www.cycfi.com/wp-content/uploads/2020/06/Nexus-GK.pdf)). **Not a 13-pin input box**; "there is no Cycfi 13-pin adapter for the Nexus; that product is designed to go into the guitar" ([vguitarforums 24152](https://www.vguitarforums.com/smf/index.php?topic=24152.0), snippet) |
| SynQuaNon 13-Pin Output Breakout | Eurorack, modular -> 13-pin (wrong direction) | $329.95, out of stock | [synquanon.com](https://www.synquanon.com/product-page/13-pin-output-breakout); a 13-pin *input* Eurorack module "with 40dB gain" appears on Reverb ([listing](https://reverb.com/item/31514451-synquanon-eurorack-13-pin-input-breakout-module-roland-boss-gk-guitar-interface-with-40db-gain)) |
| Primova Sound | No breakout box; GKPX-14 (1 GK in -> 4 GK out) $170/$237 **sold out**, GX-2 sold out, MIDX-20 $190, 13-pin cables $14-54 | | [primovasound.com](https://primovasound.com/products/index.php) |
| Keith McMillen StringPort | 13-pin hex pickups -> USB 2, with 24 Mac apps; $995 at launch (28 June 2010) | $995 new (2010), one 2013 used ask $750 | "We are sold out now" (K. McMillen, May 2013, [Synthtopia comment](https://www.synthtopia.com/content/2013/05/24/keith-mcmillen-on-the-qunexus-and-why-new-music-needs-new-instruments/)); "No retailers are offering this product at this time" ([Audiofanzine](https://en.audiofanzine.com/usb-interface/keith-mcmillen-instruments/stringport/)); KVR lists it as "Hardware for macOS" ([KVR](https://www.kvraudio.com/product/stringport-by-keith-mcmillen-instruments)); [KMI press release](https://www.keithmcmillen.com/press/kmi-releases-stringport/). No current used listing found on eBay/Reverb searches; a 2013 dealer said it was "impossible to find on the used market" ([guitarsandeffects.com](http://www.guitarsandeffects.com/2013a04a29stringport/stringport.html)). Mac-only software makes it useless for this Windows project anyway. |
| Boss GKC-AD | 13-pin analogue GK -> "Serial GK" digital for GM-800/VG-800 (not a PC interface by itself) | 169 EUR, in stock | [Thomann FR](https://www.thomann.de/fr/boss_gkc_ad_gk_converter.htm) (page fetched 11 Nov 2026); description [GAK](https://GAK.co.uk/en/boss-gkc-ad-guitar-synth-converter-13-pin-gk-to-serial-gk/966890) |

Cheapest realistic route for a Variax tap: a DIY fan-out (13-pin or any 7+ pin
connector, six TS jacks, ~30-60 EUR of parts) or Ubertar's $175 box; the
Separate Strings cable (£95) if the guitar gets a 13-pin socket.

## 2. Audio interfaces with six or more simultaneous inputs (Windows, ASIO)

Prices are Thomann France VAT-included prices read on 11 November 2026 from the
page HTML (`itemprop="price"`) unless stated; they change weekly.

| Interface | Analogue inputs | Instrument (Hi-Z) inputs | Price | Driver / latency notes |
|---|---|---|---|---|
| Behringer U-Phoria UMC1820 | 8 combo XLR/TRS, "microphone / instrument / line input, switchable" | **all 8** switchable to INST, 1 MOhm; per-channel LINE/INST switch, PAD, phantom in banks of 4 | 169 EUR ([Thomann FR](https://www.thomann.de/fr/behringer_umc1820.htm)); $158 ([thomannmusic.com](https://www.thomannmusic.com/behringer_umc1820.htm)) | Behringer ASIO driver "performs better than ASIO4ALL" (Ableton forum snippet); user reports 3.5 ms RTL at 96 kHz/64 samples, 5-6 ms at 48 kHz/64 (Gearspace snippet, [thread](https://gearspace.com/board/low-end-theory/1455320-affordable-rack-audio-interfaces-other-than-behringer-2.html), page 403) **[unverified]**; specs [Behringer](https://www.behringer.com/en/products/0805-AAN), [manual](https://www.markertek.com/Attachments/Manuals/Behringer/UMC1820-Manual.pdf) |
| Focusrite Scarlett 18i20 4th Gen | "eight analogue inputs with mic preamps" | 2 front combo inputs switchable to Hi-Z ([Thomann](https://www.thomannmusic.com/focusrite_scarlett_18i20_4th_gen.htm)); SOS says "you can switch each of the 18i20's first eight inputs between mic/line and instrument modes" **[contradictory]** | 639 EUR ([Thomann FR](https://www.thomann.de/fr/focusrite_scarlett_18i20_4th_gen.htm)); UK £629.99 | SOS Jan 2025: RTL "the same here as it is with the smaller 4th Gen models" ([SOS review](https://www.soundonsound.com/reviews/focusrite-scarlett-18i20-4th-gen)); Focusrite warns ASIO4ALL "will not guarantee proper functionality" ([Focusrite support](https://support.focusrite.com/hc/en-gb/articles/207546305)) |
| Tascam US-16x08 | 8 XLR mic + 8 TRS line (16 analogue) | 2 (line jacks 9-10 switchable to Hi-Z) | 355 EUR ([Thomann FR](https://www.thomann.de/fr/tascam_us_16x08.htm)); $333 ([thomannmusic.com](https://www.thomannmusic.com/tascam_us_16x08.htm)) | Gearspace review snippet: 10 ms in / 4 ms out at 44.1 kHz, 128 samples ([review thread](https://gearspace.com/board/reviews/991300-tascam-us-16x08.html)) **[unverified]**; line inputs have no preamps ([Equipboard](https://equipboard.com/items/tascam-us-16x08-usb-audio-interface)) |
| MOTU UltraLite mk5 | 2 combo mic/line/Hi-Z + 6 line = 8 analogue | 2 | 740 EUR ([Thomann FR](https://www.thomann.de/fr/motu_ultralite_mk5.htm)); 695 EUR ([musicstore.de](https://musicstore.de/en_DE/EUR/MOTU-UltraLite-MK5/art-PCM0016919-000), snippet) | MOTU: Windows driver "2.4 ms Round Trip Latency (32 sample buffer at 96 kHz)" ([motu.com](https://motu.com/de/news/introducing-the-ultralite-mk5)) |
| PreSonus Studio 1824c | 8 mic (2 combo mic/line/Hi-Z + 6 mic/line) | 2 | ~389 EUR ([muzi.lt](https://muzi.lt/lv/25078-audio-interface-presonus-studio-1824c.html)); not on Thomann FR (404) | Several retailers list it discontinued / no longer orderable ([bax-shop](https://www.bax-shop.co.uk/external-audio-interface/presonus-studio-1824c-usb-c-audio-interface), [djcity](https://djcity.com.au/product/presonus-pre-studio-1824c-18x20-usb-c-audio-interface/)); no successor named **[unverified]** |
| Zoom UAC-8 | 8 combo | 2 (IN1/IN2 Hi-Z, 1 MOhm) ([manual](https://www.manualslib.com/manual/1023000/Zoom-Uac-8.html)) | used only | Listed on Zoom's discontinued-products page ([zoomcorp.com](https://zoomcorp.com/en/us/discontinued-products/)) |

Reading: because the six signals are buffered (section 0 and 1.1), **six line
inputs suffice electrically**; only the UMC1820 offers eight true instrument
inputs if you want 1 MOhm on every string, and it is also the cheapest. Driver
quality varies more than specs suggest: for USB2 interfaces on the same XMOS
chip "performance varied greatly depending on the choice of OEM 3rd party base
driver", and RTL "at a listed 064 setting could vary anywhere from under 4ms
all the way to over 12ms" (DAWbench, 8 May 2025,
[dawbench.substack.com](https://dawbench.substack.com/p/audio-interface-low-latency-performance)).
Measure with RTL Utility ([Oblique Audio](https://oblique-audio.com/rtl-utility.php)).

**The owner's BOSS Gigcaster 5 cannot take six channels.** Official spec:
"Guitar jack: 1/4-inch phone type" (1 MOhm), "Mic 1, 2 jacks: XLR type" (5.6
kOhm), "INST (L, R) jacks: 1/4-inch phone type" (100 kOhm): five analogue
inputs, one of them Hi-Z; the USB side is "Input: 16 ch / Output: 12 ch" but
those are mix/channel feeds, not extra analogue inputs
([boss.info specifications](https://www.boss.info/products/gigcaster_5/specifications/)).
Three of its five inputs (Guitar + INST L/R) could carry three buffered strings
at most, so a second interface is needed.

## 3. What a browser can capture on Windows

- **Chrome/Chromium: two channels maximum per getUserMedia track.** The
  Chromium bug "Support multichannel input from audio device via getUserMedia()"
  (issue 453876 / 40403559, opened 30 Jan 2015, type Feature) reports "getUserMedia()
  only delivers a summed stereo from audio device" and a WebRTC engineer: "Audio
  processing module (APM) currently only supports mono hence audio is downmixed
  before it's processed"; `echoCancellation=false` gave "proper stereo channels
  (channel 1 and 2)" but nothing above ([chromium-bugs mirror](https://groups.google.com/a/chromium.org/g/chromium-bugs/c/OUWw8OtkcGc),
  [issue 40403559](https://issues.chromium.org/issues/40403559), status page
  needs sign-in). Still true in November 2024: "At least under Chrome, the
  maximum channel count has been hardcoded to 2" (Brad Isbell, W3C WebRTC list,
  22 Nov 2024, [lists.w3.org](https://lists.w3.org/Archives/Public/public-webrtc-logs/2024Nov/0191.html)).
  Confirmed in today's Chromium `main` source: `media/webrtc/audio_processor.cc`
  says "The audio processor can only handle up to two channels" and "Limit number
  of apm output channels to 2 to avoid potential problems with discrete channel
  mapping"; the Windows WASAPI input stream `media/audio/win/audio_low_latency_input_win.cc`
  says "only discrete and up to stereo is supported currently" and asserts
  `DCHECK_LE(params.channels(), 2)` ([audio_processor.cc](https://chromium.googlesource.com/chromium/src/+/main/media/webrtc/audio_processor.cc),
  [audio_low_latency_input_win.cc](https://chromium.googlesource.com/chromium/src/+/main/media/audio/win/audio_low_latency_input_win.cc)).
  Since M73 the `channelCount` constraint is accepted but the pipeline still caps
  at 2 (bug snippet). **[unverified workaround]** Many ASIO interfaces also
  expose several WDM stereo "devices" (In 1-2, In 3-4, In 5-6); opening three
  getUserMedia streams on three such devices has not been tested here and a
  2016 Chrome engineer statement said "Chrome only can access a single audio
  device (input/output) at the same time" ([W3C public-audio, 2016](https://lists.w3.org/Archives/Public/public-audio/2016JanMar/0001.html)),
  which may no longer hold. Worth a 10-minute test on the owner's PC before
  ruling the browser out entirely; expect sample-clock drift between streams.
- **Firefox:** the `channelCount` constraint accepts values above 2 (4-channel
  example, Sept 2017, [Mozilla blog](https://blog.mozilla.org/webrtc/channelcount-microphone-constraint/));
  Bugzilla 1393401 "Support audio capture with more than 2 channels in
  getUserMedia" was closed WORKSFORME with "This has been implemented a while
  back, but it depends on the OS. I just tested and it works well here on macOS"
  (Paul Adenot, [bugzilla](https://bugzilla.mozilla.org/show_bug.cgi?id=1393401)).
  Windows behaviour **[unverified]**; also Firefox has no ASIO, so latency would
  be WASAPI-shared class.
- **Conclusion:** six synchronous channels with ASIO latency on Windows means a
  **native** host: the planned VST3 plugin inside a DAW, or a standalone ASIO
  app. Which DAW:
  - **REAPER**: "Each track supports up to 128 individually routable channels",
    "Record directly to stereo, surround, or multichannel audio files", drivers
    "ASIO, WASAPI, Legacy Windows audio" ([reaper.fm/about](https://www.reaper.fm/about.php));
    older SOS tutorial describes the 64-channel-per-track workflow
    ([SOS](https://www.soundonsound.com/techniques/reaper-using-tracks-internal-channels)).
    A six-input plugin on one REAPER track is the natural design.
  - **Ableton Live 12**: inputs are configured "as mono or stereo pairs"; "When a
    mono signal is chosen as an audio track's input, the track will record mono
    samples; otherwise it will record stereo samples"; "Signals in the track's
    device chain are always stereo" ([Live 12 manual, Routing and I/O](https://www.ableton.com/manual/routing-and-i-o/),
    [Ableton help](https://help.ableton.com/hc/en-us/articles/360009201540-Mono-Stereo-conversion-in-Live)).
    No multichannel track type exists; surround is done by spreading stereo
    tracks ([Ableton forum](https://forum.ableton.com/viewtopic.php?p=1715436)). So in
    Live a hex plugin cannot receive six inputs on one track; it would need
    three stereo instances (one per string pair) each emitting MIDI, or a
    standalone hex tracker feeding Live over virtual MIDI. This matters for the
    owner, who works in Live 12.

## 4. How today's hex systems get per-string data into software

- **Fishman TriplePlay (Wireless / Express / Connect):** "a guitar-to-MIDI
  system based on a hexaphonic magnetic pickup"; the controller "uses industry-
  leading software algorithms in conjunction with its hexaphonic magnetic pickup
  to detect the pitch of the notes played on a guitar and convert them into
  MIDI-note messages" ([Fishman support](https://fishman.com/tripleplay-support/));
  the wireless link carries MIDI, "TriplePlay shows up as just another MIDI
  source" (SOS, Aug 2014, [review](https://www.soundonsound.com/reviews/fishman-tripleplay)).
  **No per-string audio ever reaches the computer**; pitch detection is done on
  the guitar. Price 368-390 EUR ([muziker.lt](https://www.muziker.lt/fishman-tripleplay-wireless-guitar-controller),
  [musicstore.com](https://www.musicstore.com/en_LU/EUR/Fishman-TriplePlay-Wireless-Controller-/art-GIT0028002-000) snippet, "SOLD OUT!").
- **Roland/BOSS SY-1000 (13-pin GK IN):** yes, it exposes the six strings as USB
  audio. The reference manual's USB AUDIO routing diagram (p. 85) shows
  "DIVIDED PU -> GK SETTING -> PC GK1-6 IN" alongside "PC IN" (stereo mix), and
  "PC GK 1-6 OUT" for re-synth ([SY-1000 Reference Manual PDF](https://static.roland.com/assets/media/pdf/SY-1000_reference_eng01_W.pdf), p. 85).
  BOSS: "With the GK input, you're able to record each of your instrument's
  strings to separate audio tracks" ([boss.info](https://www.boss.info/products/sy-1000/)).
  gr300.com lists the DAW inputs as 1 USB-L, 2 USB-R, 3 E-High, 4 B, 5 G, 6 D,
  7 A, 8 E-Low, at 48 kHz ([gr300.com](https://gr300.com/SY-1000.htm)). The
  per-string channels are *after* GK SETTING (gain/sensitivity normalisation),
  i.e. clean, levelled hex audio. Price: RRP 999 EUR at launch
  ([gearnews](https://www.gearnews.com/boss-sy-1000-an-expansive-and-expensive-new-guitar-synth-floor-pedal)),
  967 EUR at bax-shop.es (snippet).
- **BOSS GM-800 (Serial GK; 13-pin via GKC-AD):** the Parameter Guide USB AUDIO
  diagrams show "PC GK IN 1-7" sent to the computer in STANDARD, MIX and
  RE-SYNTH modes, RE-SYNTH being "resynthesizing the original sound of the
  divided pickup that's played by your computer" ([GM-800 Parameter Guide PDF](https://static.roland.com/assets/media/pdf/GM-800_ParameterGuide_eng02_W.pdf), p. 16).
  A forum user maps this as USB inputs 1-2 = GM-800 output, 3-8 = individual
  strings ([vguitarforums 36384](https://www.vguitarforums.com/smf/index.php?topic=36384.0), snippet, page 403) **[unverified detail]**;
  SOS confirms "the option to record the output from the divided pickup onto
  your DAW audio tracks" ([SOS, Nov 2023](https://www.soundonsound.com/reviews/boss-gm-800)).
  Price 649 EUR, "Disponible sous 4-5 semaines" ([Thomann FR](https://www.thomann.fr/boss_gm_800_guitar_synthesizer.htm));
  GK-5 pickup 222 EUR ([bax-shop.fr](https://www.bax-shop.fr/micro-pour-guitare/boss-gk-5-electric-guitar-divided-pickup-micro-de-guitare-pour-synthetiseur-de-guitare));
  GKC-AD 169 EUR for a 13-pin source.
- So the **modern equivalent of the StringPort is a BOSS synth pedal used as a
  USB interface**: SY-1000 (13-pin in, ~970 EUR) or GM-800 + GKC-AD (~820 EUR),
  both with Roland Windows drivers, 48 kHz, six string channels plus stereo. They
  also provide the ±7 V for a GK-3/Hexpander. For a Variax tap they would need a
  13-pin plug wired to the tap (section 1.2) and would deliver DC-free,
  normalised strings. Expensive, but zero electronics to design on the PC side.
- **Cycfi** is the other benchmark: Nu pickups -> 19-pin -> Nexus-GK -> 12 TRS
  channels -> ordinary multichannel interface (section 1.3). Same "many analogue
  channels into an 8-input interface" pattern as the DIY route.

## 5. Budget sketch (EUR, November 2026, VAT included where known)

| Route | Parts | Approx. total |
|---|---|---|
| A. DIY fan-out + Behringer UMC1820 | Variax tap + connector/jacks (~30-60), UMC1820 169 | **~200-230** |
| B. Ubertar 13-pin box ($175 + shipping/duty) + UMC1820 | ~190 + 169 | ~360 |
| C. Separate Strings cable (£95) + Tascam US-16x08 (355) | | ~470 |
| D. GM-800 (649) + GKC-AD (169) as USB hex interface | needs a 13-pin plug on the tap; also a full synth | ~820 |
| E. SY-1000 (~967) as USB hex interface | 13-pin in, six strings on USB at 48 kHz | ~970 |

Route A is the one to prototype: it is cheap, every link is documented, and the
UMC1820's eight switchable instrument inputs tolerate whatever level and DC the
tap turns out to have (the owner should still add series resistors and
DC-blocking as in the vguitarforums conversion). The software side is then a
REAPER multichannel track (or three stereo Live tracks) feeding the planned
VST3, not the browser.

## 6. Open points to verify by hand

1. The exact tap points and DC offset on a Variax **300** main board (all
   documented conversions are 500/700 era boards); vguitarforums topic 761 must
   be read in full once reachable.
2. Whether the owner's Windows build of Chrome can open three stereo WDM
   devices of one ASIO interface simultaneously (section 3 workaround).
3. Firefox multichannel capture on Windows (only macOS confirmed).
4. Current GM-800 USB channel map (manual shows "PC GK IN 1-7"; the seventh is
   presumably the normal pickup, not confirmed).
5. Live 12: whether a VST3 with a 6-channel input bus can be fed at all (only
   mono/stereo track inputs are documented).
6. Measured RTL of the UMC1820 driver on the owner's PC with RTL Utility.

## Sources

- Wikipedia, Variax: https://en.wikipedia.org/wiki/Variax
- H&H Guitars, V300GU-X Graph Tech Ghost upgrade instructions (Variax 300e): https://img1.wsimg.com/blobby/go/0079147b-b95d-419b-bef2-66012917280e/downloads/540cea35-4dda-4cea-9544-7779d8fa58b8/25-0914%20V300GU-X%20Instructions.pdf?ver=1757865112005
- MusicRadar, Variax Acoustic 300 Steel review: https://www.musicradar.com/reviews/guitars/line-6-variax-acoustic-300-steel-25771
- Line 6 Community, "Splitting piezo string outputs" (2016-2018): https://line6.com/support/topic/23735-splitting-piezo-string-outputs/
- Graph Tech FAQ, understanding piezo pickups: https://help-center.graphtech.com/en-US/faq-understanding-piezo-pickups-for-guitar-758673
- vguitarforums, Variax Conversion (W/ GK13 Pin Output) [snippet only]: https://www.vguitarforums.com/smf/index.php?topic=761.0
- johnp.net, Roland guitar synth cable pinouts: http://johnp.net/projects/guitar-synth/roland-pinouts.html
- vguitarforums, GK-13 Pin Cable FAQ [snippet only]: https://www.vguitarforums.com/smf/index.php?topic=19481.0
- vguitarforums, GK DIN Pinouts and DIY info [snippet only]: https://www.vguitarforums.com/smf/index.php?topic=2359.0
- vguitarforums, 13 pin GK pickup power supply [snippet only]: https://www.vguitarforums.com/smf/index.php?topic=38265.0
- vguitarforums, GM-800 USB AUDIO "RE-SYNTH" [snippet only]: https://www.vguitarforums.com/smf/index.php?topic=36384.0
- vguitarforums, Cycfi Nexus V2 Hex Pickup [snippet only]: https://www.vguitarforums.com/smf/index.php?topic=24152.0
- jamosapien.com, "Let's build a GK-3 break out cable" (Oct 2024): https://jamosapien.com/t/lets-build-a-gk-3-break-out-cable/7892
- KVR forum, "Run GK3 on phantom power?" (2012): https://www.kvraudio.com/forum/viewtopic.php?t=355708
- Separate Strings, GKP-2 Breakout Box: https://www.separate-strings.co.uk/GKP2%20Breakout%20Box.html
- Separate Strings, breakout cables: https://www.separate-strings.co.uk/breakout%20cables%20and%20more.html
- Sound On Sound, Separate Strings V-Guitar Breakout Box review (May 2013): https://www.soundonsound.com/node/4903810
- Ubertar hexaphonic products: https://www.ubertar.com/hexaphonic/products.html
- Gibson forum, audio connection details (RMC Fanout Box): https://forum.gibson.com/topic/9390-audio-connection-details/
- Cycfi Nexus-GK GK Set: https://www.cycfi.com/product/nexus-gk-gk-set/
- Cycfi Nexus-GK manual (PDF): https://www.cycfi.com/wp-content/uploads/2020/06/Nexus-GK.pdf
- SynQuaNon 13-Pin Output Breakout: https://www.synquanon.com/product-page/13-pin-output-breakout
- Reverb, SynQuaNon 13-pin input breakout module: https://reverb.com/item/31514451-synquanon-eurorack-13-pin-input-breakout-module-roland-boss-gk-guitar-interface-with-40db-gain
- Primova Sound products: https://primovasound.com/products/index.php
- Graph Tech, Ghost Hexpander basic preamp: https://www.graphtech.com/products/ghost-hexpander-basic-preamp-w-traktion
- KMI press release, StringPort (28 June 2010): https://www.keithmcmillen.com/press/kmi-releases-stringport/
- KVR, StringPort product page: https://www.kvraudio.com/product/stringport-by-keith-mcmillen-instruments
- Audiofanzine, StringPort: https://en.audiofanzine.com/usb-interface/keith-mcmillen-instruments/stringport/
- Synthtopia, Keith McMillen on the QuNexus (May 2013, "sold out" comment): https://www.synthtopia.com/content/2013/05/24/keith-mcmillen-on-the-qunexus-and-why-new-music-needs-new-instruments/
- guitarsandeffects.com, used StringPort listing (2013): http://www.guitarsandeffects.com/2013a04a29stringport/stringport.html
- Thomann FR, Boss GKC-AD: https://www.thomann.de/fr/boss_gkc_ad_gk_converter.htm
- GAK, Boss GKC-AD overview: https://GAK.co.uk/en/boss-gkc-ad-guitar-synth-converter-13-pin-gk-to-serial-gk/966890
- Thomann FR, Behringer UMC1820: https://www.thomann.de/fr/behringer_umc1820.htm
- thomannmusic.com, Behringer UMC1820: https://www.thomannmusic.com/behringer_umc1820.htm
- Behringer UMC1820 product page: https://www.behringer.com/en/products/0805-AAN
- Behringer UMC1820 manual: https://www.markertek.com/Attachments/Manuals/Behringer/UMC1820-Manual.pdf
- Gearspace, affordable rack interfaces (UMC1820 RTL reports) [snippet only]: https://gearspace.com/board/low-end-theory/1455320-affordable-rack-audio-interfaces-other-than-behringer-2.html
- Thomann FR, Scarlett 18i20 4th Gen: https://www.thomann.de/fr/focusrite_scarlett_18i20_4th_gen.htm
- thomannmusic.com, Scarlett 18i20 4th Gen: https://www.thomannmusic.com/focusrite_scarlett_18i20_4th_gen.htm
- Sound On Sound, Scarlett 18i20 4th Gen review (Jan 2025): https://www.soundonsound.com/reviews/focusrite-scarlett-18i20-4th-gen
- Focusrite support, ASIO4ALL note: https://support.focusrite.com/hc/en-gb/articles/207546305
- Thomann FR, Tascam US-16x08: https://www.thomann.de/fr/tascam_us_16x08.htm
- thomannmusic.com, Tascam US-16x08: https://www.thomannmusic.com/tascam_us_16x08.htm
- Gearspace, Tascam US-16x08 review [snippet only]: https://gearspace.com/board/reviews/991300-tascam-us-16x08.html
- Equipboard, Tascam US-16x08: https://equipboard.com/items/tascam-us-16x08-usb-audio-interface
- Thomann FR, MOTU UltraLite mk5: https://www.thomann.de/fr/motu_ultralite_mk5.htm
- MOTU, introducing the UltraLite mk5: https://motu.com/de/news/introducing-the-ultralite-mk5
- musicstore.de, MOTU UltraLite mk5 [snippet only]: https://musicstore.de/en_DE/EUR/MOTU-UltraLite-MK5/art-PCM0016919-000
- muzi.lt, PreSonus Studio 1824c: https://muzi.lt/lv/25078-audio-interface-presonus-studio-1824c.html
- bax-shop.co.uk, PreSonus Studio 1824c (no longer orderable): https://www.bax-shop.co.uk/external-audio-interface/presonus-studio-1824c-usb-c-audio-interface
- djcity.com.au, PreSonus Studio 1824c (discontinued): https://djcity.com.au/product/presonus-pre-studio-1824c-18x20-usb-c-audio-interface/
- Zoom, discontinued products (UAC-8): https://zoomcorp.com/en/us/discontinued-products/
- ManualsLib, Zoom UAC-8 operation manual: https://www.manualslib.com/manual/1023000/Zoom-Uac-8.html
- DAWbench, audio interface low latency performance (8 May 2025): https://dawbench.substack.com/p/audio-interface-low-latency-performance
- Oblique Audio, RTL Utility: https://oblique-audio.com/rtl-utility.php
- BOSS Gigcaster 5 specifications: https://www.boss.info/products/gigcaster_5/specifications/
- Chromium bug 453876 mirror (chromium-bugs): https://groups.google.com/a/chromium.org/g/chromium-bugs/c/OUWw8OtkcGc
- Chromium issue 40403559: https://issues.chromium.org/issues/40403559
- W3C public-webrtc-logs, Brad Isbell, 22 Nov 2024: https://lists.w3.org/Archives/Public/public-webrtc-logs/2024Nov/0191.html
- W3C public-audio, Hongchan Choi, 2016: https://lists.w3.org/Archives/Public/public-audio/2016JanMar/0001.html
- Chromium source, media/webrtc/audio_processor.cc: https://chromium.googlesource.com/chromium/src/+/main/media/webrtc/audio_processor.cc
- Chromium source, media/audio/win/audio_low_latency_input_win.cc: https://chromium.googlesource.com/chromium/src/+/main/media/audio/win/audio_low_latency_input_win.cc
- Mozilla, channelCount constraint (Sept 2017): https://blog.mozilla.org/webrtc/channelcount-microphone-constraint/
- Bugzilla 1393401: https://bugzilla.mozilla.org/show_bug.cgi?id=1393401
- REAPER about page: https://www.reaper.fm/about.php
- Sound On Sound, REAPER internal channels: https://www.soundonsound.com/techniques/reaper-using-tracks-internal-channels
- Ableton Live 12 manual, Routing and I/O: https://www.ableton.com/manual/routing-and-i-o/
- Ableton help, mono/stereo conversion: https://help.ableton.com/hc/en-us/articles/360009201540-Mono-Stereo-conversion-in-Live
- Ableton forum, multichannel/surround support request: https://forum.ableton.com/viewtopic.php?p=1715436
- Fishman TriplePlay support: https://fishman.com/tripleplay-support/
- Sound On Sound, Fishman TriplePlay review (Aug 2014): https://www.soundonsound.com/reviews/fishman-tripleplay
- muziker.lt, Fishman TriplePlay Wireless: https://www.muziker.lt/fishman-tripleplay-wireless-guitar-controller
- Roland, SY-1000 Reference Manual (PDF, p. 85 USB AUDIO): https://static.roland.com/assets/media/pdf/SY-1000_reference_eng01_W.pdf
- BOSS SY-1000 product page: https://www.boss.info/products/sy-1000/
- gr300.com, SY-1000 (USB input list): https://gr300.com/SY-1000.htm
- gearnews, SY-1000 RRP 999 EUR: https://www.gearnews.com/boss-sy-1000-an-expansive-and-expensive-new-guitar-synth-floor-pedal
- Roland, GM-800 Parameter Guide (PDF, p. 16 USB AUDIO): https://static.roland.com/assets/media/pdf/GM-800_ParameterGuide_eng02_W.pdf
- Sound On Sound, Boss GM-800 review (Nov 2023): https://www.soundonsound.com/reviews/boss-gm-800
- Thomann FR, Boss GM-800: https://www.thomann.fr/boss_gm_800_guitar_synthesizer.htm
- bax-shop.fr, Boss GK-5: https://www.bax-shop.fr/micro-pour-guitare/boss-gk-5-electric-guitar-divided-pickup-micro-de-guitare-pour-synthetiseur-de-guitare
