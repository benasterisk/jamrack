// The MidPluck editor, in the style of the JAMRACK web app's GUITAR -> MIDI card:
// a rack module (warm charcoal face, rack screws, glowing amber knobs, LEDs,
// amber LCD). Header: power LED, brand, status LCD, MODE selector, BYPASS. Main
// row: the engine display (note and tuner in MONO, voices and hop cost in POLY,
// level meter) then the INPUT, NOTES and MIDI OUT groups. Bottom right: the
// build label "MidPluck 0.1.0 (hash)" that every owner's report quotes.
//
// The module is laid out once at baseWidth x baseHeight logical pixels and
// scaled as a whole (resizable 75 % to 200 %, fixed aspect ratio): vector
// drawing stays sharp at any host or Windows scaling.
//
// Threads: message thread only. The editor reads the processor's meters
// (relaxed atomics written by the audio thread) and the parameters' raw values
// at 30 Hz, never locks and never calls into the engines; every control goes
// through an APVTS parameter attachment.
#pragma once

#include "PluginProcessor.h"
#include "ui/JamrackLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

class MidPluckEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    static constexpr int baseWidth = 960;
    static constexpr int baseHeight = 240;

    explicit MidPluckEditor (MidPluckProcessor&);
    ~MidPluckEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The LCD line: "BYPASSED", "POLY loading...", "Listening", "Note E3 · 41 ms",
     *  "3 voices · ECO"... (also used by the snapshot tool). */
    static juce::String statusText (const MidPluckProcessor::Meters&, bool poly, bool bypassed);

    /** Reads the meters and parameters now, as the 30 Hz timer does; `instant` skips
     *  the level meter's fall-off (snapshots). */
    void refresh (bool instant = false);

private:
    class Module;

    void timerCallback() override { refresh(); }

    MidPluckProcessor& processor;
    jamrack::LookAndFeel lnf;
    std::unique_ptr<Module> face;
    juce::TooltipWindow tooltips { this, 800 };

    float shownDb = -60.0f;     // level meter: instant rise, ~36 dB/s fall
    int lastNotes = -1;
    int activityFrames = 0;     // MIDI activity LED: frames left lit
    int lastPoly = -1;
    int lastBad = 0, lastDropped = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidPluckEditor)
};
