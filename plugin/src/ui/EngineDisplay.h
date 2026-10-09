// The engine display of the MidPluck editor (the .gtr-tuner box of the web card):
//   MONO  big note name (teal), tuner needle in cents, level meter, LAT / NOTES
//   POLY  voice count and six voice LEDs, level meter, HOP cost / budget, ECO tag
// Fed by the editor's 30 Hz timer with a plain value struct (no access to the
// processor); repaints only when what it shows changes. Its static parts (the
// recessed box, the tuner scale, the unlit segments) are drawn once into an
// image per scale.
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
        int note = -1;            // MONO: nearest MIDI note of the confident pitch, -1 = none
        int cents = 0;            // MONO: -50..50
        int litSegments = 0;      // level meter, 0..segments
        int levelDb = -60;        // rounded, for the readout
        int latMs = -1;           // MONO: onset -> note-on of the last note, -1 = none yet
        int notes = 0;            // notes sent
        int voices = 0;           // POLY
        int hopUs = 0;            // POLY: cost per hop, microseconds
        int budgetUs = 0;         // POLY: ECO budget, microseconds
        bool eco = false;         // POLY

        bool operator== (const EngineView& o) const noexcept
        {
            return poly == o.poly && bypassed == o.bypassed && polyReady == o.polyReady && note == o.note
                && cents == o.cents && litSegments == o.litSegments && levelDb == o.levelDb && latMs == o.latMs
                && notes == o.notes && voices == o.voices && hopUs == o.hopUs && budgetUs == o.budgetUs && eco == o.eco;
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

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void renderBackground (float scale);
        void drawReadout (juce::Graphics&, juce::Rectangle<float> area,
                          std::initializer_list<std::pair<juce::String, bool>> runs) const;

        juce::SharedResourcePointer<Fonts> fonts;
        EngineView view;
        juce::Image background;
        float backgroundScale = 0.0f;

        // layout (local coordinates)
        juce::Rectangle<float> noteArea, sideArea, tunerBar, tunerText, meterBox, dbArea, readoutArea, ecoTag, voicesLabel;
        juce::Rectangle<float> segmentRects[segments];
        juce::Point<float> voiceLeds[maxVoices];

        juce::Font noteFont { juce::FontOptions {} }, monoSmall { juce::FontOptions {} }, labelFont { juce::FontOptions {} }, tagFont { juce::FontOptions {} }, centsFont { juce::FontOptions {} };
        float monoAdvance = 7.0f;
        GlowText noteGlow;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineDisplay)
    };
}
