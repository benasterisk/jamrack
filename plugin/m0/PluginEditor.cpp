#include "PluginEditor.h"

GtmPrototypeEditor::GtmPrototypeEditor (GtmPrototypeProcessor& p)
    : AudioProcessorEditor (p), processor (p), generic (p)
{
    buildLabel.setText (GtmPrototypeProcessor::buildLabel(), juce::dontSendNotification);
    buildLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    buildLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (buildLabel);

    notesLabel.setFont (juce::FontOptions (13.0f));
    notesLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (notesLabel);

    addAndMakeVisible (generic);
    setSize (420, 150);
    startTimerHz (10);
    timerCallback();
}

GtmPrototypeEditor::~GtmPrototypeEditor() { stopTimer(); }

void GtmPrototypeEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void GtmPrototypeEditor::resized()
{
    auto r = getLocalBounds().reduced (8);
    buildLabel.setBounds (r.removeFromTop (24));
    notesLabel.setBounds (r.removeFromTop (20));
    r.removeFromTop (4);
    generic.setBounds (r);
}

void GtmPrototypeEditor::timerCallback()
{
    const int n = processor.notesEmitted.load (std::memory_order_relaxed);
    if (n != shownNotes)
    {
        shownNotes = n;
        notesLabel.setText ("M0 prototype - notes sent: " + juce::String (n), juce::dontSendNotification);
    }
}
