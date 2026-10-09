#include "Widgets.h"

namespace jamrack
{
    using namespace colours;

    //==============================================================================
    Knob::Knob (juce::RangedAudioParameter& p, const juce::String& l, Formatter f)
        : param (p),
          slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox),
          attachment (p, slider),
          resetter (p, [] (float) {}, nullptr),
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
        slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);   // the web knob's ns-resize
        slider.onValueChange = [this] { repaint (valueArea.getSmallestIntegerContainer()); };
        addAndMakeVisible (slider);

        valueFont = fonts->get (Face::mono, 12.5f);
        labelFont = fonts->get (Face::labelMedium, 12.0f, 0.12f);
    }

    void Knob::resetToDefault()
    {
        resetter.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
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
        // [ - ] [ 0 ] [ + ] with the label right under it (.stepper-ctl: square buttons,
        // LCD readout); the label sits 1 px below so that it reads as this stepper's
        // name, not the next row's
        const float h = 26.0f, b = 26.0f, gap = 4.0f;
        const float readW = (float) getWidth() - 2.0f * (b + gap);
        minus.setBounds (0, 0, (int) b, (int) h);
        plus.setBounds (getWidth() - (int) b, 0, (int) b, (int) h);
        readout = { b + gap, 0.0f, readW, h };
        labelArea = { 0.0f, h + 1.0f, (float) getWidth(), 14.0f };
    }

    void Stepper::paint (juce::Graphics& g)
    {
        drawRecessed (g, readout, 4.0f, lcdBg);
        // U+2212 for negatives: as wide as "+", centred like it
        const auto text = value > 0 ? (signedDisplay ? "+" : "") + juce::String (value)
                                    : (value < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String (-value)
                                                 : juce::String ("0"));
        g.setFont (valueFont);
        g.setColour (lcdTxt);
        g.drawText (text, readout.translated (0.0f, 0.5f), juce::Justification::centred, false);
        g.setFont (labelFont);
        g.setColour (dim);
        g.drawText (label, labelArea, juce::Justification::centred, false);
    }

    void Stepper::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (! wheelEnabled)
        {
            Component::mouseWheelMove (e, wheel);   // passed on to the parent, never a step
            return;
        }
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
    Segmented::Segmented (juce::RangedAudioParameter& p, const juce::StringArray& segs, Look l, const juce::String& cap)
        : param (p),
          segments (segs),
          look (l),
          caption (cap),
          attachment (p, [this] (float v)
                      {
                          selected = juce::jlimit (0, juce::jmax (0, segments.size() - 1), juce::roundToInt (v));
                          repaint();
                      }, nullptr)
    {
        textFont = look == Look::switches ? fonts->get (Face::labelSemi, 14.0f, 0.12f) : fonts->get (Face::monoMedium, 12.5f);
        supFont = fonts->get (Face::brand, 10.0f);
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
            captionArea = juce::Rectangle<float> (r.getX(), strip.getBottom() + 1.0f, r.getWidth(), 14.0f);
        }
        else
        {
            // switches: the lit button's glow needs room around it (bounds 4 px larger)
            strip = look == Look::switches ? r.reduced (4.0f) : r;
        }
    }

    juce::Rectangle<float> Segmented::segmentBounds (int i) const
    {
        const int n = juce::jmax (1, segments.size());
        if (look == Look::switches)
        {
            // separate buttons, 6 px apart (.layout-switch gap)
            const float gap = 6.0f;
            const float w = (strip.getWidth() - gap * (float) (n - 1)) / (float) n;
            return { strip.getX() + (w + gap) * (float) i, strip.getY(), w, strip.getHeight() };
        }
        const float w = strip.getWidth() / (float) n;
        return { strip.getX() + w * (float) i, strip.getY(), w, strip.getHeight() };
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

    void Segmented::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (! wheelEnabled)
        {
            Component::mouseWheelMove (e, wheel);   // passed on to the parent, never a switch
            return;
        }
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
        if (look == Look::lcd)
            drawButtonFace (g, strip, 6.0f, false, false, false);

        for (int i = 0; i < n; ++i)
        {
            const auto seg = segmentBounds (i);
            const bool on = i == selected;
            const bool hover = i == hovered && ! on;

            juce::Colour colour;
            if (look == Look::switches)
            {
                if (on)
                {
                    // lit like the web's .layout-switch button.on: amber face, dark text
                    g.setColour (amber.withAlpha (0.12f));
                    g.fillRoundedRectangle (seg.expanded (3.0f), 8.5f);
                    g.setColour (amber.withAlpha (0.10f));
                    g.fillRoundedRectangle (seg.expanded (1.5f), 7.0f);
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc46b), 0.0f, seg.getY(),
                                                             juce::Colour (0xfff0a040), 0.0f, seg.getBottom(), false));
                    g.fillRoundedRectangle (seg, 6.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.35f));
                    g.drawLine (seg.getX() + 6.0f, seg.getY() + 1.0f, seg.getRight() - 6.0f, seg.getY() + 1.0f, 1.0f);
                    g.setColour (amber);
                    g.drawRoundedRectangle (seg.reduced (0.5f), 6.0f, 1.0f);
                    colour = juce::Colour (0xff14100a);
                }
                else
                {
                    drawButtonFace (g, seg, 6.0f, hover, false, false);
                    colour = hover ? txt.withAlpha (0.85f) : dim;
                }
            }
            else
            {
                if (on)
                    drawButtonFace (g, seg.reduced (2.0f), 4.5f, false, false, true);
                else if (i > 0 && i - 1 != selected)
                {
                    g.setColour (lineSoft);
                    g.drawVerticalLine ((int) std::round (seg.getX()), seg.getY() + 6.0f, seg.getBottom() - 6.0f);
                }
                colour = on ? lcdTxt : (hover ? txt.withAlpha (0.8f) : dim);
            }

            g.setFont (textFont);
            g.setColour (colour);
            if (i == supSegment && supText.isNotEmpty())
            {
                // text + superscript, centred together
                const float tw = juce::GlyphArrangement::getStringWidth (textFont, segments[i]);
                const float sw = juce::GlyphArrangement::getStringWidth (supFont, supText);
                const float x0 = seg.getCentreX() - (tw + 2.0f + sw) * 0.5f;
                g.drawText (segments[i], juce::Rectangle<float> (x0, seg.getY(), tw + 4.0f, seg.getHeight()),
                            juce::Justification::centredLeft, false);
                g.setFont (supFont);
                g.drawText (supText, juce::Rectangle<float> (x0 + tw + 2.0f, seg.getY() - 5.0f, sw + 4.0f, seg.getHeight()),
                            juce::Justification::centredLeft, false);
            }
            else
            {
                g.drawText (segments[i], seg, juce::Justification::centred, false);
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
        altFont = fonts->get (Face::brand, 12.0f);   // the arrow and beta the mono subset lacks
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
        const auto colour = warning ? warnText : lcdTxt;
        glow.draw (g, text, font, r.reduced (12.0f, 0.0f).translated (0.0f, 0.5f), juce::Justification::centredLeft,
                   colour, (warning ? red : lcdTxt).withAlpha (0.45f), 6.0f, &altFont);
    }

    //==============================================================================
    Footer::Footer (const juce::String& b) : build (b)
    {
        buildFont = fonts->get (Face::mono, 11.0f);
        setInterceptsMouseClicks (false, false);
    }

    void Footer::paint (juce::Graphics& g)
    {
        g.setFont (buildFont);
        g.setColour (dim);
        g.drawText (build, getLocalBounds().toFloat(), juce::Justification::centredRight, false);
    }

    //==============================================================================
    ResetButton::ResetButton (const juce::String& name) : juce::Button (name)
    {
        setWantsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void ResetButton::paintButton (juce::Graphics& g, bool hover, bool down)
    {
        const auto r = getLocalBounds().toFloat();
        if (hover || down)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r, 5.0f);
        }
        // an open circle, gap at the top, arrowhead at its upper right end turning
        // anticlockwise (the web's U+21BA, drawn: the embedded faces lack it)
        const auto c = r.getCentre().translated (0.0f, down ? 0.6f : 0.0f);
        const float rad = 4.6f;
        const float a0 = juce::MathConstants<float>::pi * 0.18f, a1 = juce::MathConstants<float>::pi * 1.80f;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
        const auto colour = hover || down ? amber : dim.withMultipliedAlpha (0.7f);
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const juce::Point<float> tip (c.x + rad * std::sin (a0), c.y - rad * std::cos (a0));
        const juce::Point<float> dir (-std::cos (a0), -std::sin (a0));      // anticlockwise tangent
        const juce::Point<float> side (-dir.y, dir.x);
        juce::Path head;
        head.addTriangle (tip + dir * 2.6f, tip - dir * 1.4f + side * 2.4f, tip - dir * 1.4f - side * 2.4f);
        g.fillPath (head);
    }
}
