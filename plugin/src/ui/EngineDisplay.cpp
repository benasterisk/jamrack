#include "EngineDisplay.h"

#include <cmath>

namespace jamrack
{
    using namespace colours;

    namespace
    {
        constexpr float pad = 12.0f;

        juce::Colour segmentColour (int i) noexcept
        {
            // web meter: green, then amber from about -24 dB, red from about -10 dB
            return i >= 13 ? red : (i >= 9 ? amber : green);
        }

        /** "E3", "C#4": scientific names (C4 = MIDI 60), as the web card (js/i18n noteName). */
        juce::String noteName (int midi)
        {
            static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const int m = juce::jlimit (0, 127, midi);
            return juce::String (names[m % 12]) + juce::String (m / 12 - 1);
        }

        juce::String usToMs (int us)
        {
            return juce::String ((double) us / 1000.0, 2);
        }
    }

    EngineDisplay::EngineDisplay()
    {
        noteFont = fonts->get (Face::brandHeavy, 34.0f);
        monoSmall = fonts->get (Face::mono, 11.0f);
        centsFont = fonts->get (Face::monoMedium, 11.5f);
        labelFont = fonts->get (Face::labelSemi, 11.5f, 0.16f);
        tagFont = fonts->get (Face::labelSemi, 11.0f, 0.12f);
        setInterceptsMouseClicks (true, false);   // tooltip only
    }

    int EngineDisplay::segmentsForDb (float db) noexcept
    {
        if (! std::isfinite (db))
            return 0;
        return juce::jlimit (0, segments, (int) std::floor ((db + 60.0f) / 60.0f * (float) segments + 0.5f));
    }

    void EngineDisplay::setView (const EngineView& v)
    {
        if (v == view)
            return;
        const bool modeChanged = v.poly != view.poly;
        view = v;
        if (modeChanged)
            background = {};
        repaint();
    }

    void EngineDisplay::resized()
    {
        const auto r = getLocalBounds().toFloat();
        const float inner = r.getWidth() - 2.0f * pad;
        noteArea = { pad, pad - 2.0f, 104.0f, 48.0f };
        sideArea = { pad + 112.0f, pad, inner - 112.0f, 46.0f };
        tunerBar = { sideArea.getX(), sideArea.getY() + 12.0f, sideArea.getWidth(), 10.0f };
        tunerText = { sideArea.getX(), tunerBar.getBottom() + 5.0f, sideArea.getWidth(), 15.0f };
        voicesLabel = { sideArea.getX(), sideArea.getY() + 1.0f, sideArea.getWidth(), 15.0f };
        ecoTag = { sideArea.getRight() - 38.0f, sideArea.getY(), 38.0f, 17.0f };
        for (int i = 0; i < maxVoices; ++i)
        {
            const float step = sideArea.getWidth() / (float) maxVoices;
            voiceLeds[i] = { sideArea.getX() + step * ((float) i + 0.5f), sideArea.getY() + 33.0f };
        }

        meterBox = { pad, pad + 58.0f, inner - 52.0f, 18.0f };
        dbArea = { meterBox.getRight() + 4.0f, meterBox.getY(), r.getWidth() - pad - meterBox.getRight() - 4.0f, meterBox.getHeight() };
        const float gap = 2.0f;
        const auto inside = meterBox.reduced (4.0f, 4.0f);
        const float sw = (inside.getWidth() - gap * (float) (segments - 1)) / (float) segments;
        for (int i = 0; i < segments; ++i)
            segmentRects[i] = { inside.getX() + (sw + gap) * (float) i, inside.getY(), sw, inside.getHeight() };

        readoutArea = { pad, meterBox.getBottom() + 9.0f, inner, 16.0f };
        monoAdvance = juce::GlyphArrangement::getStringWidth (monoSmall, "0000000000") / 10.0f;
        background = {};
    }

    void EngineDisplay::renderBackground (float scale)
    {
        const int w = juce::jmax (1, (int) std::ceil ((float) getWidth() * scale));
        const int h = juce::jmax (1, (int) std::ceil ((float) getHeight() * scale));
        background = juce::Image (juce::Image::ARGB, w, h, true);
        backgroundScale = scale;
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (scale));

        // .gtr-tuner: lcd-bg box, black border, inner shadow
        drawRecessed (g, getLocalBounds().toFloat().reduced (0.5f, 0.5f).withTrimmedBottom (1.0f), 6.0f, lcdBg);

        // the level meter housing (#15100a) with its unlit segments (#2a241d)
        g.setColour (juce::Colour (0xff15100a));
        g.fillRoundedRectangle (meterBox, 4.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (meterBox.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) std::round (meterBox.getBottom() + 0.5f), meterBox.getX() + 4.0f, meterBox.getRight() - 4.0f);
        for (const auto& s : segmentRects)
        {
            g.setColour (lineSoft);
            g.fillRoundedRectangle (s, 1.2f);
        }

        if (! view.poly)
        {
            // .gtr-cents: red - amber - green - amber - red scale, centre tick
            juce::ColourGradient scaleFill (juce::Colour (0xff5a2a22), tunerBar.getX(), 0.0f,
                                            juce::Colour (0xff5a2a22), tunerBar.getRight(), 0.0f, false);
            scaleFill.addColour (0.35, juce::Colour (0xff4a3b16));
            scaleFill.addColour (0.47, juce::Colour (0xff1f3d2a));
            scaleFill.addColour (0.53, juce::Colour (0xff1f3d2a));
            scaleFill.addColour (0.65, juce::Colour (0xff4a3b16));
            g.setGradientFill (scaleFill);
            g.fillRoundedRectangle (tunerBar, 5.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xcc000000), 0.0f, tunerBar.getY(),
                                                     juce::Colours::transparentBlack, 0.0f, tunerBar.getY() + 4.0f, false));
            g.fillRoundedRectangle (tunerBar, 5.0f);
            g.setColour (juce::Colours::white.withAlpha (0.33f));
            g.fillRect (juce::Rectangle<float> (tunerBar.getCentreX() - 0.5f, tunerBar.getY() - 3.0f, 1.0f, tunerBar.getHeight() + 6.0f));
        }
    }

    void EngineDisplay::drawReadout (juce::Graphics& g, juce::Rectangle<float> area,
                                     std::initializer_list<std::pair<juce::String, bool>> runs) const
    {
        g.setFont (monoSmall);
        float x = area.getX();
        for (const auto& run : runs)
        {
            const float w = monoAdvance * (float) run.first.length();
            g.setColour (run.second && ! view.bypassed ? lcdTxt : dim);
            g.drawText (run.first, juce::Rectangle<float> (x, area.getY(), w + 4.0f, area.getHeight()),
                        juce::Justification::centredLeft, false);
            x += w;
        }
    }

    void EngineDisplay::paint (juce::Graphics& g)
    {
        const float ps = physicalScale (g);
        if (background.isNull() || ps != backgroundScale)
            renderBackground (ps);
        g.drawImageTransformed (background, juce::AffineTransform::scale (1.0f / backgroundScale));

        const bool live = ! view.bypassed;
        const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
        const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

        // ---- big figure: the note (MONO) or the voice count (POLY)
        juce::String big = dash;
        bool lit = false;
        if (live && ! view.poly && view.note >= 0)
        {
            big = noteName (view.note);
            lit = true;
        }
        else if (live && view.poly && view.polyReady && view.voices > 0)
        {
            big = juce::String (view.voices);
            lit = true;
        }
        if (lit)
            noteGlow.draw (g, big, noteFont, noteArea, juce::Justification::centred, teal, teal.withAlpha (0.6f), 10.0f);
        else
        {
            g.setFont (noteFont);
            g.setColour (noteIdle);
            g.drawText (big, noteArea, juce::Justification::centred, false);
        }

        // ---- side: tuner (MONO) or voices (POLY)
        if (! view.poly)
        {
            const bool heard = live && view.note >= 0;
            const int c = juce::jlimit (-50, 50, view.cents);
            const float x = tunerBar.getCentreX() + (heard ? (float) c / 50.0f * (tunerBar.getWidth() * 0.5f - 2.0f) : 0.0f);
            const auto needle = juce::Rectangle<float> (4.0f, 16.0f).withCentre ({ x, tunerBar.getCentreY() });
            const auto colour = ! heard ? noteIdle : (std::abs (view.cents) < 5 ? teal : amber);
            if (heard)
            {
                g.setColour (colour.withAlpha (0.22f));
                g.fillRoundedRectangle (needle.expanded (4.0f, 3.0f), 5.0f);
                g.setColour (colour.withAlpha (0.35f));
                g.fillRoundedRectangle (needle.expanded (2.0f, 1.5f), 3.5f);
            }
            g.setColour (colour);
            g.fillRoundedRectangle (needle, 2.0f);

            g.setFont (centsFont);
            if (heard)
            {
                g.setColour (lcdTxt);
                g.drawText ((view.cents > 0 ? "+" : "") + juce::String (view.cents) + " ct", tunerText,
                            juce::Justification::centred, false);
            }
            else
            {
                g.setFont (labelFont);
                g.setColour (dim);
                g.drawText ("CENTS", tunerText, juce::Justification::centred, false);
            }
        }
        else
        {
            const bool loading = ! view.polyReady;
            g.setFont (labelFont);
            g.setColour (live && loading ? amber : dim);
            g.drawText (live && loading ? juce::String (juce::CharPointer_UTF8 ("LOADING\xe2\x80\xa6")) : juce::String ("VOICES"),
                        voicesLabel, juce::Justification::centredLeft, false);
            for (int i = 0; i < maxVoices; ++i)
                drawLed (g, voiceLeds[i], 5.0f, teal, live && view.polyReady && i < view.voices);

            // ECO tag: unlit outline, amber when the engine sheds work (rule 3.3-6)
            const bool ecoOn = live && view.polyReady && view.eco;
            if (ecoOn)
            {
                g.setColour (amber.withAlpha (0.16f));
                g.fillRoundedRectangle (ecoTag.expanded (2.0f), 5.0f);
            }
            g.setColour (ecoOn ? amber : ledOff);
            g.drawRoundedRectangle (ecoTag.reduced (0.5f), 3.5f, 1.0f);
            g.setFont (tagFont);
            g.drawText ("ECO", ecoTag.translated (0.5f, 0.0f), juce::Justification::centred, false);
        }

        // ---- level meter
        const int litCount = live ? view.litSegments : 0;
        for (int i = 0; i < litCount; ++i)
        {
            const auto& s = segmentRects[i];
            const auto colour = segmentColour (i);
            g.setColour (colour.withAlpha (0.18f));
            g.fillRoundedRectangle (s.expanded (1.5f, 1.5f), 2.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (s, 1.2f);
        }
        g.setFont (monoSmall);
        g.setColour (dim);
        g.drawText (live && view.levelDb > -60 ? juce::String (view.levelDb) + " dB" : dash + " dB", dbArea,
                    juce::Justification::centredRight, false);

        // ---- readouts
        const auto notes = juce::String (view.notes);
        if (! view.poly)
        {
            drawReadout (g, readoutArea, { { "LAT ", false },
                                           { view.latMs >= 0 ? juce::String (view.latMs) : juce::String ("--"), true },
                                           { " ms" + dot + "NOTES ", false },
                                           { notes, true } });
        }
        else
        {
            const bool ready = view.polyReady;
            drawReadout (g, readoutArea, { { "HOP ", false },
                                           { ready ? usToMs (view.hopUs) : juce::String ("--"), true },
                                           { " / ", false },
                                           { usToMs (view.budgetUs), true },
                                           { " ms" + dot + "NOTES ", false },
                                           { notes, true } });
        }
    }
}
