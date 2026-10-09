// Generic editor (one control per parameter) topped by two labels: the build
// (product, version, short git hash: every report names the build it tested)
// and a status line refreshed at 10 Hz from the processor's meters
// (atomics written by the audio thread): mode, note + cents and onset -> note
// latency (MONO), voices, cost per hop and ECO tag (POLY), level, notes sent.

#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

class MidPluckEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit MidPluckEditor (MidPluckProcessor&);
    ~MidPluckEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The status line for the current meters (also used by tests). */
    static juce::String statusText (const MidPluckProcessor::Meters&);

private:
    void timerCallback() override;

    MidPluckProcessor& processor;
    juce::Label buildLabel, statusLabel;
    juce::GenericAudioProcessorEditor generic;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidPluckEditor)
};
