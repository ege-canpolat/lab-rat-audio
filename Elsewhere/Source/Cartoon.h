#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The 2000s-cartoon toolkit: palette, fonts, outlined shapes, wobbly blobs.
namespace cartoon
{
namespace colours
{
inline const juce::Colour ink     { 0xff1a1033 };   // outlines: deep purple-black
inline const juce::Colour orange  { 0xffff7a00 };
inline const juce::Colour slime   { 0xff9be81e };
inline const juce::Colour slimeDk { 0xff5fae0e };
inline const juce::Colour purple  { 0xff6a2bc2 };
inline const juce::Colour purpleDk{ 0xff45188a };
inline const juce::Colour pink    { 0xffff3ea5 };
inline const juce::Colour cyan    { 0xff22d3ee };
inline const juce::Colour yellow  { 0xffffe01a };
inline const juce::Colour blue    { 0xff2f6bff };
inline const juce::Colour red     { 0xffff3b30 };
inline const juce::Colour cream   { 0xfffff8e7 };
inline const juce::Colour teal    { 0xff12c2a5 };
}

struct Fonts
{
    Fonts();
    juce::Font display (float height) const;   // Luckiest Guy
    juce::Font body (float height) const;      // Fredoka

    juce::Typeface::Ptr displayFace, bodyFace;
};

const Fonts& fonts();

// Fill + hard drop shadow + thick outline. The whole look in one call.
void shape (juce::Graphics&, const juce::Path&, juce::Colour fill,
            float outline = 4.0f, juce::Point<float> shadow = { 4.0f, 5.0f },
            juce::Colour outlineColour = colours::ink);

// Text with an ink outline and a hard shadow.
void text (juce::Graphics&, const juce::String&, const juce::Font&, juce::Rectangle<float> area,
           juce::Justification, juce::Colour fill, float outline = 3.0f,
           juce::Point<float> shadow = { 2.5f, 3.0f }, float rotationRadians = 0.0f, int maxLines = 1);

juce::Path blob (juce::Rectangle<float> r, float wobble, int seed, int points = 10);
juce::Path wobblyRect (juce::Rectangle<float> r, float corner, float wobble, int seed);
juce::Path splat (juce::Point<float> centre, float radius, int seed, int lobes = 11);

// A little quaver, for sound escaping from wherever the music is.
juce::Path note (juce::Point<float> centre, float size, int variant);

// Notes drifting out of an emitter. Stateless: positions come from time.
void noteStream (juce::Graphics&, juce::Point<float> from, float angle, float distance, float spread,
                 int count, float time, float speed, float intensity, float size, int seed);

// Our recurring blob buddy.
struct Buddy
{
    juce::Colour colour = colours::pink;
    float size = 60.0f;
    float squash = 0.0f;      // bounce to the beat
    juce::Point<float> look { 0.0f, 0.0f };
    enum Mood { happy, annoyed, chill, shocked } mood = happy;
    bool armUp = false;
    bool back = false;        // seen from behind (at the urinal, at the mirror)
    bool queasy = false;      // about to be sick
    float alpha = 1.0f;
};

void drawBuddy (juce::Graphics&, juce::Point<float> feet, const Buddy&);

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
};

// A chunky button that squishes down when pressed.
class BlobButton : public juce::Button
{
public:
    BlobButton (const juce::String& label, juce::Colour colour, int seed, float tiltDegrees = 0.0f);

    void setLabels (const juce::String& off, const juce::String& on) { offLabel = off; onLabel = on; repaint(); }
    void setColour (juce::Colour c) { colour = c; repaint(); }
    void setOnColour (juce::Colour c) { onColour = c; repaint(); }
    void setFontHeight (float h) { fontHeight = h; repaint(); }
    void setSelectedStyle (bool radioStyle) { radio = radioStyle; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String offLabel, onLabel;
    juce::Colour colour, onColour;
    int seed;
    float tilt, fontHeight = 22.0f;
    bool radio = false;
};

} // namespace cartoon
