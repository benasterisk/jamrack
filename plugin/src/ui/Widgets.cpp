#include "Widgets.h"

namespace jamrack
{
    using namespace colours;

    //==============================================================================
    Knob::Knob (juce::RangedAudioParameter& p, const juce::String& l, Formatter f)
        : slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox),
          attachment (p, slider),
          label (l),
          format (std::move (f))
    {
        // 270-degree sweep from bottom-left, like the web knob (conic from 225deg)
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (220);
        slider.setScrollWheelEnabled (true);
        slider.setPopupDisplayEnabled (false, false, nullptr);
        slider.setTitle (label);
        slider.setWantsKeyboardFocus (false);
        slider.onValueChange = [this] { repaint (valueArea.getSmallestIntegerContainer()); };
        addAndMakeVisible (slider);

        valueFont = fonts->get (Face::mono, 12.5f);
        labelFont = fonts->get (Face::labelMedium, 12.0f, 0.12f);
    }

    void Knob::resized()
    {
        const int w = getWidth();
        slider.setBounds (0, 0, w, w);
        valueArea = { 0.0f, (float) w - 2.0f, (float) w, 15.0f };
        labelArea = { 0.0f, (float) w + 13.0f, (float) w, 15.0f };
    }

    void Knob::paint (juce::Graphics& g)
    {
        g.setFont (valueFont);
        g.setColour (amberHot.withAlpha (0.9f));
        g.drawText (format != nullptr ? format (slider.getValue()) : juce::String (slider.getValue(), 2),
                    valueArea, juce::Justification::centred, false);
        g.setFont (labelFont);
        g.setColour (dim);
        g.drawText (label, labelArea, juce::Justification::centred, false);
    }

    //==============================================================================
    Stepper::Stepper (juce::RangedAudioParameter& p, const juce::String& l, bool sign)
        : param (p),
          attachment (p, [this] (float v) { value = juce::roundToInt (v); repaint (readout.getSmallestIntegerContainer()); }, nullptr),
          label (l),
          signedDisplay (sign)
    {
        const auto range = p.getNormalisableRange();
        minValue = juce::roundToInt (range.start);
        maxValue = juce::roundToInt (range.end);

        for (auto* b : { &minus, &plus })
        {
            b->setRepeatSpeed (420, 90);
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
        minus.setTitle (label + " down");
        plus.setTitle (label + " up");
        minus.onClick = [this] { step (-1); };
        plus.onClick = [this] { step (+1); };
        setTitle (label);

        valueFont = fonts->get (Face::monoMedium, 14.5f);
        labelFont = fonts->get (Face::labelMedium, 12.0f, 0.14f);
        attachment.sendInitialUpdate();
    }

    void Stepper::step (int delta)
    {
        const int next = juce::jlimit (minValue, maxValue, value + delta);
        if (next != value)
            attachment.setValueAsCompleteGesture ((float) next);
    }

    void Stepper::resized()
    {
        // [ - ] [ 0 ] [ + ] over the label (.stepper-ctl: 24 px buttons, 34 px readout)
        const float h = 26.0f, b = 26.0f, gap = 4.0f;
        const float readW = (float) getWidth() - 2.0f * (b + gap);
        minus.setBounds (0, 0, (int) b, (int) h);
        plus.setBounds (getWidth() - (int) b, 0, (int) b, (int) h);
        readout = { b + gap, 0.0f, readW, h };
        labelArea = { 0.0f, h + 3.0f, (float) getWidth(), 15.0f };
    }

    void Stepper::paint (juce::Graphics& g)
    {
        drawRecessed (g, readout, 4.0f, lcdBg);
        const auto text = (signedDisplay && value > 0) ? "+" + juce::String (value) : juce::String (value);
        g.setFont (valueFont);
        g.setColour (lcdTxt);
        g.drawText (text, readout.translated (0.0f, 0.5f), juce::Justification::centred, false);
        g.setFont (labelFont);
        g.setColour (dim);
        g.drawText (label, labelArea, juce::Justification::centred, false);
    }

    void Stepper::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        wheelAccum += wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
        if (std::abs (wheelAccum) >= 0.12f)
        {
            step (wheelAccum > 0.0f ? 1 : -1);
            wheelAccum = 0.0f;
        }
    }

    void Stepper::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (readout.contains (e.position))
            attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    }

    //==============================================================================
    Segmented::Segmented (juce::RangedAudioParameter& p, const juce::StringArray& segs, bool leds, const juce::String& cap)
        : param (p),
          segments (segs),
          withLeds (leds),
          caption (cap),
          attachment (p, [this] (float v)
                      {
                          selected = juce::jlimit (0, juce::jmax (0, segments.size() - 1), juce::roundToInt (v));
                          repaint();
                      }, nullptr)
    {
        textFont = withLeds ? fonts->get (Face::labelSemi, 14.0f, 0.12f) : fonts->get (Face::monoMedium, 12.5f);
        supFont = fonts->get (Face::brand, 8.5f);
        captionFont = fonts->get (Face::labelMedium, 12.0f, 0.14f);
        setTitle (caption.isNotEmpty() ? caption : param.getName (32));
        attachment.sendInitialUpdate();
    }

    void Segmented::setSuperscript (int segment, const juce::String& text)
    {
        supSegment = segment;
        supText = text;
        repaint();
    }

    void Segmented::resized()
    {
        auto r = getLocalBounds().toFloat();
        if (caption.isNotEmpty())
        {
            strip = r.removeFromTop (26.0f);
            captionArea = juce::Rectangle<float> (r.getX(), strip.getBottom() + 3.0f, r.getWidth(), 15.0f);
        }
        else
        {
            strip = r;
        }
    }

    int Segmented::segmentAt (juce::Point<float> p) const
    {
        if (! strip.contains (p) || segments.isEmpty())
            return -1;
        const float w = strip.getWidth() / (float) segments.size();
        return juce::jlimit (0, segments.size() - 1, (int) ((p.x - strip.getX()) / w));
    }

    void Segmented::choose (int index)
    {
        if (index >= 0 && index < segments.size() && index != selected)
            attachment.setValueAsCompleteGesture ((float) index);
    }

    void Segmented::mouseDown (const juce::MouseEvent& e) { choose (segmentAt (e.position)); }

    void Segmented::mouseMove (const juce::MouseEvent& e)
    {
        const int h = segmentAt (e.position);
        if (h != hovered)
        {
            hovered = h;
            repaint();
        }
    }

    void Segmented::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }

    void Segmented::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        wheelAccum += wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
        if (std::abs (wheelAccum) >= 0.12f)
        {
            choose (juce::jlimit (0, segments.size() - 1, selected + (wheelAccum > 0.0f ? 1 : -1)));
            wheelAccum = 0.0f;
        }
    }

    void Segmented::paint (juce::Graphics& g)
    {
        const int n = segments.size();
        if (n == 0)
            return;
        drawButtonFace (g, strip, 6.0f, false, false, false);
        const float w = strip.getWidth() / (float) n;

        for (int i = 0; i < n; ++i)
        {
            const auto seg = juce::Rectangle<float> (strip.getX() + w * (float) i, strip.getY(), w, strip.getHeight());
            const bool on = i == selected;
            const bool hover = i == hovered && ! on;

            if (on)
                drawButtonFace (g, seg.reduced (2.0f), 4.5f, false, false, true);
            else if (i > 0 && i - 1 != selected)
            {
                g.setColour (lineSoft);
                g.drawVerticalLine ((int) std::round (seg.getX()), seg.getY() + 6.0f, seg.getBottom() - 6.0f);
            }

            auto textArea = seg;
            if (withLeds)
            {
                const float ledR = 3.6f;
                drawLed (g, { seg.getX() + 13.0f, seg.getCentreY() }, ledR, amber, on, 0.8f);
                textArea = seg.withTrimmedLeft (22.0f).withTrimmedRight (4.0f);
            }

            const auto colour = on ? (withLeds ? txt : lcdTxt) : (hover ? txt.withAlpha (0.8f) : dim);
            g.setFont (textFont);
            g.setColour (colour);
            if (i == supSegment && supText.isNotEmpty())
            {
                // text + superscript, centred together
                const float tw = juce::GlyphArrangement::getStringWidth (textFont, segments[i]);
                const float sw = juce::GlyphArrangement::getStringWidth (supFont, supText);
                const float x0 = textArea.getCentreX() - (tw + 2.0f + sw) * 0.5f;
                g.drawText (segments[i], juce::Rectangle<float> (x0, textArea.getY(), tw + 4.0f, textArea.getHeight()),
                            juce::Justification::centredLeft, false);
                g.setFont (supFont);
                g.setColour (colour.withMultipliedAlpha (0.85f));
                g.drawText (supText, juce::Rectangle<float> (x0 + tw + 2.0f, textArea.getY() - 5.0f, sw + 4.0f, textArea.getHeight()),
                            juce::Justification::centredLeft, false);
            }
            else
            {
                g.drawText (segments[i], textArea, juce::Justification::centred, false);
            }
        }

        if (caption.isNotEmpty())
        {
            g.setFont (captionFont);
            g.setColour (dim);
            g.drawText (caption, captionArea, juce::Justification::centred, false);
        }
    }

    //==============================================================================
    LedButton::LedButton (juce::RangedAudioParameter& p, Style s)
        : juce::Button (s == Style::power ? "Power" : "BYPASS"),
          style (s),
          attachment (p, [this] (float v) { bypassed = v >= 0.5f; repaint(); }, nullptr)
    {
        setClickingTogglesState (false);
        setWantsKeyboardFocus (false);
        textFont = fonts->get (Face::labelSemi, 14.0f, 0.14f);
        attachment.sendInitialUpdate();
    }

    void LedButton::clicked()
    {
        attachment.setValueAsCompleteGesture (bypassed ? 0.0f : 1.0f);
    }

    void LedButton::paintButton (juce::Graphics& g, bool hover, bool down)
    {
        const auto r = getLocalBounds().toFloat();
        if (style == Style::power)
        {
            drawButtonFace (g, r, 6.0f, hover, down, false);
            drawLed (g, r.getCentre().translated (0.0f, down ? 0.6f : 0.0f), 5.0f, teal, ! bypassed);
            return;
        }
        // BYPASS: the REC look of the web topbar when latched (red border, red LED)
        drawButtonFace (g, r, 6.0f, hover, down, bypassed, juce::Colour (0xff7a2c26));
        const float dy = down ? 0.6f : 0.0f;
        drawLed (g, { r.getX() + 15.0f, r.getCentreY() + dy }, 4.2f, red, bypassed);
        g.setFont (textFont);
        g.setColour (bypassed ? txt : (hover ? txt.withAlpha (0.85f) : dim.withAlpha (1.0f)));
        g.drawText (getButtonText(), r.withTrimmedLeft (24.0f).translated (0.0f, dy), juce::Justification::centred, false);
    }

    //==============================================================================
    Lcd::Lcd()
    {
        font = fonts->get (Face::mono, 15.0f);
    }

    void Lcd::setText (const juce::String& t, bool warn)
    {
        if (t == text && warn == warning)
            return;
        text = t;
        warning = warn;
        repaint();
    }

    void Lcd::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        drawRecessed (g, r, 5.0f, lcdBg);
        const auto colour = warning ? juce::Colour (0xffff8a7a) : lcdTxt;
        glow.draw (g, text, font, r.reduced (12.0f, 0.0f).translated (0.0f, 0.5f), juce::Justification::centredLeft,
                   colour, (warning ? red : lcdTxt).withAlpha (0.45f), 6.0f);
    }

    //==============================================================================
    Footer::Footer (const juce::String& b) : build (b)
    {
        buildFont = fonts->get (Face::mono, 11.0f);
        warnFont = fonts->get (Face::monoMedium, 11.0f);
        setInterceptsMouseClicks (false, false);
    }

    void Footer::setWarning (const juce::String& w)
    {
        if (w == warning)
            return;
        warning = w;
        repaint();
    }

    void Footer::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        const float bw = juce::GlyphArrangement::getStringWidth (buildFont, build);
        g.setFont (buildFont);
        g.setColour (dim);
        g.drawText (build, r, juce::Justification::centredRight, false);
        if (warning.isNotEmpty())
        {
            g.setFont (warnFont);
            g.setColour (juce::Colour (0xffff8a7a));
            g.drawText (warning, r.withTrimmedRight (bw + 18.0f), juce::Justification::centredRight, false);
        }
    }
}
