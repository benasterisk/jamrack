// The JAMRACK look for the MidPluck editor: the design tokens of css/style.css
// (warm charcoal, amber LEDs, teal accents, amber LCD), the three embedded
// typefaces (resources/fonts, SIL OFL 1.1) and the drawing helpers the widgets
// share (knob, pill switch, LED, recessed LCD box, button face, rack screw,
// glowing text).
//
// Everything here runs on the message thread, inside paint() or resized().
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jamrack
{
    /** css/style.css :root tokens (and the GUITAR -> MIDI card's own face tint). */
    namespace colours
    {
        inline const juce::Colour bg        { 0xff0d0b09 };
        inline const juce::Colour bgDeep    { 0xff080706 };
        inline const juce::Colour panel     { 0xff1b1713 };
        inline const juce::Colour panelHi   { 0xff241f19 };
        inline const juce::Colour panelLo   { 0xff141110 };
        inline const juce::Colour line      { 0xff383026 };
        inline const juce::Colour lineSoft  { 0xff2a241d };
        inline const juce::Colour txt       { 0xffece3d3 };
        inline const juce::Colour dim       { 0xcc94876f };   // --dim: #94876fcc
        inline const juce::Colour amber     { 0xffffb454 };
        inline const juce::Colour amberHot  { 0xffffd9a0 };
        inline const juce::Colour amberDim  { 0xff7a5626 };
        inline const juce::Colour teal      { 0xff3ad0c4 };
        inline const juce::Colour red       { 0xffff5040 };
        inline const juce::Colour warnText  { 0xffff8a7a };   // red text that stays legible on the LCD
        inline const juce::Colour green     { 0xff5fbf72 };   // level meter, low segments
        inline const juce::Colour lcdBg     { 0xff201807 };
        inline const juce::Colour lcdTxt    { 0xffffc46b };
        inline const juce::Colour ledOff    { 0xff3a3128 };
        inline const juce::Colour noteIdle  { 0xff6b5a3a };   // .gtr-note without a note
        inline const juce::Colour faceTop   { 0xff1f2221 };   // .module.guitar face
        inline const juce::Colour faceBottom{ 0xff171a19 };
        inline const juce::Colour btnTop    { 0xff2e2820 };   // .tb-btn / .sq-btn
        inline const juce::Colour btnBottom { 0xff1c1813 };
        inline const juce::Colour btnHover  { 0xff55483a };
    }

    /** The embedded faces (resources/fonts). */
    enum class Face
    {
        brand,          // Unbounded Medium (500)
        brandHeavy,     // Unbounded ExtraBold (800)
        label,          // Barlow Condensed Regular
        labelMedium,    // Barlow Condensed Medium
        labelSemi,      // Barlow Condensed SemiBold
        mono,           // Spline Sans Mono Regular
        monoMedium      // Spline Sans Mono Medium
    };

    /** The seven typefaces, created once per process from the binary data and shared
     *  by every editor through a juce::SharedResourcePointer. */
    class Fonts
    {
    public:
        Fonts();

        /** A font sized like CSS: px = font-size (the em), trackingEm = letter-spacing in em. */
        juce::Font get (Face, float px, float trackingEm = 0.0f) const;

    private:
        juce::Typeface::Ptr faces[7];
    };

    /** Rotary knobs, the BEND pill switch, square buttons, tooltips and the corner resizer. */
    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        juce::Font font (Face f, float px, float trackingEm = 0.0f) const { return fonts->get (f, px, trackingEm); }

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&,
                             bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                               juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

        /** The grip engraved in the face's corner; w x h = 18 x 18 at the base size, scaled
         *  with the face by the editor. */
        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

    private:
        juce::TextLayout layoutTooltip (const juce::String&) const;

        juce::SharedResourcePointer<Fonts> fonts;
    };

    //==============================================================================
    // Drawing helpers (message thread, inside paint)

    /** The scale between the logical coordinates of g and device pixels (editor
     *  transform x display scaling): cached images are rendered at this scale. */
    float physicalScale (juce::Graphics&);

    /** A round LED (.led): dark when off, coloured with a soft halo when lit. */
    void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour, bool lit, float glow = 1.0f);

    /** A recessed box (.mod-lcd / .gtr-tuner): fill, black border, inner shadow, lower highlight. */
    void drawRecessed (juce::Graphics&, juce::Rectangle<float>, float cornerRadius, juce::Colour fill);

    /** A raised button face (.tb-btn / .sq-btn); `latched` = the pressed look (amber border). */
    void drawButtonFace (juce::Graphics&, juce::Rectangle<float>, float cornerRadius,
                         bool hover, bool down, bool latched, juce::Colour latchedBorder = colours::amberDim);

    /** A slotted rack screw (.module::before), its slot turned by `angle` radians. */
    void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angle);

    /** True for the characters the embedded label and mono subsets lack (arrows,
     *  Greek): text in those faces draws them with Unbounded instead (arrangeLine). */
    bool needsBrandGlyph (juce::juce_wchar) noexcept;

    /** One line of text in `font`, the characters it lacks (needsBrandGlyph) in `alt`,
     *  justified in `area` like Graphics::drawText. */
    juce::GlyphArrangement arrangeLine (const juce::String& text, const juce::Font& font, const juce::Font& alt,
                                        juce::Rectangle<float> area, juce::Justification);

    /** Text with a soft halo (CSS text-shadow 0 0 radius colour). The blurred halo is an
     *  image cached until the text, font, area, colour or scale change, so a display
     *  repainted at 30 Hz blurs only when its text changes. With `alt`, the characters
     *  `font` lacks are drawn in `alt` (arrangeLine). */
    class GlowText
    {
    public:
        void draw (juce::Graphics&, const juce::String& text, const juce::Font&, juce::Rectangle<float> area,
                   juce::Justification, juce::Colour textColour, juce::Colour glowColour, float radius,
                   const juce::Font* alt = nullptr);

    private:
        void drawText (juce::Graphics&) const;

        juce::String text;
        juce::Font font { juce::FontOptions {} };
        juce::Font altFont { juce::FontOptions {} };
        bool hasAlt = false;
        juce::Rectangle<float> area, haloArea;
        juce::Justification just { juce::Justification::centred };
        float radius = 0.0f, scale = 0.0f, haloScale = 1.0f;
        juce::Image halo;
    };
}
