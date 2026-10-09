// midpluck_ui_snapshot: renders the MidPluck editor to PNG files without a DAW.
//
//   midpluck_ui_snapshot <output folder>
//
// Builds the real processor and editor (the same src/ files as the plugin,
// compiled with the same JucePlugin_Name / JucePlugin_VersionString), sets the
// parameters and the meters to realistic states (what the audio thread would
// write), asks the editor to read them (MidPluckEditor::refresh, as its 30 Hz
// timer does) and saves Component::createComponentSnapshot at scale 1.0 and 1.5
// (100 % and 150 % Windows display scaling), plus the window resized by the
// user to 75 % and 200 %. Nothing here runs audio: the meters are written
// directly, the way processBlock stores them (relaxed atomics).

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <iostream>
#include <limits>

namespace
{
    namespace id = midpluck::params::id;

    void setParam (MidPluckProcessor& p, const char* paramId, float plainValue)
    {
        auto* param = p.apvts.getParameter (paramId);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    void defaults (MidPluckProcessor& p)
    {
        for (auto* param : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
                rp->setValueNotifyingHost (rp->getDefaultValue());

        auto& m = p.meters;
        const auto relaxed = std::memory_order_relaxed;
        m.mode.store (midpluck::params::modeMono, relaxed);
        m.bypassed.store (false, relaxed);
        m.polyReady.store (true, relaxed);
        m.db.store (-200.0f, relaxed);
        m.midiF.store (std::numeric_limits<float>::quiet_NaN(), relaxed);
        m.latMs.store (0.0f, relaxed);
        m.voices.store (0, relaxed);
        m.hopMs.store (0.0f, relaxed);
        m.budgetMs.store (2.6666667f, relaxed);   // 48 kHz, block 128 (rule 3.3-6)
        m.eco.store (false, relaxed);
        m.notesSent.store (0, relaxed);
        m.droppedEvents.store (0, relaxed);
        m.badSamples.store (0, relaxed);
    }

    void monoPlaying (MidPluckProcessor& p)
    {
        auto& m = p.meters;
        m.midiF.store (52.12f, std::memory_order_relaxed);     // E3 +12 ct
        m.db.store (-14.0f, std::memory_order_relaxed);
        m.latMs.store (41.0f, std::memory_order_relaxed);
        m.notesSent.store (123, std::memory_order_relaxed);
    }

    void poly (MidPluckProcessor& p, bool ready, int voices, float hop, float budget, bool eco, float db)
    {
        setParam (p, id::mode, (float) midpluck::params::modePoly);
        auto& m = p.meters;
        const auto relaxed = std::memory_order_relaxed;
        m.mode.store (midpluck::params::modePoly, relaxed);
        m.polyReady.store (ready, relaxed);
        m.voices.store (voices, relaxed);
        m.hopMs.store (hop, relaxed);
        m.budgetMs.store (budget, relaxed);
        m.eco.store (eco, relaxed);
        m.db.store (db, relaxed);
        m.notesSent.store (ready ? 123 : 0, relaxed);
    }

    struct State
    {
        const char* name;
        std::function<void (MidPluckProcessor&)> setup;
    };

    /** The editor as a host holds it: editorBeingDeleted() before the delete. */
    struct HostedEditor
    {
        explicit HostedEditor (MidPluckProcessor& p) : processor (p), editor (p.createEditorAndMakeActive()) {}
        ~HostedEditor()
        {
            if (editor != nullptr)
                processor.editorBeingDeleted (editor.get());
        }
        MidPluckEditor* get() const { return dynamic_cast<MidPluckEditor*> (editor.get()); }

        MidPluckProcessor& processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
    };

    bool savePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return out.openedOk() && png.writeImageToStream (image, out);
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::cout << std::unitbuf;

    const auto outDir = argc > 1 ? juce::File (juce::String (juce::CharPointer_UTF8 (argv[1])))
                                 : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-snapshots");
    if (! outDir.createDirectory())
    {
        std::cerr << "cannot create " << outDir.getFullPathName() << "\n";
        return 1;
    }

    const State states[] = {
        { "mono-idle", [] (MidPluckProcessor&) {} },
        { "mono-playing", monoPlaying },
        { "poly-3-voices", [] (MidPluckProcessor& p) { poly (p, true, 3, 0.22f, 2.6666667f, false, -18.0f); } },
        { "poly-loading", [] (MidPluckProcessor& p) { poly (p, false, 0, 0.0f, 2.6666667f, false, -200.0f); } },
        { "poly-eco", [] (MidPluckProcessor& p) { poly (p, true, 4, 1.31f, 1.3333333f, true, -9.0f); } },
        { "bypassed", [] (MidPluckProcessor& p)
          {
              monoPlaying (p);
              setParam (p, id::bypass, 1.0f);
              p.meters.bypassed.store (true, std::memory_order_relaxed);
          } },
        { "mono-warning", [] (MidPluckProcessor& p)
          {
              monoPlaying (p);
              p.meters.badSamples.store (2, std::memory_order_relaxed);
          } },
        { "mono-settings", [] (MidPluckProcessor& p)
          {
              // every control away from its default: value formatting and stepper signs
              monoPlaying (p);
              p.meters.midiF.store (57.46f, std::memory_order_relaxed);   // A3 +46 ct (out of tune: amber needle)
              p.meters.db.store (-4.0f, std::memory_order_relaxed);       // red segments
              setParam (p, id::gain, 2.0f);
              setParam (p, id::sens, 0.8f);
              setParam (p, id::decay, 0.25f);
              setParam (p, id::dyn, 1.0f);
              setParam (p, id::bend, 0.0f);
              setParam (p, id::range, 1.0f);       // 12
              setParam (p, id::octave, 1.0f);
              setParam (p, id::transpose, -3.0f);
              setParam (p, id::channel, 10.0f);
              p.meters.droppedEvents.store (1, std::memory_order_relaxed);
              p.meters.badSamples.store (17, std::memory_order_relaxed);
          } },
    };

    int failures = 0;
    MidPluckProcessor processor;

    for (const auto& state : states)
    {
        defaults (processor);
        processor.editorWidth = 0;
        state.setup (processor);

        HostedEditor hosted (processor);
        auto* editor = hosted.get();
        if (editor == nullptr)
        {
            std::cerr << "no MidPluckEditor\n";
            return 1;
        }
        editor->refresh (true);

        // 100 % and 150 % display scaling for every state, 125 % for one
        const bool with125 = juce::String (state.name) == "mono-playing";
        for (const float scale : { 1.0f, 1.25f, 1.5f })
        {
            if (scale == 1.25f && ! with125)
                continue;
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
            const auto file = outDir.getChildFile (juce::String (state.name) + "@" + juce::String (scale, 2) + "x.png");
            const bool ok = savePng (image, file);
            failures += ok ? 0 : 1;
            std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << "  " << image.getWidth() << " x "
                      << image.getHeight() << "\n";
        }
    }

    // the window resized by the user (fixed aspect ratio, limits 75 %..200 %)
    {
        defaults (processor);
        processor.editorWidth = 0;
        monoPlaying (processor);
        auto hosted = std::make_unique<HostedEditor> (processor);
        auto* editor = hosted->get();
        if (editor == nullptr)
            return 1;
        for (const auto& [name, factor] : { std::pair<const char*, float> { "resized-75", 0.5f }, { "resized-200", 3.0f } })
        {
            // ask for more than the limits: the constrainer clamps and keeps the ratio
            auto* c = editor->getConstrainer();
            auto bounds = juce::Rectangle<int> (juce::roundToInt (MidPluckEditor::baseWidth * factor),
                                                juce::roundToInt (MidPluckEditor::baseHeight * factor));
            c->checkBounds (bounds, editor->getBounds(), juce::Rectangle<int> (0, 0, 10000, 10000), false, false, true, true);
            editor->setSize (bounds.getWidth(), bounds.getHeight());
            editor->refresh (true);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = outDir.getChildFile (juce::String (name) + ".png");
            const bool ok = savePng (image, file);
            failures += ok ? 0 : 1;
            std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << "  " << image.getWidth() << " x "
                      << image.getHeight() << "\n";
        }
        // reopened: the window comes back at the size it was left at
        editor = nullptr;
        hosted = nullptr;
        HostedEditor again (processor);
        if (again.editor == nullptr)
            return 1;
        std::cout << "reopened at " << again.editor->getWidth() << " x " << again.editor->getHeight() << "\n";
    }

    std::cout << "status lines:\n";
    {
        defaults (processor);
        auto& m = processor.meters;
        std::cout << "  mono idle     : " << MidPluckEditor::statusText (m, false, false) << "\n";
        monoPlaying (processor);
        std::cout << "  mono playing  : " << MidPluckEditor::statusText (m, false, false) << "\n";
        std::cout << "  bypassed      : " << MidPluckEditor::statusText (m, false, true) << "\n";
        poly (processor, false, 0, 0.0f, 2.67f, false, -200.0f);
        std::cout << "  poly loading  : " << MidPluckEditor::statusText (m, true, false) << "\n";
        poly (processor, true, 3, 0.22f, 2.67f, true, -18.0f);
        std::cout << "  poly eco      : " << MidPluckEditor::statusText (m, true, false) << "\n";
    }

    return failures == 0 ? 0 : 1;
}
