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

        const juce::String& dash()
        {
            static const juce::String d (juce::CharPointer_UTF8 ("\xe2\x80\x94"));   // the one placeholder: an em dash
            return d;
        }
    }

    EngineDisplay::EngineDisplay()
    {
        noteFont = fonts->get (Face::brandHeavy, 34.0f);
        idleFont = fonts->get (Face::brand, 22.0f);       // the idle dash: thin, as the web's 22 px one
        monoSmall = fonts->get (Face::mono, 11.0f);
        monoStrong = fonts->get (Face::monoMedium, 11.0f);
        centsFont = fonts->get (Face::monoMedium, 11.5f);
        labelFont = fonts->get (Face::labelSemi, 11.5f, 0.16f);
        tagFont = fonts->get (Face::labelSemi, 11.0f, 0.12f);
        setInterceptsMouseClicks (true, false);   // tooltip only
    }

    juce::String EngineDisplay::noteName (int midi)
    {
        static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int m = juce::jlimit (0, 127, midi);
        return juce::String (names[m % 12]) + juce::String (m / 12 - 1);
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
        view = v;
        repaint();
    }

    void EngineDisplay::resized()
    {
        const auto r = getLocalBounds().toFloat();
        const float inner = r.getWidth() - 2.0f * pad;
        const float h = r.getHeight();

        // three bands: big figure + side (tuner or voices), level meter, readouts
        const float readoutH = 16.0f, meterH = 16.0f;
        readoutArea = { pad, h - 10.0f - readoutH, inner, readoutH };
        meterBox = { pad, readoutArea.getY() - 6.0f - meterH, inner - 52.0f, meterH };
        dbArea = { meterBox.getRight() + 4.0f, meterBox.getY(), r.getWidth() - pad - meterBox.getRight() - 4.0f, meterH };

        const float topH = meterBox.getY() - 6.0f;     // the band above the meter
        noteArea = { pad, 2.0f, 104.0f, topH - 2.0f };
        const float mid = noteArea.getCentreY();
        sideArea = { pad + 112.0f, mid - 20.0f, inner - 112.0f, 40.0f };

        // MONO: the cents scale on the note's centre line, its reading under it
        tunerBar = { sideArea.getX(), mid - 9.0f, sideArea.getWidth(), 10.0f };
        tunerText = { sideArea.getX(), tunerBar.getBottom() + 4.0f, sideArea.getWidth(), 14.0f };

        // POLY: six voice LEDs on the note's centre line; the ECO tag at the end of the
        // readout line, next to the CPU figure it is about
        ledRow = { sideArea.getX(), mid - 8.0f, sideArea.getWidth(), 16.0f };
        const float step = juce::jmin (17.0f, ledRow.getWidth() / (float) maxVoices);
        const float x0 = ledRow.getCentreX() - step * (float) maxVoices * 0.5f;
        for (int i = 0; i < maxVoices; ++i)
            voiceLeds[i] = { x0 + step * ((float) i + 0.5f), mid };
        ecoTag = { readoutArea.getRight() - 34.0f, readoutArea.getCentreY() - 7.5f, 34.0f, 15.0f };

        const float gap = 2.0f;
        const auto inside = meterBox.reduced (4.0f, 4.0f);
        const float sw = (inside.getWidth() - gap * (float) (segments - 1)) / (float) segments;
        for (int i = 0; i < segments; ++i)
            segmentRects[i] = { inside.getX() + (sw + gap) * (float) i, inside.getY(), sw, inside.getHeight() };
    }

    void EngineDisplay::paintBackground (juce::Graphics& g) const
    {
        // .gtr-tuner: lcd-bg box, black border, inner shadow
        drawRecessed (g, getLocalBounds().toFloat().reduced (0.5f, 0.5f).withTrimmedBottom (1.0f), 6.0f, lcdBg);

        // the level meter housing (#15100a) with its unlit segments (#2a241d)
        g.setColour (juce::Colour (0xff15100a));
        g.fillRoundedRectangle (meterBox, 4.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (meterBox.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) std::round (meterBox.getBottom() + 0.5f), meterBox.getX() + 4.0f, meterBox.getRight() - 4.0f);
        g.setColour (lineSoft);
        for (const auto& s : segmentRects)
            g.fillRoundedRectangle (s, 1.2f);

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
                                     std::initializer_list<std::pair<juce::String, juce::Colour>> runs) const
    {
        float x = area.getX();
        for (const auto& [text, colour] : runs)
        {
            // dim captions in the regular face, values in their colour (medium face when not lcd-txt: alerts)
            const bool caption = colour == dim;
            const auto& f = (caption || colour == lcdTxt) ? monoSmall : monoStrong;
            const float w = juce::GlyphArrangement::getStringWidth (f, text);
            g.setFont (f);
            g.setColour (view.bypassed ? dim : colour);
            g.drawText (text, juce::Rectangle<float> (x, area.getY(), w + 4.0f, area.getHeight()),
                        juce::Justification::centredLeft, false);
            x += w;
        }
    }

    void EngineDisplay::paint (juce::Graphics& g)
    {
        paintBackground (g);

        const bool live = ! view.bypassed;
        const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

        // ---- big figure: the note (MONO) or the voice count (POLY). As on the web
        // card: amber while a pitch is only heard, teal while a note is held.
        juce::String big;
        juce::Colour colour = noteIdle;
        if (live && ! view.poly)
        {
            if (view.note >= 0)
            {
                big = noteName (view.note);
                colour = view.heldNote >= 0 ? teal : lcdTxt;
            }
            else if (view.heldNote >= 0)
            {
                big = noteName (view.heldNote);    // held while the pitch tracker lost it (fading string)
                colour = teal;
            }
        }
        else if (live && view.poly && view.polyReady && view.voices > 0)
        {
            big = juce::String (view.voices);
            colour = teal;
        }
        if (big.isNotEmpty())
        {
            const bool held = colour == teal;
            noteGlow.draw (g, big, noteFont, noteArea, juce::Justification::centred, colour,
                           colour.withAlpha (held ? 0.55f : 0.4f), held ? 10.0f : 8.0f);
        }
        else
        {
            g.setFont (idleFont);
            g.setColour (noteIdle);
            g.drawText (dash(), noteArea, juce::Justification::centred, false);
        }

        // ---- side: tuner (MONO) or voices (POLY)
        if (! view.poly)
        {
            const bool heard = live && view.note >= 0;
            const int c = juce::jlimit (-50, 50, view.cents);
            const float x = tunerBar.getCentreX() + (heard ? (float) c / 50.0f * (tunerBar.getWidth() * 0.5f - 2.0f) : 0.0f);
            const auto needle = juce::Rectangle<float> (4.0f, 16.0f).withCentre ({ x, tunerBar.getCentreY() });
            const auto needleColour = ! heard ? noteIdle : (std::abs (view.cents) < 5 ? teal : amber);
            if (heard)
            {
                g.setColour (needleColour.withAlpha (0.22f));
                g.fillRoundedRectangle (needle.expanded (4.0f, 3.0f), 5.0f);
                g.setColour (needleColour.withAlpha (0.35f));
                g.fillRoundedRectangle (needle.expanded (2.0f, 1.5f), 3.5f);
            }
            g.setColour (needleColour);
            g.fillRoundedRectangle (needle, 2.0f);

            if (heard)
            {
                g.setFont (centsFont);
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
            if (live && ! view.polyReady)
            {
                g.setFont (labelFont);
                g.setColour (amber);
                g.drawText (juce::String (juce::CharPointer_UTF8 ("LOADING\xe2\x80\xa6")), ledRow,
                            juce::Justification::centred, false);
            }
            else
            {
                for (int i = 0; i < maxVoices; ++i)
                    drawLed (g, voiceLeds[i], 4.5f, teal, live && i < view.voices);
            }

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
            const auto segColour = segmentColour (i);
            g.setColour (segColour.withAlpha (0.18f));
            g.fillRoundedRectangle (s.expanded (1.5f, 1.5f), 2.0f);
            g.setColour (segColour);
            g.fillRoundedRectangle (s, 1.2f);
        }
        g.setFont (monoSmall);
        g.setColour (dim);
        g.drawText ((live && view.levelDb > -60 ? juce::String (view.levelDb) : dash()) + " dB", dbArea,
                    juce::Justification::centredRight, false);

        // ---- readouts (the web card's words: TRK in MONO, CPU in POLY)
        const auto notes = juce::String (view.notes);
        if (! view.poly)
        {
            drawReadout (g, readoutArea, { { "TRK ", dim },
                                           { view.trkMs >= 0 ? juce::String (view.trkMs) : dash(), lcdTxt },
                                           { " ms" + dot + "NOTES ", dim },
                                           { notes, lcdTxt } });
        }
        else
        {
            const bool known = view.polyReady && view.cpuPct >= 0;
            // amber from 80 % (where ECO starts), red from 100 % (the analysis no longer keeps up)
            const auto cpuColour = ! known ? lcdTxt : (view.cpuPct >= 100 ? warnText : (view.cpuPct >= 80 ? amber : lcdTxt));
            drawReadout (g, readoutArea, { { "CPU ", dim },
                                           { known ? juce::String (view.cpuPct) : dash(), cpuColour },
                                           { " %" + dot + "NOTES ", dim },
                                           { notes, lcdTxt } });
        }
    }
}
