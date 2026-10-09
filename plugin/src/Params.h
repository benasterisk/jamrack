// MidPluck parameters (docs/plugin-plan.md, contract 3.1). The ids are stable:
// hosts store automation and presets under them, never rename one.
//
//   id         label        range / default            effect
//   gain       GAIN         0.1..10 / 1                multiplies the input before the engines
//   sens       SENS         0..1 / 0.5                 MONO: gate -36..-60 dBFS; no effect in POLY
//   decay      DECAY        0..1 / 0.5                 MONO: note-off 15..45 dB below the peak; POLY fixed (20 dB)
//   dyn        DYN          0..1 / 0.7                 MONO velocity law
//   bend       BEND         on/off / on                MONO: continuous bend or chromatic retrigger
//   range      BEND RANGE   2/12/24/48 semitones / 2   14-bit wheel = 8192 + round(semis / range * 8192), 0..16383
//   octave     OCTAVE       -2..2 / 0                  shift x12
//   transpose  TRANSPOSE    -12..12 / 0                shift in semitones
//   mode       MODE         MONO / POLY (beta) / MONO  engine switch, with flush + reset
//   channel    MIDI CH      1..16 / 1                  output channel
//   bypass     BYPASS       on/off / off               returned by getBypassParameter()
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <memory>

namespace midpluck::params
{
    namespace id
    {
        inline constexpr const char* gain = "gain";
        inline constexpr const char* sens = "sens";
        inline constexpr const char* decay = "decay";
        inline constexpr const char* dyn = "dyn";
        inline constexpr const char* bend = "bend";
        inline constexpr const char* range = "range";
        inline constexpr const char* octave = "octave";
        inline constexpr const char* transpose = "transpose";
        inline constexpr const char* mode = "mode";
        inline constexpr const char* channel = "channel";
        inline constexpr const char* bypass = "bypass";
    }

    enum Mode : int { modeMono = 0, modePoly = 1 };

    /** BEND RANGE choices, in semitones (index = the choice parameter's value). */
    inline constexpr int rangeSemis[4] = { 2, 12, 24, 48 };

    inline juce::StringArray rangeChoices() { return { "2", "12", "24", "48" }; }
    inline juce::StringArray modeChoices() { return { "MONO", "POLY (beta)" }; }

    /**
     * A slider value (float) as the double the JS page would hold: the nearest
     * multiple of 1e-6. The float 0.7f is 0.699999988..., the JS default is
     * 0.7; without this the MONO velocity law would differ by an ulp-sized
     * amount from DEFAULT_PARAMS at the default setting.
     */
    inline double sliderValue (float v) noexcept
    {
        return std::round (static_cast<double> (v) * 1e6) / 1e6;
    }

    /**
     * An integer parameter hosts see as stepped. JUCE's AudioParameterInt is not
     * discrete for its VST3 wrapper (stepCount 0): Live would show OCTAVE,
     * TRANSPOSE and MIDI CH as continuous controls and keep unsnapped values.
     * Same ids and linear mapping, so states and automation stay valid.
     */
    class SteppedInt final : public juce::AudioParameterInt
    {
    public:
        using juce::AudioParameterInt::AudioParameterInt;
        bool isDiscrete() const override { return true; }
    };

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto twoDecimals = AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 2); });

        NormalisableRange<float> gainRange (0.1f, 10.0f, 0.01f);
        gainRange.setSkewForCentre (1.0f);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id::gain, 1 }, "GAIN", gainRange, 1.0f,
                                                           twoDecimals.withLabel ("x")));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id::sens, 1 }, "SENS",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f, twoDecimals));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id::decay, 1 }, "DECAY",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f, twoDecimals));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id::dyn, 1 }, "DYN",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.7f, twoDecimals));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id::bend, 1 }, "BEND", true));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id::range, 1 }, "BEND RANGE", rangeChoices(), 0,
                                                            AudioParameterChoiceAttributes().withLabel ("st")));
        layout.add (std::make_unique<SteppedInt> (ParameterID { id::octave, 1 }, "OCTAVE", -2, 2, 0));
        layout.add (std::make_unique<SteppedInt> (ParameterID { id::transpose, 1 }, "TRANSPOSE", -12, 12, 0,
                                                         AudioParameterIntAttributes().withLabel ("st")));
        // "poly", "POLY (beta)", "1" all select POLY; anything else MONO (host text entry, vst3_probe --param mode=poly)
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id::mode, 1 }, "MODE", modeChoices(), modeMono,
                                                            AudioParameterChoiceAttributes().withValueFromStringFunction (
                                                                [] (const String& text)
                                                                {
                                                                    const auto t = text.trim().toLowerCase();
                                                                    return (t.startsWith ("poly") || t == "1") ? int (modePoly) : int (modeMono);
                                                                })));
        layout.add (std::make_unique<SteppedInt> (ParameterID { id::channel, 1 }, "MIDI CH", 1, 16, 1));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id::bypass, 1 }, "BYPASS", false));
        return layout;
    }
}
