// The controls of the MidPluck editor, in the JAMRACK style. Every control that
// changes a parameter goes through a JUCE parameter attachment (slider,
// button or plain ParameterAttachment): gestures reach the host, automation
// moves the control. Message thread only.
#pragma once

#include "JamrackLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace jamrack
{
    //==============================================================================
    /** A rotary knob (.knob): dial, value text (mono, amber-hot) and label (letter-spaced caps).
     *  Vertical drag, mouse wheel, double-click = the parameter's default. */
    class Knob final : public juce::Component
    {
    public:
        using Formatter = std::function<juce::String (double)>;

        Knob (juce::RangedAudioParameter&, const juce::String& label, Formatter);

        void setTooltip (const juce::String& t) { slider.setTooltip (t); }
        juce::Slider& getSlider() noexcept { return slider; }

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Height for a given width: the dial square plus the two text rows. */
        static int heightForWidth (int w) noexcept { return w + 28; }

    private:
        juce::SharedResourcePointer<Fonts> fonts;
        juce::Slider slider;
        juce::SliderParameterAttachment attachment;
        juce::String label;
        Formatter format;
        juce::Font valueFont { juce::FontOptions {} }, labelFont { juce::FontOptions {} };
        juce::Rectangle<float> valueArea, labelArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
    };

    //==============================================================================
    /** "- 0 +" (.stepper): two square buttons around an LCD readout, label below.
     *  Buttons repeat while held; mouse wheel and double-click (default) on the readout. */
    class Stepper final : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        Stepper (juce::RangedAudioParameter&, const juce::String& label, bool signedDisplay);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        static constexpr int preferredWidth = 96, preferredHeight = 44;

    private:
        void step (int delta);

        juce::SharedResourcePointer<Fonts> fonts;
        juce::RangedAudioParameter& param;
        juce::TextButton minus { juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) }, plus { "+" };
        juce::ParameterAttachment attachment;
        juce::String label;
        bool signedDisplay;
        int value = 0, minValue = 0, maxValue = 0;
        juce::Rectangle<float> readout, labelArea;
        juce::Font valueFont { juce::FontOptions {} }, labelFont { juce::FontOptions {} };
        float wheelAccum = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Stepper)
    };

    //==============================================================================
    /** A segmented selector bound to a choice parameter: MONO | POLY (with LEDs) or
     *  the BEND RANGE 2 | 12 | 24 | 48 (mono figures, caption below). */
    class Segmented final : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        Segmented (juce::RangedAudioParameter&, const juce::StringArray& segments, bool withLeds,
                   const juce::String& caption = {});

        /** A small superscript after a segment's text (the beta of POLY). */
        void setSuperscript (int segment, const juce::String& text);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        int segmentAt (juce::Point<float>) const;
        void choose (int index);

        juce::SharedResourcePointer<Fonts> fonts;
        juce::RangedAudioParameter& param;
        juce::StringArray segments;
        bool withLeds;
        juce::String caption;
        int supSegment = -1;
        juce::String supText;
        juce::ParameterAttachment attachment;
        int selected = 0, hovered = -1;
        juce::Rectangle<float> strip, captionArea;
        juce::Font textFont { juce::FontOptions {} }, supFont { juce::FontOptions {} }, captionFont { juce::FontOptions {} };
        float wheelAccum = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segmented)
    };

    //==============================================================================
    /** The BYPASS button (LED red when bypassed) and the power button (.mod-power,
     *  LED teal while active): both drive the BYPASS parameter, power inverted. */
    class LedButton final : public juce::Button
    {
    public:
        enum class Style { power, bypass };

        LedButton (juce::RangedAudioParameter& bypassParam, Style);

        void paintButton (juce::Graphics&, bool hover, bool down) override;
        void clicked() override;

    private:
        juce::SharedResourcePointer<Fonts> fonts;
        Style style;
        juce::ParameterAttachment attachment;
        bool bypassed = false;
        juce::Font textFont { juce::FontOptions {} };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LedButton)
    };

    //==============================================================================
    /** The amber LCD strip of the header (.mod-lcd): one status line with a soft glow. */
    class Lcd final : public juce::Component,
                      public juce::SettableTooltipClient
    {
    public:
        Lcd();
        void setText (const juce::String& text, bool warning);
        void paint (juce::Graphics&) override;

    private:
        juce::SharedResourcePointer<Fonts> fonts;
        juce::String text;
        bool warning = false;
        juce::Font font { juce::FontOptions {} };
        GlowText glow;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Lcd)
    };

    //==============================================================================
    /** Bottom line: the build label (every report quotes it) and, to its left, the
     *  input warnings ("bad input samples N", "dropped N") when there are any. */
    class Footer final : public juce::Component
    {
    public:
        explicit Footer (const juce::String& buildLabel);
        void setWarning (const juce::String&);
        void paint (juce::Graphics&) override;

    private:
        juce::SharedResourcePointer<Fonts> fonts;
        juce::String build, warning;
        juce::Font buildFont { juce::FontOptions {} }, warnFont { juce::FontOptions {} };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Footer)
    };
}
