#include "PluginEditor.h"

#include "ui/EngineDisplay.h"
#include "ui/Widgets.h"

#include <cmath>

using namespace jamrack;
namespace id = midpluck::params::id;

namespace
{
    // ---- layout of the module, logical pixels (baseWidth x baseHeight)
    constexpr int margin = 28;                  // side gutter: the screws live in it
    constexpr int headerY = 14, headerH = 30;
    constexpr int separatorY = 56;
    constexpr int titleY = 66, titleH = 14;
    constexpr int controlsY = 88;
    constexpr int knobW = 64;
    constexpr int rowGap = 6;                   // between the two rows of a stacked column
    constexpr int stepperH = 43;
    constexpr int displayW = 252, displayH = 118;

    struct Span { int x, w; };
    constexpr Span engineSpan { margin, displayW };
    constexpr Span inputSpan  { 302, knobW * 2 + 6 };
    constexpr Span notesSpan  { 458, 356 };
    constexpr Span midiSpan   { 836, 96 };

    /** GAIN as the web card shows it: dB, rounded, "+" from unity up ("+3dB", "-6dB"). */
    juce::String gainText (double v)
    {
        if (v <= 0.0)
            return "-inf";
        const double db = 20.0 * std::log10 (v);
        const int r = (int) std::floor (db + 0.5);
        return (v >= 1.0 ? "+" : "") + juce::String (r == 0 && v < 1.0 ? 0 : r) + "dB";
    }

    /** SENS, DECAY, DYN: 0..100 like the web card (fmtPct). */
    juce::String percentText (double v) { return juce::String ((int) std::floor (v * 100.0 + 0.5)); }

    /** A round LED + caption that flashes while notes leave the plugin. */
    class ActivityLed final : public juce::Component,
                              public juce::SettableTooltipClient
    {
    public:
        ActivityLed() { font = fonts->get (jamrack::Face::labelMedium, 12.0f, 0.14f); }

        void setLit (bool on)
        {
            if (on != lit) { lit = on; repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            drawLed (g, { r.getX() + 8.0f, r.getCentreY() }, 4.5f, colours::amber, lit);
            g.setFont (font);
            g.setColour (colours::dim);
            g.drawText ("NOTE OUT", r.withTrimmedLeft (20.0f), juce::Justification::centredLeft, false);
        }

    private:
        juce::SharedResourcePointer<Fonts> fonts;
        juce::Font font { juce::FontOptions {} };
        bool lit = false;
    };
}

//==============================================================================
/** The rack module: every control, laid out at the base size; its static face
 *  (metal, screws, rules, titles, brand) is drawn into an image per scale. */
class MidPluckEditor::Module final : public juce::Component
{
public:
    explicit Module (MidPluckProcessor& p)
        : power (param (p, id::bypass), LedButton::Style::power),
          bypass (param (p, id::bypass), LedButton::Style::bypass),
          mode (param (p, id::mode), { "MONO", "POLY" }, true),
          gain (param (p, id::gain), "GAIN", gainText),
          sens (param (p, id::sens), "SENS", percentText),
          decay (param (p, id::decay), "DECAY", percentText),
          dyn (param (p, id::dyn), "DYN", percentText),
          bendAttachment (param (p, id::bend), bend),
          range (param (p, id::range), midpluck::params::rangeChoices(), false, "BEND RANGE"),
          octave (param (p, id::octave), "OCTAVE", true),
          transpose (param (p, id::transpose), "TRANSPOSE", true),
          channel (param (p, id::channel), "MIDI CH", false),
          footer (MidPluckProcessor::buildLabel())
    {
        setOpaque (true);
        mode.setSuperscript (1, juce::String (juce::CharPointer_UTF8 ("\xce\xb2")));
        bend.setWantsKeyboardFocus (false);
        bend.setTitle ("BEND");

        power.setTooltip ("Power: click to bypass the plugin (the notes held are released).");
        bypass.setTooltip ("BYPASS: no MIDI out, audio passes unchanged.");
        lcd.setTooltip ("Status of the engine.");
        mode.setTooltip ("MONO: one note at a time, with pitch bend. POLY (beta): several notes, no bend.");
        display.setTooltip ("MONO: note heard and tuning in cents, latency attack -> note. POLY: voices, cost per analysis hop against its budget, ECO tag.");
        gain.setTooltip ("GAIN: input level before the engines (the audio out is not changed).");
        sens.setTooltip ("SENS: attack threshold, -36 to -60 dBFS (MONO only).");
        decay.setTooltip ("DECAY: the note ends 15 to 45 dB below its peak (MONO only).");
        dyn.setTooltip ("DYN: velocity dynamics, 0 = every note at full velocity (MONO only).");
        bend.setTooltip ("BEND on: continuous pitch bend; off: chromatic retriggered notes (MONO only).");
        range.setTooltip ("BEND RANGE in semitones: set the same range on the synth (MONO only).");
        octave.setTooltip ("OCTAVE shift. Double-click the number: back to 0.");
        transpose.setTooltip ("TRANSPOSE in semitones. Double-click the number: back to 0.");
        channel.setTooltip ("MIDI channel of the notes sent.");
        activity.setTooltip ("Lights while notes leave the plugin.");

        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &power, &lcd, &mode, &bypass, &display, &gain, &sens, &decay, &dyn, &bend, &range,
                 &octave, &transpose, &channel, &activity, &footer })
            addAndMakeVisible (c);
    }

    void resized() override
    {
        power.setBounds (margin, headerY, 30, headerH);
        lcd.setBounds (232, headerY, 380, headerH);
        mode.setBounds (624, headerY, 176, headerH);
        bypass.setBounds (812, headerY, baseWidth - margin - 812, headerH);

        display.setBounds (engineSpan.x, controlsY, engineSpan.w, displayH);

        const int knobH = Knob::heightForWidth (knobW);
        gain.setBounds (inputSpan.x, controlsY, knobW, knobH);
        sens.setBounds (inputSpan.x + knobW + 6, controlsY, knobW, knobH);

        const int row2 = controlsY + stepperH + rowGap;
        decay.setBounds (notesSpan.x, controlsY, knobW, knobH);
        dyn.setBounds (notesSpan.x + knobW + 6, controlsY, knobW, knobH);
        const int colA = notesSpan.x + 2 * knobW + 6 + 14;
        bend.setBounds (colA, controlsY, 100, 26);
        range.setBounds (colA, row2, 100, stepperH);
        const int colB = colA + 100 + 12;
        octave.setBounds (colB, controlsY, Stepper::preferredWidth, stepperH);
        transpose.setBounds (colB, row2, Stepper::preferredWidth, stepperH);

        channel.setBounds (midiSpan.x, controlsY, midiSpan.w, stepperH);
        activity.setBounds (midiSpan.x + 6, row2 + 4, midiSpan.w - 6, 18);

        footer.setBounds (margin, baseHeight - 30, baseWidth - 2 * margin, 18);
        cache = {};
    }

    void paint (juce::Graphics& g) override
    {
        const float ps = physicalScale (g);
        if (cache.isNull() || ps != cacheScale)
            renderFace (ps);
        g.drawImageTransformed (cache, juce::AffineTransform::scale (1.0f / cacheScale));
    }

    /** MONO-only controls are dimmed in POLY (the engine ignores them), like the web
     *  card; they stay usable so that every parameter remains controllable. */
    void setPolyLook (bool poly)
    {
        for (juce::Component* c : std::initializer_list<juce::Component*> { &sens, &decay, &dyn, &bend, &range })
            c->setAlpha (poly ? 0.35f : 1.0f);
    }

    LedButton power, bypass;
    Lcd lcd;
    Segmented mode;
    EngineDisplay display;
    Knob gain, sens, decay, dyn;
    juce::ToggleButton bend { "BEND" };
    juce::ButtonParameterAttachment bendAttachment;
    Segmented range;
    Stepper octave, transpose, channel;
    ActivityLed activity;
    Footer footer;

private:
    static juce::RangedAudioParameter& param (MidPluckProcessor& p, const char* paramId)
    {
        auto* rp = p.apvts.getParameter (paramId);
        jassert (rp != nullptr);
        return *rp;
    }

    void renderFace (float scale)
    {
        const int w = (int) std::ceil ((float) baseWidth * scale);
        const int h = (int) std::ceil ((float) baseHeight * scale);
        cache = juce::Image (juce::Image::ARGB, w, h, true);
        cacheScale = scale;
        juce::Graphics g (cache);
        g.addTransform (juce::AffineTransform::scale (scale));
        drawFace (g);
    }

    void drawFace (juce::Graphics& g)
    {
        using namespace colours;
        const auto all = juce::Rectangle<float> ((float) baseWidth, (float) baseHeight);
        g.setColour (bg);
        g.fillRect (all);

        // the module face (.module.guitar): a slightly lifted plate with a top sheen
        const auto plate = all.reduced (2.0f);
        const float cr = 10.0f;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (plate.translated (0.0f, 1.5f), cr);
        g.setGradientFill (juce::ColourGradient (faceTop, 0.0f, plate.getY(), faceBottom, 0.0f, plate.getBottom(), false));
        g.fillRoundedRectangle (plate, cr);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.04f), 0.0f, plate.getY(),
                                                 juce::Colours::transparentWhite, 0.0f, plate.getY() + plate.getHeight() * 0.3f, false));
        g.fillRoundedRectangle (plate, cr);
        // a faint vertical light falling on the left half, like the stage glow of the page
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x0affc46b), plate.getX() + plate.getWidth() * 0.3f, plate.getY(),
                                                 juce::Colours::transparentBlack, plate.getX() + plate.getWidth() * 0.3f, plate.getY() + 160.0f, true));
        g.fillRoundedRectangle (plate, cr);
        g.setColour (line);
        g.drawRoundedRectangle (plate.reduced (0.5f), cr, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawLine (plate.getX() + cr, plate.getY() + 1.5f, plate.getRight() - cr, plate.getY() + 1.5f, 1.0f);

        // rack screws, two per side, slots turned a little differently
        const float sx[2] = { 13.0f, (float) baseWidth - 13.0f };
        const float sy[2] = { 29.0f, (float) baseHeight - 29.0f };
        const float turn[4] = { 0.62f, -0.35f, 0.21f, 0.95f };
        for (int i = 0; i < 4; ++i)
            drawScrew (g, { sx[i % 2], sy[i / 2] }, 6.0f, turn[i]);

        // header rule (.mod-body border-top) with its lit lower edge
        g.setColour (lineSoft);
        g.fillRect (juce::Rectangle<float> ((float) margin, (float) separatorY, (float) (baseWidth - 2 * margin), 1.0f));
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.fillRect (juce::Rectangle<float> ((float) margin, (float) separatorY + 1.0f, (float) (baseWidth - 2 * margin), 1.0f));

        // section titles (.mod-sec-title) and the rules between sections
        const auto titleFont = fonts->get (jamrack::Face::labelSemi, 12.0f, 0.2f);
        g.setFont (titleFont);
        g.setColour (dim);
        for (const auto& [span, title] : { std::pair<Span, const char*> { engineSpan, "ENGINE" },
                                           { inputSpan, "INPUT" }, { notesSpan, "NOTES" }, { midiSpan, "MIDI OUT" } })
            g.drawText (title, juce::Rectangle<float> ((float) span.x, (float) titleY, (float) span.w, (float) titleH),
                        juce::Justification::centred, false);
        for (const int x : { (engineSpan.x + engineSpan.w + inputSpan.x) / 2, (inputSpan.x + inputSpan.w + notesSpan.x) / 2,
                             (notesSpan.x + notesSpan.w + midiSpan.x) / 2 })
        {
            g.setColour (lineSoft);
            g.fillRect (juce::Rectangle<float> ((float) x, (float) titleY, 1.0f, (float) (controlsY + displayH - titleY)));
        }

        // brand: MID + PLUCK (amber), wide-spaced Unbounded caps, and the subtitle
        const auto brandFont = fonts->get (jamrack::Face::brand, 17.0f, 0.26f);
        const auto brandArea = juce::Rectangle<float> (70.0f, 11.0f, 160.0f, 22.0f);
        const float midW = juce::GlyphArrangement::getStringWidth (brandFont, "MID");
        GlowText glowA, glowB;
        glowA.draw (g, "MID", brandFont, brandArea, juce::Justification::centredLeft, txt, amber.withAlpha (0.18f), 12.0f);
        glowB.draw (g, "PLUCK", brandFont, brandArea.withTrimmedLeft (midW), juce::Justification::centredLeft,
                    amber, amber.withAlpha (0.35f), 12.0f);

        const auto subFont = fonts->get (jamrack::Face::labelSemi, 11.0f, 0.24f);
        const auto arrowFont = fonts->get (jamrack::Face::brand, 9.0f);
        const float y = 33.0f, hh = 12.0f;
        float x = 71.0f;
        g.setColour (dim);
        g.setFont (subFont);
        g.drawText ("GUITAR", juce::Rectangle<float> (x, y, 60.0f, hh), juce::Justification::centredLeft, false);
        x += juce::GlyphArrangement::getStringWidth (subFont, "GUITAR") + 3.0f;
        g.setFont (arrowFont);
        const auto arrow = juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92"));
        g.drawText (arrow, juce::Rectangle<float> (x, y, 20.0f, hh), juce::Justification::centredLeft, false);
        x += juce::GlyphArrangement::getStringWidth (arrowFont, arrow) + 6.0f;
        g.setFont (subFont);
        g.drawText ("MIDI", juce::Rectangle<float> (x, y, 60.0f, hh), juce::Justification::centredLeft, false);
    }

    juce::SharedResourcePointer<Fonts> fonts;
    juce::Image cache;
    float cacheScale = 0.0f;
};

//==============================================================================
MidPluckEditor::MidPluckEditor (MidPluckProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    // read first: setResizeLimits() below already resizes (and resized() stores the width)
    const int remembered = p.editorWidth;
    setLookAndFeel (&lnf);
    setOpaque (true);
    face = std::make_unique<Module> (p);
    addAndMakeVisible (*face);

    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    const int w = juce::jlimit (baseWidth * 3 / 4, baseWidth * 2, remembered > 0 ? remembered : baseWidth);
    setSize (w, juce::roundToInt ((double) w * baseHeight / baseWidth));

    refresh (true);
    startTimerHz (30);
}

MidPluckEditor::~MidPluckEditor()
{
    stopTimer();
    face = nullptr;
    setLookAndFeel (nullptr);
}

void MidPluckEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
}

void MidPluckEditor::resized()
{
    if (face == nullptr)
        return;
    const float s = juce::jmin ((float) getWidth() / (float) baseWidth, (float) getHeight() / (float) baseHeight);
    face->setBounds (0, 0, baseWidth, baseHeight);
    face->setTransform (juce::AffineTransform::scale (s).translated (((float) getWidth() - (float) baseWidth * s) * 0.5f,
                                                                     ((float) getHeight() - (float) baseHeight * s) * 0.5f));
    processor.editorWidth = getWidth();
}

juce::String MidPluckEditor::statusText (const MidPluckProcessor::Meters& m, bool poly, bool bypassed)
{
    const auto relaxed = std::memory_order_relaxed;
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    if (bypassed)
        return "BYPASSED";
    if (poly)
    {
        if (! m.polyReady.load (relaxed))
            return juce::String (juce::CharPointer_UTF8 ("POLY loading\xe2\x80\xa6"));
        const int v = m.voices.load (relaxed);
        juce::String s = v > 0 ? juce::String (v) + (v == 1 ? " voice" : " voices") : juce::String ("Listening");
        if (m.eco.load (relaxed))
            s << dot << "ECO";
        return s;
    }
    const float midiF = m.midiF.load (relaxed);
    if (! std::isfinite (midiF))
        return "Listening";
    const int note = juce::jlimit (0, 127, (int) std::lround (midiF));
    juce::String s = "Note " + juce::MidiMessage::getMidiNoteName (note, true, true, 4);
    if (const float lat = m.latMs.load (relaxed); lat > 0.0f && std::isfinite (lat))
        s << dot << juce::roundToInt (lat) << " ms";
    return s;
}

void MidPluckEditor::refresh (bool instant)
{
    const auto relaxed = std::memory_order_relaxed;
    const auto& m = processor.meters;
    const bool poly = processor.apvts.getRawParameterValue (id::mode)->load (relaxed) >= 0.5f;
    const bool bypassed = processor.apvts.getRawParameterValue (id::bypass)->load (relaxed) >= 0.5f;

    // level meter: instant rise, ~36 dB/s fall at 30 Hz
    float db = m.db.load (relaxed);
    db = std::isfinite (db) ? juce::jmax (db, -60.0f) : -60.0f;
    shownDb = (instant || db >= shownDb) ? db : juce::jmax (db, shownDb - 1.2f);

    EngineView v;
    v.poly = poly;
    v.bypassed = bypassed;
    v.polyReady = m.polyReady.load (relaxed);
    if (const float midiF = m.midiF.load (relaxed); ! poly && std::isfinite (midiF))
    {
        v.note = juce::jlimit (0, 127, (int) std::lround (midiF));
        v.cents = juce::jlimit (-50, 50, (int) std::lround ((midiF - (float) v.note) * 100.0f));
    }
    v.litSegments = EngineDisplay::segmentsForDb (shownDb);
    v.levelDb = juce::roundToInt (shownDb);
    v.notes = m.notesSent.load (relaxed);
    if (const float lat = m.latMs.load (relaxed); lat > 0.0f && std::isfinite (lat))
        v.latMs = juce::roundToInt (lat);
    v.voices = m.voices.load (relaxed);
    v.hopUs = juce::roundToInt (m.hopMs.load (relaxed) * 100.0f) * 10;    // 0.01 ms steps, as shown
    v.budgetUs = juce::roundToInt (m.budgetMs.load (relaxed) * 100.0f) * 10;
    v.eco = m.eco.load (relaxed);
    if (v != face->display.getView())
    {
        face->display.setView (v);
        face->lcd.setText (statusText (m, poly, bypassed), false);   // built only when something shown changed
    }

    if (const int bad = m.badSamples.load (relaxed), dropped = m.droppedEvents.load (relaxed);
        bad != lastBad || dropped != lastDropped)
    {
        lastBad = bad;
        lastDropped = dropped;
        juce::StringArray warnings;
        if (bad > 0)
            warnings.add ("bad input samples " + juce::String (bad));
        if (dropped > 0)
            warnings.add ("dropped " + juce::String (dropped));
        face->footer.setWarning (warnings.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))));
    }

    if ((int) poly != lastPoly)
    {
        lastPoly = (int) poly;
        face->setPolyLook (poly);
    }

    // MIDI activity: about 150 ms of light after each new note
    if (lastNotes >= 0 && v.notes != lastNotes && ! bypassed)
        activityFrames = 5;
    else if (activityFrames > 0)
        --activityFrames;
    lastNotes = v.notes;
    face->activity.setLit (activityFrames > 0);
}
