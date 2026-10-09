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
// user to its smallest and largest sizes and the window opened on a processor
// the host has not prepared yet. Nothing here runs audio: the meters are
// written directly, the way processBlock stores them (relaxed atomics).
//
// It also checks, and exits with code 1 when a PNG cannot be written or a check
// fails:
//   - the wiring of every control, both ways (control -> parameter, host ->
//     control), the section resets, the wheel rules (never on MODE and MIDI CH),
//     the tooltips over the stepper buttons and the knob texts, the dimming of
//     the MONO-only controls in POLY;
//   - what each state shows: the LCD line, the big note's colour (amber when a
//     pitch is only heard, teal while a note is held), NOTE OUT;
//   - the parameter text hosts show (GAIN in dB, SENS / DECAY / DYN 0..100) and
//     that every text reads back to its value;
//   - window sizes: exactly 4:1 whatever the drag, limits 100 %..200 %.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/EngineDisplay.h"
#include "ui/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <tuple>

namespace
{
    namespace id = midpluck::params::id;

    int checks = 0, failed = 0;

    void expect (const juce::String& what, bool ok)
    {
        ++checks;
        if (! ok)
        {
            ++failed;
            std::cout << "  FAILED: " << what << "\n";
        }
    }

    void setParam (MidPluckProcessor& p, const char* paramId, float plainValue)
    {
        auto* param = p.apvts.getParameter (paramId);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    float value (MidPluckProcessor& p, const char* paramId)
    {
        auto* rp = p.apvts.getParameter (paramId);
        return rp->convertFrom0to1 (rp->getValue());
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
        m.notesHeld.store (0, relaxed);
        m.noteOut.store (-1, relaxed);
        m.droppedEvents.store (0, relaxed);
        m.badSamples.store (0, relaxed);
    }

    /** A pitch heard (E3 + 12 ct) and, with `held`, its note held in the host. */
    void monoPlaying (MidPluckProcessor& p, bool held = true)
    {
        auto& m = p.meters;
        const auto relaxed = std::memory_order_relaxed;
        m.midiF.store (52.12f, relaxed);
        m.db.store (-14.0f, relaxed);
        m.latMs.store (41.0f, relaxed);
        m.notesSent.store (123, relaxed);
        m.noteOut.store (52, relaxed);
        m.notesHeld.store (held ? 1 : 0, relaxed);
    }

    void poly (MidPluckProcessor& p, bool ready, int voices, float hop, float budget, bool eco, float db)
    {
        setParam (p, id::mode, (float) midpluck::params::modePoly);
        auto& m = p.meters;
        const auto relaxed = std::memory_order_relaxed;
        m.mode.store (midpluck::params::modePoly, relaxed);
        m.polyReady.store (ready, relaxed);
        m.voices.store (voices, relaxed);
        m.notesHeld.store (voices, relaxed);
        m.hopMs.store (hop, relaxed);
        m.budgetMs.store (budget, relaxed);
        m.eco.store (eco, relaxed);
        m.db.store (db, relaxed);
        m.notesSent.store (ready ? 123 : 0, relaxed);
    }

    enum class Hue { none, amber, teal };

    struct State
    {
        const char* name;
        std::function<void (MidPluckProcessor&)> setup;                     // before the editor opens
        std::function<void (MidPluckProcessor&)> afterOpen;                 // then, before the snapshot (may be null)
        juce::String lcd;                                                   // the LCD line expected
        Hue bigFigure = Hue::none;                                          // colour of the big note / voice count
        bool noteOut = false;                                               // NOTE OUT lit
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

    /** The first descendant of `root` of type T whose title or name is `key` (any if empty). */
    template <typename T>
    T* find (juce::Component& root, const juce::String& key)
    {
        for (auto* c : root.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c); t != nullptr && (key.isEmpty() || c->getTitle() == key || c->getName() == key))
                return t;
            if (auto* r = find<T> (*c, key))
                return r;
        }
        return nullptr;
    }

    /** The NOTE OUT LED: the only TooltipClient whose tooltip starts with "Lit while". */
    juce::Component* findActivity (juce::Component& root)
    {
        for (auto* c : root.getChildren())
        {
            if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (c); t != nullptr && t->getTooltip().startsWith ("Lit while"))
                return c;
            if (auto* r = findActivity (*c))
                return r;
        }
        return nullptr;
    }

    /** A plain left click (or double-click) at `pos`, local to `c`. */
    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier,
                                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                 juce::MouseInputSource::defaultTiltY, &c, &c, now, pos, now, clicks, false);
    }

    /** One notch of a Windows mouse wheel, up (JUCE: 60/256 per notch). */
    juce::MouseWheelDetails notch()
    {
        juce::MouseWheelDetails w;
        w.deltaX = 0.0f;
        w.deltaY = 60.0f / 256.0f;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        return w;
    }

    /** The hue of the brightest pixels of `area` (snapshot pixels). */
    Hue hueOf (const juce::Image& image, juce::Rectangle<int> area)
    {
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
        int best = -1;
        juce::Colour bright;
        for (int y = juce::jmax (0, area.getY()); y < juce::jmin (image.getHeight(), area.getBottom()); ++y)
            for (int x = juce::jmax (0, area.getX()); x < juce::jmin (image.getWidth(), area.getRight()); ++x)
            {
                const auto c = data.getPixelColour (x, y);
                const int sum = c.getRed() + c.getGreen() + c.getBlue();
                if (sum > best) { best = sum; bright = c; }
            }
        if (best < 300)
            return Hue::none;   // nothing lit: the idle dash
        if (bright.getGreen() > bright.getRed() && bright.getBlue() > bright.getRed())
            return Hue::teal;
        if (bright.getRed() > bright.getBlue() + 60)
            return Hue::amber;
        return Hue::none;
    }

    const char* hueName (Hue h) { return h == Hue::teal ? "teal" : (h == Hue::amber ? "amber" : "none"); }

    /**
     * Every parameter reachable from the editor, both ways: the control moves the
     * parameter (what a click, a drag or a wheel step does) and the parameter moved
     * by the host moves the control.
     */
    void checkControls (MidPluckProcessor& p, MidPluckEditor& editor)
    {
        // knobs: drag/wheel move the slider, the attachment moves the parameter; double-click = default
        for (const auto& [label, paramId, setTo] : { std::tuple<const char*, const char*, double> { "GAIN", id::gain, 2.0 },
                                                     { "SENS", id::sens, 0.8 }, { "DECAY", id::decay, 0.25 }, { "DYN", id::dyn, 1.0 } })
        {
            auto* s = find<juce::Slider> (editor, label);
            expect (juce::String (label) + " knob found", s != nullptr);
            if (s == nullptr)
                continue;
            s->setValue (setTo, juce::sendNotificationSync);
            expect (juce::String (label) + " knob -> parameter", std::abs (value (p, paramId) - setTo) < 0.011);
            auto* rp = p.apvts.getParameter (paramId);
            expect (juce::String (label) + " double-click = default",
                    s->isDoubleClickReturnEnabled()
                        && std::abs (s->getDoubleClickReturnValue() - rp->convertFrom0to1 (rp->getDefaultValue())) < 1e-6);
            expect (juce::String (label) + " wheel and vertical drag",
                    s->isScrollWheelEnabled() && s->getSliderStyle() == juce::Slider::RotaryVerticalDrag);
            auto* knob = dynamic_cast<jamrack::Knob*> (s->getParentComponent());
            expect (juce::String (label) + " tooltip over its value and label",
                    knob != nullptr && knob->getTooltip().isNotEmpty() && knob->getTooltip() == s->getTooltip());
            setParam (p, paramId, rp->convertFrom0to1 (rp->getDefaultValue()));
            expect (juce::String (label) + " parameter -> knob",
                    std::abs (s->getValue() - rp->convertFrom0to1 (rp->getDefaultValue())) < 1e-6);
        }

        // section resets: INPUT -> GAIN, SENS; NOTES -> DECAY, DYN
        for (const auto& [name, a, b] : { std::tuple<const char*, const char*, const char*> { "Reset INPUT", id::gain, id::sens },
                                          { "Reset NOTES", id::decay, id::dyn } })
        {
            auto* reset = find<jamrack::ResetButton> (editor, name);
            expect (juce::String (name) + " button found", reset != nullptr);
            if (reset == nullptr)
                continue;
            setParam (p, a, 0.9f);
            setParam (p, b, 0.9f);
            reset->onClick();
            auto* ra = p.apvts.getParameter (a);
            auto* rb = p.apvts.getParameter (b);
            expect (juce::String (name) + " -> defaults",
                    std::abs (ra->getValue() - ra->getDefaultValue()) < 1e-6 && std::abs (rb->getValue() - rb->getDefaultValue()) < 1e-6);
        }

        // MODE: a click on a segment; never the wheel
        if (auto* mode = find<jamrack::Segmented> (editor, "MODE"))
        {
            mode->mouseWheelMove (mouseAt (*mode, { 10.0f, 10.0f }), notch());
            mode->mouseWheelMove (mouseAt (*mode, { 10.0f, 10.0f }), notch());
            expect ("a wheel notch over MODE changes nothing", juce::roundToInt (value (p, id::mode)) == midpluck::params::modeMono);
            mode->mouseDown (mouseAt (*mode, { (float) mode->getWidth() * 0.75f, (float) mode->getHeight() * 0.5f }));
            expect ("click POLY -> mode = POLY", juce::roundToInt (value (p, id::mode)) == midpluck::params::modePoly);
            editor.refresh (true);
            auto* sens = find<juce::Slider> (editor, "SENS");
            expect ("POLY dims SENS (still enabled)", sens != nullptr && sens->getParentComponent()->getAlpha() < 0.5f && sens->isEnabled());
            mode->mouseDown (mouseAt (*mode, { (float) mode->getWidth() * 0.25f, (float) mode->getHeight() * 0.5f }));
            expect ("click MONO -> mode = MONO", juce::roundToInt (value (p, id::mode)) == midpluck::params::modeMono);
            editor.refresh (true);
            expect ("MONO restores SENS", sens != nullptr && sens->getParentComponent()->getAlpha() == 1.0f);
        }
        else
            expect ("MODE selector found", false);

        if (auto* range = find<jamrack::Segmented> (editor, "BEND RANGE"))
        {
            range->mouseDown (mouseAt (*range, { (float) range->getWidth() * 2.5f / 4.0f, 10.0f }));
            expect ("click 24 -> BEND RANGE = 24", juce::roundToInt (value (p, id::range)) == 2);
            range->mouseWheelMove (mouseAt (*range, { 10.0f, 10.0f }), notch());
            expect ("a wheel notch over BEND RANGE steps it (24 -> 48)", juce::roundToInt (value (p, id::range)) == 3);
            setParam (p, id::range, 0.0f);
        }
        else
            expect ("BEND RANGE selector found", false);

        // BEND pill switch
        if (auto* bend = find<juce::ToggleButton> (editor, "BEND"))
        {
            bend->setToggleState (false, juce::sendNotificationSync);
            expect ("BEND switch -> bend off", value (p, id::bend) < 0.5f);
            setParam (p, id::bend, 1.0f);
            expect ("bend on -> BEND switch", bend->getToggleState());
        }
        else
            expect ("BEND switch found", false);

        // BYPASS and the power LED both drive the bypass parameter (power inverted)
        auto* bypass = find<jamrack::LedButton> (editor, "BYPASS");
        auto* power = find<jamrack::LedButton> (editor, "Power");
        expect ("BYPASS and power buttons found", bypass != nullptr && power != nullptr);
        if (bypass != nullptr && power != nullptr)
        {
            bypass->clicked();
            expect ("BYPASS click -> bypassed", value (p, id::bypass) >= 0.5f);
            power->clicked();
            expect ("power click -> active", value (p, id::bypass) < 0.5f);
        }

        // steppers: the buttons step, the double-click on the readout resets; the
        // wheel steps OCTAVE / TRANSPOSE but never MIDI CH
        for (const auto& [label, paramId, delta, times, wheel] :
             { std::tuple<const char*, const char*, int, int, bool> { "OCTAVE", id::octave, +1, 3, true },
               { "TRANSPOSE", id::transpose, -1, 3, true }, { "MIDI CH", id::channel, +1, 1, false } })
        {
            auto* st = find<jamrack::Stepper> (editor, label);
            auto* btn = find<juce::TextButton> (editor, juce::String (label) + (delta > 0 ? " up" : " down"));
            expect (juce::String (label) + " stepper found", st != nullptr && btn != nullptr);
            if (st == nullptr || btn == nullptr)
                continue;
            expect (juce::String (label) + " tooltip over its buttons",
                    st->getTooltip().isNotEmpty() && btn->getTooltip() == st->getTooltip());
            auto* rp = p.apvts.getParameter (paramId);
            const int start = juce::roundToInt (rp->convertFrom0to1 (rp->getDefaultValue()));
            for (int i = 0; i < times; ++i)
                btn->onClick();
            // OCTAVE stops at +2: the stepper clamps to the parameter's range
            const int want = juce::jlimit (juce::roundToInt (rp->getNormalisableRange().start),
                                           juce::roundToInt (rp->getNormalisableRange().end), start + delta * times);
            expect (juce::String (label) + " buttons -> parameter", juce::roundToInt (value (p, paramId)) == want);
            st->mouseDoubleClick (mouseAt (*st, { (float) st->getWidth() * 0.5f, 12.0f }, 2));
            expect (juce::String (label) + " double-click = default", juce::roundToInt (value (p, paramId)) == start);
            st->mouseWheelMove (mouseAt (*st, { (float) st->getWidth() * 0.5f, 12.0f }), notch());
            expect (juce::String (label) + (wheel ? " a wheel notch steps it" : " a wheel notch changes nothing"),
                    juce::roundToInt (value (p, paramId)) == (wheel ? start + 1 : start));
            setParam (p, paramId, (float) start);
        }
    }

    /** The text hosts show for the parameters, and that each one reads back to its value. */
    void checkHostText (MidPluckProcessor& p)
    {
        auto* gain = p.apvts.getParameter (id::gain);
        const auto text = [] (juce::RangedAudioParameter* rp, float plain) { return rp->getText (rp->convertTo0to1 (plain), 64); };
        expect ("GAIN 1 reads '+0.0 dB' in the host (got '" + text (gain, 1.0f) + "')", text (gain, 1.0f) == "+0.0 dB");
        expect ("GAIN 2 reads '+6.0 dB' (got '" + text (gain, 2.0f) + "')", text (gain, 2.0f) == "+6.0 dB");
        expect ("GAIN 0.1 reads '-20.0 dB' (got '" + text (gain, 0.1f) + "')", text (gain, 0.1f) == "-20.0 dB");
        expect ("SENS 0.5 reads '50'", text (p.apvts.getParameter (id::sens), 0.5f) == "50");
        expect ("GAIN text entry '+6' = 2.00", std::abs (gain->convertFrom0to1 (gain->getValueForText ("+6")) - 2.0f) < 1e-4);
        expect ("GAIN text entry '2x' = 2.00", std::abs (gain->convertFrom0to1 (gain->getValueForText ("2x")) - 2.0f) < 1e-4);

        int bad = 0;
        for (int k = 10; k <= 1000; ++k)
        {
            const float v = (float) k / 100.0f;
            const float back = gain->convertFrom0to1 (gain->getValueForText (text (gain, v)));
            if (std::abs (back - v) > 1e-4f)
            {
                if (bad++ < 3)
                    std::cout << "  GAIN " << v << " -> '" << text (gain, v) << "' -> " << back << "\n";
            }
        }
        for (const char* pid : { id::sens, id::decay, id::dyn })
            for (int k = 0; k <= 100; ++k)
            {
                auto* rp = p.apvts.getParameter (pid);
                const float v = (float) k / 100.0f;
                if (std::abs (rp->convertFrom0to1 (rp->getValueForText (text (rp, v))) - v) > 1e-4f)
                    ++bad;
            }
        expect ("every GAIN / SENS / DECAY / DYN text reads back to its value (" + juce::String (bad) + " misses)", bad == 0);
    }

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

    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    const juce::String polyBeta (juce::CharPointer_UTF8 ("POLY \xce\xb2"));

    const State states[] = {
        { "mono-idle", [] (MidPluckProcessor&) {}, nullptr, "Listening", Hue::none, false },
        // a pitch heard, no note held (under the attack threshold): amber note, NOTE OUT dark
        { "mono-heard", [] (MidPluckProcessor& p) { monoPlaying (p, false); }, nullptr, "Listening", Hue::amber, false },
        { "mono-playing",
          [] (MidPluckProcessor& p)
          {
              monoPlaying (p);
              p.meters.notesSent.store (122, std::memory_order_relaxed);
          },
          // the 123rd note leaves while the window is open
          [] (MidPluckProcessor& p) { p.meters.notesSent.store (123, std::memory_order_relaxed); },
          "Listening", Hue::teal, true },
        { "poly-3-voices", [] (MidPluckProcessor& p) { poly (p, true, 3, 0.22f, 2.6666667f, false, -18.0f); }, nullptr,
          polyBeta + dot + "plucked notes only", Hue::teal, true },
        { "poly-loading", [] (MidPluckProcessor& p) { poly (p, false, 0, 0.0f, 2.6666667f, false, -200.0f); }, nullptr,
          polyBeta + dot + juce::String (juce::CharPointer_UTF8 ("loading\xe2\x80\xa6")), Hue::none, false },
        { "poly-eco", [] (MidPluckProcessor& p) { poly (p, true, 4, 1.31f, 1.3333333f, true, -9.0f); }, nullptr,
          polyBeta + dot + "ECO: lighter analysis", Hue::teal, true },
        { "bypassed", [] (MidPluckProcessor& p)
          {
              monoPlaying (p);
              p.meters.notesHeld.store (0, std::memory_order_relaxed);    // the bypass flush released it
              setParam (p, id::bypass, 1.0f);
              p.meters.bypassed.store (true, std::memory_order_relaxed);
          }, nullptr, "BYPASSED" + dot + "audio passes, no MIDI", Hue::none, false },
        { "mono-warning", [] (MidPluckProcessor& p) { monoPlaying (p); },
          // NaN samples arrive while the window is open: a 3 s warning on the LCD
          [] (MidPluckProcessor& p) { p.meters.badSamples.store (2, std::memory_order_relaxed); },
          "Input glitch: bad samples silenced", Hue::teal, true },
        { "mono-settings", [] (MidPluckProcessor& p)
          {
              // every control away from its default: value formatting, stepper signs, and
              // the note sent spelled out (A3 played, +1 octave -3 semitones = F#4 = MIDI 66)
              monoPlaying (p);
              p.meters.midiF.store (57.46f, std::memory_order_relaxed);   // A3 +46 ct (out of tune: amber needle)
              p.meters.noteOut.store (66, std::memory_order_relaxed);
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
              p.meters.badSamples.store (17, std::memory_order_relaxed);   // before the window opened: tooltip only
          }, nullptr,
          juce::String (juce::CharPointer_UTF8 ("A3 \xe2\x86\x92 F#4")) + dot + "MIDI 66", Hue::teal, true },
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
        if (state.afterOpen != nullptr)
        {
            state.afterOpen (processor);
            editor->refresh (true);
        }

        // what the state shows
        auto* lcd = find<jamrack::Lcd> (*editor, {});
        const auto shown = lcd != nullptr ? lcd->getText() : juce::String();
        expect (juce::String (state.name) + ": LCD '" + shown + "', expected '" + state.lcd + "'", shown == state.lcd);
        auto* activity = findActivity (*editor);
        expect (juce::String (state.name) + ": NOTE OUT found", activity != nullptr);

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
            if (scale == 1.0f)
            {
                // the big figure (display at x 28, y 84; its note area 12..116 x 2..56)
                const auto hue = hueOf (image, { 40, 86, 104, 54 });
                expect (juce::String (state.name) + ": big figure " + hueName (hue) + ", expected " + hueName (state.bigFigure),
                        hue == state.bigFigure);
                // NOTE OUT LED (MIDI OUT column, second row): a lit LED is amber-bright
                if (activity != nullptr)
                {
                    const auto inEditor = editor->getLocalArea (activity->getParentComponent(), activity->getBoundsInParent());
                    const auto ledHue = hueOf (image, inEditor.withWidth (26));
                    expect (juce::String (state.name) + ": NOTE OUT " + (ledHue == Hue::amber ? "lit" : "dark") + ", expected "
                                + (state.noteOut ? "lit" : "dark"),
                            (ledHue == Hue::amber) == state.noteOut);
                }
            }
        }
    }

    // a processor the host has not prepared yet (meters at their initial values): the
    // window must open filled ("Listening"), not with an empty LCD
    {
        MidPluckProcessor fresh;
        HostedEditor hosted (fresh);
        if (hosted.get() == nullptr)
            return 1;
        const auto image = hosted.get()->createComponentSnapshot (hosted.get()->getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile ("fresh-open@1.00x.png");
        const bool ok = savePng (image, file);
        failures += ok ? 0 : 1;
        auto* lcd = find<jamrack::Lcd> (*hosted.get(), {});
        const auto text = lcd != nullptr ? lcd->getText() : juce::String();
        std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << "  LCD '" << text << "'" << std::endl;
        expect ("a fresh processor opens with LCD 'Listening' (got '" + text + "')", text == "Listening");
    }

    // every control drives its parameter and follows it
    {
        defaults (processor);
        processor.editorWidth = 0;
        HostedEditor hosted (processor);
        if (hosted.get() == nullptr)
            return 1;
        checkControls (processor, *hosted.get());
    }

    checkHostText (processor);

    // the window resized by the user: exactly 4:1, limits 100 %..200 %
    {
        defaults (processor);
        processor.editorWidth = 0;
        monoPlaying (processor);
        auto hosted = std::make_unique<HostedEditor> (processor);
        auto* editor = hosted->get();
        if (editor == nullptr)
            return 1;
        expect ("opens at 960 x 240", editor->getWidth() == 960 && editor->getHeight() == 240);
        auto* c = editor->getConstrainer();
        const auto drag = [&] (int w, int h)
        {
            auto bounds = juce::Rectangle<int> (w, h);
            c->checkBounds (bounds, editor->getBounds(), juce::Rectangle<int> (0, 0, 10000, 10000), false, false, true, true);
            return bounds;
        };
        for (const auto& [w, h] : { std::pair<int, int> { 1001, 250 }, { 1003, 251 }, { 1290, 300 }, { 1500, 377 } })
        {
            const auto b = drag (w, h);
            expect ("a drag to " + juce::String (w) + " x " + juce::String (h) + " gives " + juce::String (b.getWidth()) + " x "
                        + juce::String (b.getHeight()) + " (exactly 4:1)",
                    b.getWidth() == 4 * b.getHeight());
        }
        for (const auto& [name, factor] : { std::pair<const char*, float> { "resized-min", 0.5f }, { "resized-max", 3.0f } })
        {
            // ask for more than the limits: the constrainer clamps and keeps the ratio
            const auto bounds = drag (juce::roundToInt (MidPluckEditor::baseWidth * factor),
                                      juce::roundToInt (MidPluckEditor::baseHeight * factor));
            editor->setSize (bounds.getWidth(), bounds.getHeight());
            editor->refresh (true);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = outDir.getChildFile (juce::String (name) + ".png");
            const bool ok = savePng (image, file);
            failures += ok ? 0 : 1;
            std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << "  " << image.getWidth() << " x "
                      << image.getHeight() << "\n";
        }
        expect ("largest size 1920 x 480", editor->getWidth() == 1920 && editor->getHeight() == 480);

        // a host that imposes a size off 4:1 (1001 x 250): the face is centred on a whole
        // pixel, so it draws exactly the pixels of 1000 x 250, shifted (no resampling blur)
        {
            editor->setSize (1000, 250);
            const auto exact = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            editor->setSize (1001, 250);
            const auto off = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            int bestDiff = std::numeric_limits<int>::max(), bestDx = 0;
            for (int dx = 0; dx <= 1; ++dx)
            {
                int diff = 0;
                for (int y = 0; y < 250; ++y)
                    for (int x = 0; x < 1000 - 40; ++x)   // the grip corner aside
                    {
                        const auto a = exact.getPixelAt (x, y), b = off.getPixelAt (x + dx, y);
                        if (std::abs (a.getRed() - b.getRed()) > 2 || std::abs (a.getGreen() - b.getGreen()) > 2
                            || std::abs (a.getBlue() - b.getBlue()) > 2)
                            ++diff;
                    }
                if (diff < bestDiff) { bestDiff = diff; bestDx = dx; }
            }
            std::cout << "1001 x 250 against 1000 x 250: " << bestDiff << " pixels differ (shift " << bestDx << ")\n";
            expect ("a host size off 4:1 draws the 4:1 face on whole pixels (" + juce::String (bestDiff) + " pixels differ)",
                    bestDiff == 0);
            editor->setSize (1920, 480);
        }
        // reopened: the window comes back at the size it was left at
        editor = nullptr;
        hosted = nullptr;
        HostedEditor again (processor);
        if (again.editor == nullptr)
            return 1;
        std::cout << "reopened at " << again.editor->getWidth() << " x " << again.editor->getHeight() << "\n";
        expect ("reopens at 1920 x 480", again.editor->getWidth() == 1920 && again.editor->getHeight() == 480);
    }

    std::cout << "status lines:\n";
    {
        defaults (processor);
        auto& m = processor.meters;
        std::cout << "  mono idle     : " << MidPluckEditor::statusText (m, false, false, 0) << "\n";
        monoPlaying (processor);
        std::cout << "  mono playing  : " << MidPluckEditor::statusText (m, false, false, 0) << "\n";
        m.noteOut.store (66, std::memory_order_relaxed);
        std::cout << "  mono shifted  : " << MidPluckEditor::statusText (m, false, false, 9) << "\n";
        std::cout << "  bypassed      : " << MidPluckEditor::statusText (m, false, true, 0) << "\n";
        poly (processor, false, 0, 0.0f, 2.67f, false, -200.0f);
        std::cout << "  poly loading  : " << MidPluckEditor::statusText (m, true, false, 0) << "\n";
        poly (processor, true, 3, 0.22f, 2.67f, false, -18.0f);
        std::cout << "  poly          : " << MidPluckEditor::statusText (m, true, false, 0) << "\n";
        poly (processor, true, 3, 0.22f, 2.67f, true, -18.0f);
        std::cout << "  poly eco      : " << MidPluckEditor::statusText (m, true, false, 0) << "\n";
    }

    std::cout << "checks: " << checks << ", failed: " << failed << "\n";
    return failures == 0 && failed == 0 ? 0 : 1;
}
