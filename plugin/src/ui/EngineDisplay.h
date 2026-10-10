// The engine display of the MidPluck editor (the .gtr-tuner box of the web card):
//   MONO  big note name, tuner needle in cents, level meter, TRK / NOTES
//   POLY  voice count and six voice LEDs, ECO tag, level meter, CPU / NOTES
// The big note follows the web card: amber while a pitch is only heard, teal
// while a note is held in the host (MIDI really sent).
// Fed by the editor's 30 Hz timer with a plain value struct (no access to the
// processor); repaints only when what it shows changes. Everything is drawn as
// vectors at each paint (a few primitives): the display sits at a fractional
// device position at most window sizes, where a cached image would be
// resampled and blurred.
#pragma once

#include "JamrackLookAndFeel.h"

namespace jamrack
{
    /** What the display shows; built by the editor from the processor's meters. */
    struct EngineView
    {
        bool poly = false;
        bool bypassed = false;
        bool polyReady = false;
        int note = -1;            // MONO: nearest MIDI note of the confident pitch heard, -1 = none
        int cents = 0;            // MONO: -50..50
        int heldNote = -1;        // MONO: the note held in the host (after octave / transpose), -1 = none
        int litSegments = 0;      // level meter, 0..segments
        int levelDb = -60;        // rounded, for the readout
        int trkMs = -1;           // MONO: onset -> note-on of the last note (TRK of the web card), -1 = none yet
        int notes = 0;            // notes sent
        int voices = 0;           // POLY
        int cpuPct = -1;          // POLY: cost of an analysis hop against its budget, %, -1 = unknown
        bool eco = false;         // POLY

        bool operator== (const EngineView& o) const noexcept
        {
            return poly == o.poly && bypassed == o.bypassed && polyReady == o.polyReady && note == o.note
                && cents == o.cents && heldNote == o.heldNote && litSegments == o.litSegments && levelDb == o.levelDb
                && trkMs == o.trkMs && notes == o.notes && voices == o.voices && cpuPct == o.cpuPct && eco == o.eco;
        }
        bool operator!= (const EngineView& o) const noexcept { return ! (*this == o); }
    };

    class EngineDisplay final : public juce::Component,
                                public juce::SettableTooltipClient
    {
    public:
        static constexpr int segments = 16;
        static constexpr int maxVoices = 6;

        EngineDisplay();

        void setView (const EngineView&);
        const EngineView& getView() const noexcept { return view; }

        /** Level meter segments lit for a level in dBFS (-60..0 dB over the 16 segments). */
        static int segmentsForDb (float db) noexcept;

        /** "E3", "C#4": scientific names (C4 = MIDI 60), as the web card (js/i18n noteName). */
        static juce::String noteName (int midi);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void paintBackground (juce::Graphics&) const;
        void drawReadout (juce::Graphics&, juce::Rectangle<float> area,
                          std::initializer_list<std::pair<juce::String, juce::Colour>> runs) const;

        juce::SharedResourcePointer<Fonts> fonts;
        EngineView view;

        // layout (local coordinates)
        juce::Rectangle<float> noteArea, sideArea, tunerBar, tunerText, meterBox, dbArea, readoutArea, ecoTag, ledRow;
        juce::Rectangle<float> segmentRects[segments];
        juce::Point<float> voiceLeds[maxVoices];

        juce::Font noteFont { juce::FontOptions {} }, idleFont { juce::FontOptions {} }, monoSmall { juce::FontOptions {} },
                   monoStrong { juce::FontOptions {} }, labelFont { juce::FontOptions {} }, tagFont { juce::FontOptions {} },
                   centsFont { juce::FontOptions {} };
        GlowText noteGlow;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineDisplay)
    };
}
