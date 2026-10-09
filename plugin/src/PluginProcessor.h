// MidPluck: guitar -> MIDI, VST3 audio effect (docs/plugin-plan.md, M2 + M3 task 5).
//
// Audio in = audio out (pass-through, bit for bit): the guitar track keeps
// sounding. The engine reads channel 0 and the plugin emits MIDI that a MIDI
// track takes with "MIDI From" (Ableton Live 12) or that the next FX of the
// chain receives (REAPER). Two engines, both ports of the browser engines of
// this repo (the JavaScript is canonical):
//   MONO  dsp/mono_tracker   js/audio/guitar/tracker.js       one note at a time, pitch bend
//   POLY  dsp/poly/engine    js/audio/guitar/poly/engine.js   beta, no bend, generic profile
//
// MIDI out (contract 3.2): note-on clamped to 0..127, velocity
// clamp(round(v * 127), 1, 127); explicit note-offs; the `sounding` set of
// js/input/guitar.js (MONO: a note-off naming a pitch not held releases
// everything held); 14-bit pitch wheel from BEND RANGE (MONO), back to 8192
// at every note-on and note-off; a flush (note-offs of everything held +
// wheel 8192) at prepareToPlay, reset, releaseResources, bypass, a mode
// change, an octave/transpose (or MIDI CH) change; events stamped at the
// sample that completed the analysis frame/hop; latency 0.
//
// Threads (rules 3.3-1 and 3.3-5):
//   audio      processBlock: no allocation, no lock, no std::function; the
//              parameters are read through APVTS atomics
//   message    a 100 ms timer frees retired POLY states and asks for rebuilds;
//              the editor reads the meters (atomics) at 10 Hz
//   builder    a juce::Thread renders the POLY state (bank, decomposer, note
//              rule, tracker: tens of ms) at the host rate; it is published
//              through an atomic pointer and swapped in at the top of
//              processBlock; the old one goes back through an AbstractFifo,
//              never deleted on the audio thread
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "midpluck/events.h"
#include "midpluck/mono_tracker.h"
#include "midpluck/poly/engine.h"
#include "Params.h"
#include "Sounding.h"

#include <atomic>
#include <limits>
#include <memory>

class MidPluckProcessor final : public juce::AudioProcessor,
                                private juce::Timer
{
public:
    MidPluckProcessor();
    ~MidPluckProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

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

    /** Our own BYPASS parameter: the VST3 wrapper then calls processBlock (never
     *  processBlockBypassed) and the flush at the bypass transition is ours. */
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    /** "MidPluck 0.1.0 (abc1234)": what the editor shows ("+" = uncommitted changes). */
    static juce::String buildLabel();

    juce::AudioProcessorValueTreeState apvts;

    /** Written by the audio thread, read by the editor at 10 Hz (relaxed atomics). */
    struct Meters
    {
        std::atomic<int> mode { midpluck::params::modeMono };
        std::atomic<bool> bypassed { false };
        std::atomic<bool> polyReady { false };
        std::atomic<float> db { -200.0f };            // level of the last meter event, dBFS
        std::atomic<float> midiF { std::numeric_limits<float>::quiet_NaN() };   // MONO: confident pitch, NaN otherwise
        std::atomic<float> latMs { 0.0f };            // MONO: onset -> note-on of the last note
        std::atomic<int> voices { 0 };                // POLY
        std::atomic<float> hopMs { 0.0f };           // POLY: cost per hop (exponential average)
        std::atomic<float> budgetMs { 0.0f };        // POLY: ECO budget (rule 3.3-6)
        std::atomic<bool> eco { false };              // POLY
        std::atomic<int> notesSent { 0 };
        std::atomic<int> droppedEvents { 0 };         // engine events that did not fit the list (should stay 0)
        std::atomic<int> badSamples { 0 };            // NaN/Inf input samples replaced by 0 before the engines
    };
    Meters meters;

private:
    using PolyEngineState = midpluck::poly::PolyEngineState;

    /** A POLY state with the host rate its resampler was built for. */
    struct PolyHolder
    {
        explicit PolyHolder (double sampleRate) : rate (sampleRate), state (sampleRate) {}
        double rate;
        PolyEngineState state;
    };

    /** Renders PolyHolder objects off the audio thread (rule 3.3-5). */
    class PolyBuilder final : public juce::Thread
    {
    public:
        explicit PolyBuilder (MidPluckProcessor& p) : juce::Thread ("MidPluck POLY builder"), owner (p) {}
        void run() override;

    private:
        MidPluckProcessor& owner;
    };

    static BusesProperties makeBuses();
    void timerCallback() override;                  // message thread, 100 ms

    // ---- POLY state life cycle
    void requestPolyBuild();                        // message thread / prepareToPlay
    void installPolyState (bool emit) noexcept;     // audio thread, top of processBlock
    bool retire (PolyHolder* h) noexcept;           // audio thread (or prepareToPlay)
    void drainRetired();                            // message thread

    // ---- audio thread helpers
    midpluck::MonoParams monoParamsNow() const noexcept;
    void dispatch (const midpluck::EventList& list, int base) noexcept;
    void flushActive (int at) noexcept;             // engine flush + note-offs of `sounding` + wheel 8192
    void flushSilently() noexcept;                  // the same, nothing sent (bypassed)
    void resetActive() noexcept;
    void resetEngines() noexcept;
    void releaseAll (int at) noexcept;
    void sendWheel (int value, int at) noexcept;

    juce::AudioParameterBool* bypassParam = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pSens = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pDyn = nullptr;
    std::atomic<float>* pBend = nullptr;
    std::atomic<float>* pRange = nullptr;
    std::atomic<float>* pOctave = nullptr;
    std::atomic<float>* pTranspose = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pChannel = nullptr;
    std::atomic<float>* pBypass = nullptr;

    // ---- engines
    std::unique_ptr<midpluck::MonoTracker> mono;    // rebuilt by prepareToPlay when the rate changes
    double monoRate = 0.0;
    midpluck::MonoParams monoApplied;

    PolyHolder* polyCurrent = nullptr;              // owned by the audio side (processBlock / prepareToPlay)
    std::atomic<PolyHolder*> polyNext { nullptr };  // published by the builder, taken by the audio thread
    std::atomic<double> polyWantedRate { 0.0 };
    std::atomic<bool> polyBuildRequested { false };
    std::atomic<bool> polyRebuildNeeded { false };  // audio thread -> timer: a state came with a stale rate
    PolyBuilder builder { *this };
    static constexpr int retireCapacity = 32;
    juce::AbstractFifo retireFifo { retireCapacity };
    PolyHolder* retireSlots[retireCapacity] = {};

    // ---- host
    double hostRate = 48000.0;
    int hostBlock = 512;
    double ecoBudgetMs = 0.0;

    // ---- shell state (audio thread)
    std::atomic<bool> shellFlushPending { false };  // prepareToPlay: engines already fresh, release what the host holds
    std::atomic<bool> engineResetPending { false }; // reset() / releaseResources(): flush, then reset the engines
    int activeMode = midpluck::params::modeMono;
    bool wasBypassed = false;
    int curChannel = 1;
    int lastWheel = -1;                             // last pitch-wheel value sent, -1 = unknown
    int appliedOctave = 0, appliedTranspose = 0;
    int bendRange = 2;
    midpluck::Sounding sounding;
    juce::MidiBuffer* midiOut = nullptr;            // valid during processBlock only
    int blockSamples = 0;

    static constexpr int maxChunk = 512;            // engines are fed at most this many samples per call
    midpluck::EventList events;                     // filled by the engines, cleared per chunk
    float scratch[maxChunk] = {};                   // input x gain (a fixed array: any host block is chunked)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidPluckProcessor)
};
