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
// What it emits: a crude attack detector (RMS envelope of channel 0, trigger
// above -30 dBFS, re-arm below -40 dBFS) plays note 60, velocity 100, at the
// sample of the attack, held 600 ms; DURING the note the pitch wheel ramps
// 8192 -> 12288 (100..250 ms) then back to 8192 (250..400 ms), one message
// every 64 samples; the note-off at 600 ms carries the wheel back to 8192 at
// the same sample. A rising-then-falling one-tone bend tells the owner, by
// ear, whether Live 12 routes VST3 pitch bend to the receiving instrument.
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
    bool acceptsMidi() const override { return false; }
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

    std::atomic<float>* channelParam = nullptr;

    double sampleRate = 44100.0;
    double envCoef = 0.0;          // one-pole smoothing of x^2 (5 ms)
    double env = 0.0;              // mean square of channel 0
    bool armed = true;             // may an attack trigger a note?
    bool noteOn = false;           // note 60 is sounding
    int noteChannel = 1;           // channel the sounding note was sent on
    int64_t sinceOn = 0;           // samples since the note-on
    int samplesToNextBend = 0;     // countdown to the next pitch-wheel message
    int lastWheel = 8192;
    bool flushPending = false;     // a note was cut by prepare/reset/release

    int64_t rampStart = 0, rampPeak = 0, rampEnd = 0, noteLength = 0;

    static constexpr double triggerDb = -30.0;
    static constexpr double rearmDb = -40.0;
    static constexpr int bendEvery = 64;
    static constexpr int note = 60;
    static constexpr int velocity = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GtmPrototypeProcessor)
};
