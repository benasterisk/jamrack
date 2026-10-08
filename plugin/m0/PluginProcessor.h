// M0 throw-away prototype (docs/plugin-plan.md, section 5): measures what
// Ableton Live 12 does with MIDI coming out of a VST3, before any DSP is
// ported. One source, two targets (JAMRACK_IS_INST selects the variant):
//
//   Fx   (Gtm0) audio effect on the guitar track: audio in = audio out
//               (pass-through), MIDI out
//   Inst (Gtm9) instrument with ONE auxiliary "Sidechain" input and no main
//               input, so that Live offers an "Audio From" side-chain
//               selector; silent audio out, MIDI out
//
// What it emits: a crude attack detector on channel 0 plays note 60,
// velocity 100, at the sample of the attack, held 600 ms; DURING the note the
// pitch wheel ramps 8192 -> 12288 (100..250 ms) then back to 8192
// (250..400 ms), one message every 64 samples; the note-off at 600 ms carries
// the wheel back to 8192 at the same sample. A rising-then-falling half-tone
// bend (at the usual +/-2 semitone range) tells the owner, by ear, whether
// Live 12 routes VST3 pitch bend to the receiving instrument.
//
// Attack = a RISE: the 5 ms mean-square envelope is above -45 dBFS AND at
// least twice a 30 ms one, which takes a sudden jump of about 6 dB. Re-armed
// once the note is over and the rise has died down (fast below 1.25 x slow,
// or below -45 dBFS), so a string left ringing does not block the next pluck
// (an absolute re-arm level did: 1 note for 10 replucks), and an attack
// during the held note is ignored, never replayed late. Reset, re-prepare
// and bypass keep the envelopes and disarm: a string still ringing then
// plays nothing. A NaN/Inf input clears the envelopes at the next block.
// Measured on GuitarSet (360 pickup recordings, attacks that arrive while no
// note sounds; plugin/m0/sim_detector.py): 72 / 73 / 71 / 61 % caught at a
// -6 / -12 / -18 / -24 dBFS peak, 87 to 96 % of the notes on a real attack
// (the first rule, -30 dBFS and re-arm below -40: 39 / 37 / 13 / 0 %).
//
// Real-time rules (plan 3.3): nothing in processBlock allocates, locks or
// sorts; parameters are read through the atomic raw value.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

#ifndef JAMRACK_IS_INST
 #define JAMRACK_IS_INST 0
#endif

class GtmPrototypeProcessor final : public juce::AudioProcessor
{
public:
    GtmPrototypeProcessor();
    ~GtmPrototypeProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    // Inst: an event input bus, required by Live for instruments (incoming MIDI is discarded)
    bool acceptsMidi() const override { return JAMRACK_IS_INST != 0; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    // a named default program: the Steinberg validator fails an unnamed one
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

   #if JAMRACK_IS_INST
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &vst3Extensions; }
   #endif

    /** "JAMRACK GTM Fx 0.0.1 (abc1234)": what the editor shows. */
    static juce::String buildLabel();

    juce::AudioProcessorValueTreeState params;

    /** Read by the editor (message thread): notes emitted since load. */
    std::atomic<int> notesEmitted { 0 };

private:
   #if JAMRACK_IS_INST
    // Presents the only input bus as auxiliary: JUCE's VST3 wrapper reports
    // the first input bus as kMain unless getPluginHasMainInput() is false,
    // and Live would then not offer an "Audio From" side-chain selector.
    struct NoMainInput final : juce::VST3ClientExtensions
    {
        bool getPluginHasMainInput() const override { return false; }
    };
    NoMainInput vst3Extensions;
   #endif

    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void resetState() noexcept;
    void trackEnvelope (const float* in, int n) noexcept;   // bypass: follow the input, no notes

    std::atomic<float>* channelParam = nullptr;

    double sampleRate = 44100.0;
    double fastCoef = 0.0, slowCoef = 0.0;   // one-pole smoothing of x^2 (5 ms, 30 ms)
    double envFast = 0.0, envSlow = 0.0;     // mean squares of channel 0
    bool armed = false;            // set once the note is over and the rise has died down
    bool noteOn = false;           // note 60 is sounding
    int noteChannel = 1;           // channel the sounding note was sent on
    int64_t sinceOn = 0;           // samples since the note-on
    int samplesToNextBend = 0;     // countdown to the next pitch-wheel message
    int lastWheel = 8192;
    bool flushPending = false;     // a note was cut by prepare/reset/release

    int64_t rampStart = 0, rampPeak = 0, rampEnd = 0, noteLength = 0;

    static constexpr double triggerDb = -45.0;   // fast envelope floor
    static constexpr double riseRatio = 2.0;     // fast / slow (mean squares): a ~6 dB jump
    static constexpr double rearmRatio = 1.25;   // hysteresis: the rise must die down
    static constexpr double fastSeconds = 0.005, slowSeconds = 0.030;
    static constexpr int bendEvery = 64;
    static constexpr int note = 60;
    static constexpr int velocity = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GtmPrototypeProcessor)
};
