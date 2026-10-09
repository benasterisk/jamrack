// The MidPluck editor, in the style of the JAMRACK web app's GUITAR -> MIDI card:
// a rack module (warm charcoal face, rack screws, glowing amber knobs, LEDs,
// amber LCD). Header: power LED, brand, status LCD, MODE selector, BYPASS. Main
// row: the engine display (TUNER in MONO: note and cents; VOICES in POLY:
// voice count and CPU; level meter) then the INPUT, NOTES and MIDI OUT groups.
// Bottom right: the build label "MidPluck 0.1.0 (hash)" that every owner's
// report quotes.
//
// The module is laid out once at baseWidth x baseHeight logical pixels and
// scaled as a whole (resizable 100 % to 200 %, always exactly 4:1 so that the
// face lands on whole pixels): vector drawing stays sharp at any host or
// Windows scaling.
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
    static_assert (baseWidth % baseHeight == 0, "the constrainer keeps width = k x height exactly");

    explicit MidPluckEditor (MidPluckProcessor&);
    ~MidPluckEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The LCD status line (also used by the snapshot tool): "Listening",
     *  "A3 → F#4 · MIDI 66" (a note held while OCTAVE / TRANSPOSE shift it),
     *  "POLY β · plucked notes only", "POLY β · loading…", "POLY β · ECO: lighter analysis",
     *  "BYPASSED · audio passes, no MIDI". `shift` = OCTAVE x 12 + TRANSPOSE. */
    static juce::String statusText (const MidPluckProcessor::Meters&, bool poly, bool bypassed, int shift);

    /** Reads the meters and parameters now, as the 30 Hz timer does; `instant` skips
     *  the level meter's fall-off (snapshots). */
    void refresh (bool instant = false);

    /** How long a warning stays on the LCD after its counter moved: 3 s at 30 Hz. */
    static constexpr int warningFrames = 90;

private:
    class Module;

    /** Fixed aspect ratio with exact sizes: width = (baseWidth / baseHeight) x height,
     *  so the face is never letterboxed by a fraction of a pixel (which would resample
     *  its cached image and soften the text). */
    class ExactRatio final : public juce::ComponentBoundsConstrainer
    {
    public:
        void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                          const juce::Rectangle<int>& limits, bool top, bool left, bool bottom, bool right) override;
    };

    void timerCallback() override { refresh(); }

    MidPluckProcessor& processor;
    const std::atomic<float>& modeValue;     // the parameters' raw values (APVTS atomics), read at 30 Hz
    const std::atomic<float>& bypassValue;
    const std::atomic<float>& octaveValue;
    const std::atomic<float>& transposeValue;
    jamrack::LookAndFeel lnf;
    ExactRatio constrainer;
    std::unique_ptr<Module> face;
    std::unique_ptr<juce::ResizableCornerComponent> grip;   // scaled with the face (JUCE's own stays 18 px)
    juce::TooltipWindow tooltips { this, 800 };

    bool viewShown = false;     // false until the display and the LCD were filled once
    float shownDb = -60.0f;     // level meter: instant rise, ~36 dB/s fall
    int lastNotes = -1;
    int activityFrames = 0;     // NOTE OUT: frames left of the flash after a new note
    int lastPoly = -1;
    int lastBad = 0, lastDropped = 0;
    int warningLeft = 0;        // frames the LCD still shows `warning`
    juce::String warning;
    juce::int64 lastStatusKey = -1;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidPluckEditor)
};
