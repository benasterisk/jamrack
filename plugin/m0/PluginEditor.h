// Generic editor (one slider per parameter) topped by a label naming the
// build: product, version and short git hash, so that every report says
// which build was tested. Plus a counter of the notes emitted since load.

#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

class GtmPrototypeEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit GtmPrototypeEditor (GtmPrototypeProcessor&);
    ~GtmPrototypeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    GtmPrototypeProcessor& processor;
    juce::Label buildLabel, notesLabel;
    juce::GenericAudioProcessorEditor generic;
    int shownNotes = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GtmPrototypeEditor)
};
