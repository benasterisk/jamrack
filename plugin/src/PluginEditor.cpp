#include "PluginEditor.h"

#include <cmath>

namespace
{
    constexpr int headerHeight = 64;

    juce::String dbText (float db)
    {
        return db > -120.0f ? juce::String (db, 1) + " dB" : juce::String ("-inf dB");
    }
}

MidPluckEditor::MidPluckEditor (MidPluckProcessor& p)
    : AudioProcessorEditor (p), processor (p), generic (p)
{
    buildLabel.setText (MidPluckProcessor::buildLabel(), juce::dontSendNotification);
    buildLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    buildLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (buildLabel);

    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (generic);
    setSize (juce::jmax (480, generic.getWidth()), headerHeight + generic.getHeight());
    startTimerHz (10);
    timerCallback();
}

MidPluckEditor::~MidPluckEditor() { stopTimer(); }

void MidPluckEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MidPluckEditor::resized()
{
    auto r = getLocalBounds().reduced (8, 4);
    buildLabel.setBounds (r.removeFromTop (22));
    statusLabel.setBounds (r.removeFromTop (headerHeight - 4 - 22 - 4));
    generic.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
}

juce::String MidPluckEditor::statusText (const MidPluckProcessor::Meters& m)
{
    const auto relaxed = std::memory_order_relaxed;
    const int notes = m.notesSent.load (relaxed);
    const juce::String sent = "notes sent " + juce::String (notes);
    if (m.bypassed.load (relaxed))
        return "BYPASSED\n" + sent;

    juce::String line;
    if (m.mode.load (relaxed) == midpluck::params::modePoly)
    {
        if (! m.polyReady.load (relaxed))
            return "POLY loading... (the engine is being built, about 50 ms)\n" + sent;
        line << "POLY (beta)  voices " << m.voices.load (relaxed)
             << "  hop " << juce::String (m.hopMs.load (relaxed), 2) << " ms / budget "
             << juce::String (m.budgetMs.load (relaxed), 2) << " ms";
        if (m.eco.load (relaxed))
            line << "  ECO";
        line << "\n" << dbText (m.db.load (relaxed)) << "  " << sent;
    }
    else
    {
        const float midiF = m.midiF.load (relaxed);
        line << "MONO  ";
        if (std::isfinite (midiF))
        {
            const int note = (int) std::lround (midiF);
            const int cents = (int) std::lround ((midiF - (float) note) * 100.0f);
            line << juce::MidiMessage::getMidiNoteName (note, true, true, 3)
                 << (cents >= 0 ? " +" : " ") << cents << " ct";
        }
        else
        {
            line << "--";
        }
        line << "  latency " << juce::String (m.latMs.load (relaxed), 1) << " ms"
             << "\n" << dbText (m.db.load (relaxed)) << "  " << sent;
    }
    if (const int dropped = m.droppedEvents.load (relaxed); dropped > 0)
        line << "  dropped " << dropped;
    return line;
}

void MidPluckEditor::timerCallback()
{
    const auto text = statusText (processor.meters);
    if (text != statusLabel.getText())
        statusLabel.setText (text, juce::dontSendNotification);
}
