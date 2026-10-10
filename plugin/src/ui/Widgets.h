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
     *  Vertical drag, mouse wheel, double-click = the parameter's default. The tooltip
     *  shows over the dial, the value and the label. */
    class Knob final : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        using Formatter = std::function<juce::String (double)>;

        Knob (juce::RangedAudioParameter&, const juce::String& label, Formatter);

        void setTooltip (const juce::String& t) override
        {
            SettableTooltipClient::setTooltip (t);
            slider.setTooltip (t);
        }
        juce::Slider& getSlider() noexcept { return slider; }

        /** Back to the parameter's default, as one host gesture (the section reset). */
        void resetToDefault();

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Height for a given width: the dial square plus the two text rows. */
        static int heightForWidth (int w) noexcept { return w + 28; }

    private:
        juce::SharedResourcePointer<Fonts> fonts;
        juce::RangedAudioParameter& param;
        juce::Slider slider;
        juce::SliderParameterAttachment attachment;
        juce::ParameterAttachment resetter;     // the section reset's gesture (the slider's attachment does the rest)
        juce::String label;
        Formatter format;
        juce::Font valueFont { juce::FontOptions {} }, labelFont { juce::FontOptions {} };
        juce::Rectangle<float> valueArea, labelArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
    };

    //==============================================================================
    /** "- 0 +" (.stepper): two square buttons around an LCD readout, label just below.
     *  Buttons repeat while held; mouse wheel (unless disabled) and double-click
     *  (default) on the readout. The tooltip shows over the buttons too. */
    class Stepper final : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        Stepper (juce::RangedAudioParameter&, const juce::String& label, bool signedDisplay);

        void setTooltip (const juce::String& t) override
        {
            SettableTooltipClient::setTooltip (t);
            minus.setTooltip (t);
            plus.setTooltip (t);
        }

        /** Off for controls whose change cuts the notes held (MIDI CH): a wheel notch
         *  while scrolling past the window must not change them. */
        void setWheelEnabled (bool on) noexcept { wheelEnabled = on; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        static constexpr int preferredWidth = 96, preferredHeight = 44;

    private:
        void step (int delta);

        juce::SharedResourcePointer<Fonts> fonts;
        juce::RangedAudioParameter& param;
        juce::TextButton minus { juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) }, plus { "+" };   // U+2212, as wide as "+"
        juce::ParameterAttachment attachment;
        juce::String label;
        bool signedDisplay;
        int value = 0, minValue = 0, maxValue = 0;
        juce::Rectangle<float> readout, labelArea;
        juce::Font valueFont { juce::FontOptions {} }, labelFont { juce::FontOptions {} };
        float wheelAccum = 0.0f;
        bool wheelEnabled = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Stepper)
    };

    //==============================================================================
    /** A segmented selector bound to a choice parameter:
     *    switches  MONO | POLY: separate buttons, the chosen one lit amber with dark
     *              text (.layout-switch button.on of the web card); the bounds keep
     *              4 px around the buttons for the lit one's glow
     *    lcd       BEND RANGE 2 | 12 | 24 | 48: a recessed strip, mono figures, caption below */
    class Segmented final : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        enum class Look { switches, lcd };

        Segmented (juce::RangedAudioParameter&, const juce::StringArray& segments, Look,
                   const juce::String& caption = {});

        /** A small superscript after a segment's text (the beta of POLY). */
        void setSuperscript (int segment, const juce::String& text);

        /** Off for MODE: a wheel notch while scrolling past the window must not
         *  switch the engine (that cuts the notes held). */
        void setWheelEnabled (bool on) noexcept { wheelEnabled = on; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        int segmentAt (juce::Point<float>) const;
        juce::Rectangle<float> segmentBounds (int index) const;
        void choose (int index);

        juce::SharedResourcePointer<Fonts> fonts;
        juce::RangedAudioParameter& param;
        juce::StringArray segments;
        Look look;
        juce::String caption;
        int supSegment = -1;
        juce::String supText;
        juce::ParameterAttachment attachment;
        int selected = 0, hovered = -1;
        juce::Rectangle<float> strip, captionArea;
        juce::Font textFont { juce::FontOptions {} }, supFont { juce::FontOptions {} }, captionFont { juce::FontOptions {} };
        float wheelAccum = 0.0f;
        bool wheelEnabled = true;

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
    /** The amber LCD strip of the header (.mod-lcd): one status line with a soft glow;
     *  `warning` = the red style of a transient warning. */
    class Lcd final : public juce::Component,
                      public juce::SettableTooltipClient
    {
    public:
        Lcd();
        void setText (const juce::String& text, bool warning);
        const juce::String& getText() const noexcept { return text; }
        bool isWarning() const noexcept { return warning; }
        void paint (juce::Graphics&) override;

    private:
        juce::SharedResourcePointer<Fonts> fonts;
        juce::String text;
        bool warning = false;
        juce::Font font { juce::FontOptions {} }, altFont { juce::FontOptions {} };
        GlowText glow;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Lcd)
    };

    //==============================================================================
    /** Bottom line: the build label, "MidPluck 0.1.0 (hash)" (every report quotes it). */
    class Footer final : public juce::Component
    {
    public:
        explicit Footer (const juce::String& buildLabel);
        void paint (juce::Graphics&) override;

    private:
        juce::SharedResourcePointer<Fonts> fonts;
        juce::String build;
        juce::Font buildFont { juce::FontOptions {} };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Footer)
    };

    //==============================================================================
    /** The section reset of the web card (.sec-reset, the anticlockwise arrow next to
     *  INPUT and NOTES): faint until hovered, then amber. onClick does the work. */
    class ResetButton final : public juce::Button
    {
    public:
        explicit ResetButton (const juce::String& name);
        void paintButton (juce::Graphics&, bool hover, bool down) override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResetButton)
    };
}
