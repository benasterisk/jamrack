#include "PluginEditor.h"

#include "ui/EngineDisplay.h"
#include "ui/Widgets.h"

#include <cmath>

using namespace jamrack;
namespace id = midpluck::params::id;

namespace
{
    // ---- layout of the module, logical pixels (baseWidth x baseHeight). One vertical
    // grid: header row centred on y = 29 (the top screws), section titles, then the
    // content band (the engine display) with the controls centred in it, then the
    // footer on y = 211 (the bottom screws).
    constexpr int margin = 28;                  // side gutter: the screws live in it
    constexpr int headerY = 14, headerH = 30;
    constexpr int separatorY = 56;
    constexpr int titleY = 63, titleH = 14;
    constexpr int contentY = 84, contentH = 112;
    constexpr int knobW = 66;
    constexpr int knobY = contentY + (contentH - (knobW + 28)) / 2;     // Knob::heightForWidth
    constexpr int row1Y = knobY + 2;            // stacked steppers: first row of buttons
    constexpr int row2Y = knobY + knobW + 13 - 27;   // second row: its label shares the knobs' label line
    constexpr int stepperH = 41;                // 26 px buttons + 1 + 14 px label
    constexpr int footerY = 211;                // centre of the build label and of the bottom screws

    struct Span { int x, w; };
    constexpr Span engineSpan { margin, 252 };
    constexpr Span inputSpan  { 302, knobW * 2 + 6 };
    constexpr Span notesSpan  { 458, 360 };
    constexpr Span midiSpan   { 836, 96 };
    constexpr int bendX = notesSpan.x + 2 * knobW + 6 + 14;     // BEND and BEND RANGE column
    constexpr int shiftX = bendX + 100 + 12;                    // OCTAVE and TRANSPOSE column

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

    /** A round LED + caption: lit while notes are held in the host, flashing at each new one. */
    class ActivityLed final : public juce::Component,
                              public juce::SettableTooltipClient
    {
    public:
        ActivityLed() { font = fonts->get (jamrack::Face::labelMedium, 12.0f, 0.14f); }

        void setLit (bool on)
        {
            if (on != lit) { lit = on; repaint(); }
        }
        bool isLit() const noexcept { return lit; }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            // the LED sits 16 px in and the component is 30 px high: its halo (3.4 x the
            // radius) fades out inside the bounds instead of being cut square
            drawLed (g, { r.getX() + 16.0f, r.getCentreY() }, 4.5f, colours::amber, lit);
            g.setFont (font);
            g.setColour (colours::dim);
            g.drawText ("NOTE OUT", r.withTrimmedLeft (28.0f), juce::Justification::centredLeft, false);
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
          mode (param (p, id::mode), { "MONO", "POLY" }, Segmented::Look::switches),
          gain (param (p, id::gain), "GAIN", gainText),
          sens (param (p, id::sens), "SENS", percentText),
          decay (param (p, id::decay), "DECAY", percentText),
          dyn (param (p, id::dyn), "DYN", percentText),
          bendAttachment (param (p, id::bend), bend),
          range (param (p, id::range), midpluck::params::rangeChoices(), Segmented::Look::lcd, "BEND RANGE"),
          octave (param (p, id::octave), "OCTAVE", true),
          transpose (param (p, id::transpose), "TRANSPOSE", true),
          channel (param (p, id::channel), "MIDI CH", false),
          resetInput ("Reset INPUT"),
          resetNotes ("Reset NOTES"),
          footer (MidPluckProcessor::buildLabel())
    {
        setOpaque (true);
        mode.setSuperscript (1, juce::String (juce::CharPointer_UTF8 ("\xce\xb2")));
        mode.setWheelEnabled (false);       // a wheel notch must never switch the engine
        channel.setWheelEnabled (false);    // nor the MIDI channel (both cut the notes held)
        bend.setWantsKeyboardFocus (false);
        bend.setTitle ("BEND");

        resetInput.onClick = [this] { gain.resetToDefault(); sens.resetToDefault(); };
        resetNotes.onClick = [this] { decay.resetToDefault(); dyn.resetToDefault(); };

        power.setTooltip ("Power: click to bypass the plugin (the notes held are released).");
        bypass.setTooltip ("BYPASS: no MIDI out, audio passes unchanged.");
        mode.setTooltip ("MONO: one note at a time, with pitch bend. POLY (beta): several notes, no bend, plucked notes only.");
        display.setTooltip ("MONO (TUNER): the note heard, amber, turning teal while its MIDI note is held; the tuning in cents; "
                            "TRK = time from the attack to the note. POLY (VOICES): notes held; CPU = cost of the analysis "
                            "against its budget (ECO lights from 80 %).");
        gain.setTooltip ("GAIN: input level before the engines (the audio out is not changed).");
        sens.setTooltip ("SENS: attack threshold, -36 to -60 dBFS (MONO only).");
        decay.setTooltip ("DECAY: the note ends 15 to 45 dB below its peak (MONO only).");
        dyn.setTooltip ("DYN: velocity dynamics, 0 = every note at full velocity (MONO only).");
        bend.setTooltip ("BEND on: continuous pitch bend; off: chromatic retriggered notes (MONO only).");
        range.setTooltip ("BEND RANGE in semitones: set the same range on the synth (MONO only).");
        octave.setTooltip ("OCTAVE shift. Double-click the number: back to 0.");
        transpose.setTooltip ("TRANSPOSE in semitones. Double-click the number: back to 0.");
        channel.setTooltip ("MIDI channel of the notes sent. Double-click the number: back to 1.");
        activity.setTooltip ("Lit while a note is held in the host (MIDI sent), flashes at each new note.");
        resetInput.setTooltip ("Reset INPUT: GAIN and SENS back to their defaults.");
        resetNotes.setTooltip ("Reset NOTES: DECAY and DYN back to their defaults.");

        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &power, &lcd, &mode, &bypass, &display, &gain, &sens, &decay, &dyn, &bend, &range,
                 &octave, &transpose, &channel, &activity, &resetInput, &resetNotes, &footer })
            addAndMakeVisible (c);
    }

    void resized() override
    {
        power.setBounds (margin, headerY, 30, headerH);
        lcd.setBounds (236, headerY, 376, headerH);
        mode.setBounds (624 - 4, headerY - 4, 176 + 8, headerH + 8);   // 4 px of room for the lit button's glow
        bypass.setBounds (812, headerY, baseWidth - margin - 812, headerH);

        display.setBounds (engineSpan.x, contentY, engineSpan.w, contentH);

        const int knobH = Knob::heightForWidth (knobW);
        gain.setBounds (inputSpan.x, knobY, knobW, knobH);
        sens.setBounds (inputSpan.x + knobW + 6, knobY, knobW, knobH);
        decay.setBounds (notesSpan.x, knobY, knobW, knobH);
        dyn.setBounds (notesSpan.x + knobW + 6, knobY, knobW, knobH);

        // the pill is centred on the first row of buttons; 5 px above and below it
        // leave the thumb's glow room to fade out
        bend.setBounds (bendX, row1Y + 13 - 18, 100, 36);
        range.setBounds (bendX, row2Y, 100, stepperH);
        octave.setBounds (shiftX, row1Y, Stepper::preferredWidth, stepperH);
        transpose.setBounds (shiftX, row2Y, Stepper::preferredWidth, stepperH);

        channel.setBounds (midiSpan.x, row1Y, midiSpan.w, stepperH);
        activity.setBounds (midiSpan.x - 2, row2Y + 13 - 15, midiSpan.w + 2, 30);

        // section resets: right after their titles, the pair centred over the section
        for (auto [button, span, title] : { std::tuple<ResetButton*, Span, const char*> { &resetInput, inputSpan, "INPUT" },
                                            { &resetNotes, notesSpan, "NOTES" } })
        {
            const float tw = titleWidth (title);
            const float x0 = (float) span.x + ((float) span.w - (tw + resetGap + resetW)) * 0.5f;
            button->setBounds (juce::roundToInt (x0 + tw + resetGap), titleY + titleH / 2 - 10, resetW, 20);
        }

        footer.setBounds (margin, footerY - 8, baseWidth - 2 * margin, 16);
        cache = {};
    }

    void paint (juce::Graphics& g) override
    {
        const float ps = physicalScale (g);
        if (cache.isNull() || ps != cacheScale)
            renderFace (ps);
        g.drawImageTransformed (cache, juce::AffineTransform::scale (1.0f / cacheScale));

        // what changes with the mode: the display's title and the MONO ONLY marks
        drawTitle (g, poly ? "VOICES" : "TUNER", engineSpan);
        if (poly)
        {
            drawMonoOnly (g, (float) sens.getX(), (float) sens.getRight());
            drawMonoOnly (g, (float) notesSpan.x, (float) (bendX + 100));
        }
    }

    /** MONO-only controls are dimmed in POLY (the engine ignores them), like the web
     *  card, and marked MONO ONLY; they stay usable so that every parameter remains
     *  controllable. The display's title follows the mode. */
    void setPolyLook (bool isPoly)
    {
        poly = isPoly;
        for (juce::Component* c : std::initializer_list<juce::Component*> { &sens, &decay, &dyn, &bend, &range })
            c->setAlpha (poly ? 0.45f : 1.0f);
        repaint();
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
    ResetButton resetInput, resetNotes;
    Footer footer;

private:
    static constexpr int resetW = 22;
    static constexpr float resetGap = 1.0f;

    static juce::RangedAudioParameter& param (MidPluckProcessor& p, const char* paramId)
    {
        auto* rp = p.apvts.getParameter (paramId);
        jassert (rp != nullptr);
        return *rp;
    }

    juce::Font titleFont() const { return fonts->get (jamrack::Face::labelSemi, 12.0f, 0.2f); }
    float titleWidth (const char* title) const { return juce::GlyphArrangement::getStringWidth (titleFont(), title); }

    /** A section title (.mod-sec-title): letter-spaced caps, dim, centred over its span. */
    void drawTitle (juce::Graphics& g, const char* title, Span span) const
    {
        g.setFont (titleFont());
        g.setColour (colours::dim);
        g.drawText (title, juce::Rectangle<float> ((float) span.x, (float) titleY, (float) span.w, (float) titleH),
                    juce::Justification::centred, false);
    }

    /** A title followed by its reset button, the pair centred over the span. */
    void drawTitleWithReset (juce::Graphics& g, const char* title, Span span) const
    {
        const float tw = titleWidth (title);
        const float x0 = (float) span.x + ((float) span.w - (tw + resetGap + (float) resetW)) * 0.5f;
        g.setFont (titleFont());
        g.setColour (colours::dim);
        g.drawText (title, juce::Rectangle<float> (x0, (float) titleY, tw + 4.0f, (float) titleH),
                    juce::Justification::centredLeft, false);
    }

    /** Silkscreen under a group of MONO-only controls (POLY): a thin amber-dim line
     *  interrupted by "MONO ONLY". */
    void drawMonoOnly (juce::Graphics& g, float x0, float x1) const
    {
        const auto font = fonts->get (jamrack::Face::labelSemi, 10.0f, 0.18f);
        const juce::String text ("MONO ONLY");
        const float tw = juce::GlyphArrangement::getStringWidth (font, text);
        const float y = (float) (contentY + contentH) - 4.0f;
        const float cx = (x0 + x1) * 0.5f;
        const auto colour = colours::amberDim.brighter (0.35f);
        g.setColour (colour.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (x0 + 4.0f, y, cx - tw * 0.5f - 6.0f - (x0 + 4.0f), 1.0f));
        g.fillRect (juce::Rectangle<float> (cx + tw * 0.5f + 6.0f, y, x1 - 4.0f - (cx + tw * 0.5f + 6.0f), 1.0f));
        g.setFont (font);
        g.setColour (colour);
        g.drawText (text, juce::Rectangle<float> (cx - tw * 0.5f - 2.0f, y - 7.0f, tw + 4.0f, 14.0f),
                    juce::Justification::centred, false);
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

        // rack screws, two per side (a 2U rack ear): the top pair on the header's
        // centre line, the bottom pair on the footer's; slots turned differently
        const float sx[2] = { 13.0f, (float) baseWidth - 13.0f };
        const float sy[2] = { (float) (headerY + headerH / 2), (float) footerY };
        const float turn[4] = { 0.62f, -0.35f, 0.21f, 0.95f };
        for (int i = 0; i < 4; ++i)
            drawScrew (g, { sx[i % 2], sy[i / 2] }, 6.0f, turn[i]);

        // header rule (.mod-body border-top) with its lit lower edge
        g.setColour (lineSoft);
        g.fillRect (juce::Rectangle<float> ((float) margin, (float) separatorY, (float) (baseWidth - 2 * margin), 1.0f));
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.fillRect (juce::Rectangle<float> ((float) margin, (float) separatorY + 1.0f, (float) (baseWidth - 2 * margin), 1.0f));

        // section titles (.mod-sec-title; the engine's follows the mode, drawn live) and
        // the rules between sections, which stop with the engine display: one bottom line
        drawTitleWithReset (g, "INPUT", inputSpan);
        drawTitleWithReset (g, "NOTES", notesSpan);
        drawTitle (g, "MIDI OUT", midiSpan);
        for (const int x : { (engineSpan.x + engineSpan.w + inputSpan.x) / 2, (inputSpan.x + inputSpan.w + notesSpan.x) / 2,
                             (notesSpan.x + notesSpan.w + midiSpan.x) / 2 })
        {
            g.setColour (lineSoft);
            g.fillRect (juce::Rectangle<float> ((float) x, (float) titleY, 1.0f, (float) (contentY + contentH - titleY)));
        }

        // brand: the JAMRACK wordmark's style (.brand-name: Unbounded 800, .02em, soft
        // amber glow), MID in cream and PLUCK in amber; the subtitle under it
        const auto brandFont = fonts->get (jamrack::Face::brandHeavy, 20.0f, 0.02f);
        const auto brandArea = juce::Rectangle<float> (70.0f, 9.0f, 160.0f, 24.0f);
        const float midW = juce::GlyphArrangement::getStringWidth (brandFont, "MID");
        GlowText glowA, glowB;
        glowA.draw (g, "MID", brandFont, brandArea, juce::Justification::centredLeft, txt, amber.withAlpha (0.15f), 12.0f);
        glowB.draw (g, "PLUCK", brandFont, brandArea.withTrimmedLeft (midW), juce::Justification::centredLeft,
                    amber, amber.withAlpha (0.3f), 12.0f);

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
    bool poly = false;
};

//==============================================================================
void MidPluckEditor::ExactRatio::checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                                              const juce::Rectangle<int>& limits, bool top, bool left, bool bottom, bool right)
{
    ComponentBoundsConstrainer::checkBounds (bounds, previous, limits, top, left, bottom, right);
    const int h = juce::jlimit (getMinimumHeight(), getMaximumHeight(), bounds.getHeight());
    const int w = h * (baseWidth / baseHeight);
    if (left) bounds.setLeft (bounds.getRight() - w);
    else      bounds.setWidth (w);
    if (top)  bounds.setTop (bounds.getBottom() - h);
    else      bounds.setHeight (h);
}

//==============================================================================
MidPluckEditor::MidPluckEditor (MidPluckProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      modeValue (*p.apvts.getRawParameterValue (id::mode)),
      bypassValue (*p.apvts.getRawParameterValue (id::bypass)),
      octaveValue (*p.apvts.getRawParameterValue (id::octave)),
      transposeValue (*p.apvts.getRawParameterValue (id::transpose))
{
    // read first: the first resize below already stores the width in resized()
    const int remembered = p.editorWidth;
    setLookAndFeel (&lnf);
    setOpaque (true);
    face = std::make_unique<Module> (p);
    addAndMakeVisible (*face);

    // 100 % to 200 %: below 100 % the 12 px labels fall under 9 px and stop being legible
    constrainer.setSizeLimits (baseWidth, baseHeight, baseWidth * 2, baseHeight * 2);
    constrainer.setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setConstrainer (&constrainer);
    setResizable (true, false);
    grip = std::make_unique<juce::ResizableCornerComponent> (this, &constrainer);
    grip->setTitle ("Resize");
    addAndMakeVisible (*grip);
    const int h = juce::jlimit (baseHeight, baseHeight * 2,
                                juce::roundToInt ((double) (remembered > 0 ? remembered : baseWidth) * baseHeight / baseWidth));
    setSize (h * (baseWidth / baseHeight), h);

    // warnings already counted before the window opened stay in the LCD's tooltip only
    lastBad = p.meters.badSamples.load (std::memory_order_relaxed);
    lastDropped = p.meters.droppedEvents.load (std::memory_order_relaxed);

    refresh (true);
    startTimerHz (30);
}

MidPluckEditor::~MidPluckEditor()
{
    stopTimer();
    grip = nullptr;
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
    // A host may still impose a size off the 4:1 ratio: centre the face, on whole
    // device pixels so that its cached image is not resampled.
    const float ds = juce::jmax (0.1f, juce::Component::getApproximateScaleFactorForComponent (this));
    const auto whole = [ds] (float v) { return std::round (v * ds) / ds; };
    const float tx = whole (((float) getWidth() - (float) baseWidth * s) * 0.5f);
    const float ty = whole (((float) getHeight() - (float) baseHeight * s) * 0.5f);
    face->setBounds (0, 0, baseWidth, baseHeight);
    face->setTransform (juce::AffineTransform::scale (s).translated (tx, ty));
    // the grip in the face's bottom-right corner, at the face's scale
    if (grip != nullptr)
    {
        const int size = juce::roundToInt (18.0f * s);
        grip->setBounds (juce::roundToInt (tx + (float) baseWidth * s) - size, juce::roundToInt (ty + (float) baseHeight * s) - size,
                         size, size);
    }
    processor.editorWidth = getWidth();
}

juce::String MidPluckEditor::statusText (const MidPluckProcessor::Meters& m, bool poly, bool bypassed, int shift)
{
    const auto relaxed = std::memory_order_relaxed;
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    if (bypassed)
        return "BYPASSED" + dot + "audio passes, no MIDI";
    if (poly)
    {
        const juce::String polyBeta (juce::CharPointer_UTF8 ("POLY \xce\xb2"));
        if (! m.polyReady.load (relaxed))
            return polyBeta + dot + juce::String (juce::CharPointer_UTF8 ("loading\xe2\x80\xa6"));
        if (m.eco.load (relaxed))
            return polyBeta + dot + "ECO: lighter analysis";
        return polyBeta + dot + "plucked notes only";
    }
    // MONO: status, as on the web card; the note sent is spelled out only when OCTAVE
    // or TRANSPOSE make it differ from the note played (the MIDI number settles the
    // naming: Live calls MIDI 60 "C3", this display and the web card "C4")
    const int out = m.noteOut.load (relaxed);
    if (shift != 0 && m.notesHeld.load (relaxed) > 0 && out >= 0)
        return EngineDisplay::noteName (out - shift) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "))
             + EngineDisplay::noteName (out) + dot + "MIDI " + juce::String (out);
    return "Listening";
}

void MidPluckEditor::refresh (bool instant)
{
    const auto relaxed = std::memory_order_relaxed;
    const auto& m = processor.meters;
    const bool poly = modeValue.load (relaxed) >= 0.5f;
    const bool bypassed = bypassValue.load (relaxed) >= 0.5f;
    const int held = m.notesHeld.load (relaxed);
    const int noteOut = m.noteOut.load (relaxed);

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
    if (! poly && ! bypassed && held > 0 && noteOut >= 0)
        v.heldNote = noteOut;
    v.litSegments = EngineDisplay::segmentsForDb (shownDb);
    v.levelDb = juce::roundToInt (shownDb);
    v.notes = m.notesSent.load (relaxed);
    if (const float lat = m.latMs.load (relaxed); lat > 0.0f && std::isfinite (lat))
        v.trkMs = juce::roundToInt (lat);
    v.voices = m.voices.load (relaxed);
    if (const float budget = m.budgetMs.load (relaxed), hop = m.hopMs.load (relaxed);
        budget > 0.0f && std::isfinite (budget) && std::isfinite (hop))
        v.cpuPct = juce::jlimit (0, 999, juce::roundToInt (100.0f * hop / budget));
    v.eco = m.eco.load (relaxed);
    // (the first time unconditionally: a processor never prepared still holds the
    // meters' initial values, equal to a default EngineView)
    if (! viewShown || v != face->display.getView())
        face->display.setView (v);

    // warnings: shown on the LCD for 3 s after their counter moved, in plain words;
    // the running totals live in the LCD's tooltip
    const int bad = m.badSamples.load (relaxed), dropped = m.droppedEvents.load (relaxed);
    if (bad != lastBad || dropped != lastDropped || ! viewShown)
    {
        if (bad > lastBad)
            warning = "Input glitch: bad samples silenced";
        if (dropped > lastDropped)
            warning = "MIDI overflow: events dropped";
        if (bad > lastBad || dropped > lastDropped)
            warningLeft = warningFrames;
        lastBad = bad;
        lastDropped = dropped;
        juce::String tip ("Status of the engine.");
        if (bad > 0)
            tip << " Since the plugin was loaded: " << bad << " bad input samples (NaN or infinite, from a device "
                << "before MidPluck) replaced by silence.";
        if (dropped > 0)
            tip << " Engine events dropped: " << dropped << " (should stay 0).";
        face->lcd.setTooltip (tip);
    }

    // status line: rebuilt only when what it depends on changed
    const int shift = juce::roundToInt (octaveValue.load (relaxed)) * 12 + juce::roundToInt (transposeValue.load (relaxed));
    const bool shiftedNote = ! poly && shift != 0 && held > 0 && noteOut >= 0;
    const juce::int64 key = (juce::int64) bypassed | ((juce::int64) poly << 1) | ((juce::int64) v.polyReady << 2)
                          | ((juce::int64) (poly && v.eco) << 3)
                          | (shiftedNote ? (((juce::int64) (noteOut + 1) << 4) | ((juce::int64) (shift + 64) << 12)) : 0);
    if (key != lastStatusKey)
    {
        lastStatusKey = key;
        status = statusText (m, poly, bypassed, shift);
    }
    if (warningLeft > 0)
    {
        --warningLeft;
        face->lcd.setText (warning, true);
    }
    else
    {
        face->lcd.setText (status, false);
    }
    viewShown = true;

    if ((int) poly != lastPoly)
    {
        lastPoly = (int) poly;
        face->setPolyLook (poly);
    }

    // NOTE OUT: lit while notes are held, plus about 150 ms of flash after each new note
    if (lastNotes >= 0 && v.notes != lastNotes && ! bypassed)
        activityFrames = 5;
    else if (activityFrames > 0)
        --activityFrames;
    lastNotes = v.notes;
    face->activity.setLit (! bypassed && (held > 0 || activityFrames > 0));
}
