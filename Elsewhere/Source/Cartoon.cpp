#include "Cartoon.h"
#include "BinaryData.h"

namespace cartoon
{
using namespace juce;

Fonts::Fonts()
{
    displayFace = Typeface::createSystemTypefaceFor (BinaryData::LuckiestGuyRegular_ttf,
                                                     (size_t) BinaryData::LuckiestGuyRegular_ttfSize);
    bodyFace = Typeface::createSystemTypefaceFor (BinaryData::Fredoka_ttf, (size_t) BinaryData::Fredoka_ttfSize);
}

Font Fonts::display (float height) const
{
    if (displayFace != nullptr)
        return Font (FontOptions (displayFace).withHeight (height));
    return Font (FontOptions ("Marker Felt", height, Font::bold));
}

Font Fonts::body (float height) const
{
    if (bodyFace != nullptr)
        return Font (FontOptions (bodyFace).withHeight (height));
    return Font (FontOptions ("Arial Rounded MT Bold", height, Font::plain));
}

const Fonts& fonts()
{
    static Fonts instance;
    return instance;
}

//==============================================================================
void shape (Graphics& g, const Path& p, Colour fill, float outline, Point<float> shadow, Colour outlineColour)
{
    if (! shadow.isOrigin())
    {
        g.setColour (colours::ink.withAlpha (0.35f * fill.getFloatAlpha()));
        g.fillPath (p, AffineTransform::translation (shadow));
    }
    g.setColour (fill);
    g.fillPath (p);
    if (outline > 0)
    {
        g.setColour (outlineColour.withMultipliedAlpha (fill.getFloatAlpha()));
        g.strokePath (p, PathStrokeType (outline, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void text (Graphics& g, const String& s, const Font& font, Rectangle<float> area, Justification j,
           Colour fill, float outline, Point<float> shadow, float rotation, int maxLines)
{
    GlyphArrangement glyphs;
    glyphs.addFittedText (font, s, area.getX(), area.getY(), area.getWidth(), area.getHeight(), j, maxLines, 0.8f);
    Path p;
    glyphs.createPath (p);
    if (rotation != 0)
        p.applyTransform (AffineTransform::rotation (rotation, area.getCentreX(), area.getCentreY()));

    if (! shadow.isOrigin())
    {
        g.setColour (colours::ink.withAlpha (0.9f * fill.getFloatAlpha()));
        g.fillPath (p, AffineTransform::translation (shadow));
        if (outline > 0)
            g.strokePath (p, PathStrokeType (outline * 2.0f, PathStrokeType::curved, PathStrokeType::rounded),
                          AffineTransform::translation (shadow));
    }
    if (outline > 0)
    {
        g.setColour (colours::ink.withAlpha (fill.getFloatAlpha()));
        g.strokePath (p, PathStrokeType (outline * 2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setColour (fill);
    g.fillPath (p);
}

//==============================================================================
static Path smoothClosed (const Array<Point<float>>& pts)
{
    Path p;
    const int n = pts.size();
    if (n < 3)
        return p;
    auto mid = [&] (int i) { return (pts[i % n] + pts[(i + 1) % n]) * 0.5f; };
    p.startNewSubPath (mid (n - 1));
    for (int i = 0; i < n; ++i)
        p.quadraticTo (pts[i], mid (i));
    p.closeSubPath();
    return p;
}

Path blob (Rectangle<float> r, float wobble, int seed, int points)
{
    Random rng (seed);
    Array<Point<float>> pts;
    for (int i = 0; i < points; ++i)
    {
        const float a = MathConstants<float>::twoPi * (float) i / (float) points;
        const float k = 1.0f + wobble * (rng.nextFloat() * 2.0f - 1.0f);
        pts.add ({ r.getCentreX() + std::cos (a) * r.getWidth() * 0.5f * k * 1.08f,
                   r.getCentreY() + std::sin (a) * r.getHeight() * 0.5f * k * 1.08f });
    }
    return smoothClosed (pts);
}

Path wobblyRect (Rectangle<float> r, float corner, float wobble, int seed)
{
    Random rng (seed);
    auto jitter = [&] { return (rng.nextFloat() * 2.0f - 1.0f) * wobble; };
    Point<float> c[4] = { r.getTopLeft(), r.getTopRight(), r.getBottomRight(), r.getBottomLeft() };
    for (auto& p : c)
        p += Point<float> (jitter(), jitter());

    corner = jmin (corner, r.getWidth() * 0.45f, r.getHeight() * 0.45f);
    Path p;
    for (int i = 0; i < 4; ++i)
    {
        const auto a = c[i], b = c[(i + 1) % 4];
        const auto dir = (b - a) / jmax (1.0f, a.getDistanceFrom (b));
        const auto start = a + dir * corner, end = b - dir * corner;
        const Point<float> normal (dir.y, -dir.x);
        const auto bulge = (start + end) * 0.5f + normal * jitter() * 0.8f;

        if (i == 0)
            p.startNewSubPath (start);
        else
            p.quadraticTo (a, start);
        p.quadraticTo (bulge, end);
    }
    p.quadraticTo (c[0], c[0] + (c[1] - c[0]) / jmax (1.0f, c[0].getDistanceFrom (c[1])) * corner);
    p.closeSubPath();
    return p;
}

Path splat (Point<float> centre, float radius, int seed, int lobes)
{
    Random rng (seed);
    Array<Point<float>> pts;
    for (int i = 0; i < lobes * 2; ++i)
    {
        const float a = MathConstants<float>::twoPi * (float) i / (float) (lobes * 2) + rng.nextFloat() * 0.15f;
        const float k = (i % 2 == 0) ? 1.0f + rng.nextFloat() * 0.18f : 0.72f + rng.nextFloat() * 0.1f;
        pts.add (centre + Point<float> (std::cos (a), std::sin (a) * 0.62f) * radius * k);
    }
    auto p = smoothClosed (pts);

    // A few flung droplets.
    for (int i = 0; i < 4; ++i)
    {
        const float a = rng.nextFloat() * MathConstants<float>::twoPi;
        const float d = radius * (1.18f + rng.nextFloat() * 0.12f);
        const float s = radius * (0.04f + rng.nextFloat() * 0.04f);
        p.addEllipse (centre.x + std::cos (a) * d - s, centre.y + std::sin (a) * d * 0.62f - s, s * 2, s * 2);
    }
    return p;
}

Path note (Point<float> c, float s, int variant)
{
    Path p;
    const float headW = s * 0.62f, headH = s * 0.44f;
    if (variant % 2 == 0)
    {
        // Single quaver.
        Path head;
        head.addEllipse (-headW * 0.5f, -headH * 0.5f, headW, headH);
        head.applyTransform (AffineTransform::rotation (-0.45f).translated (c));
        p.addPath (head);
        const float sx = c.x + headW * 0.38f;
        p.addRoundedRectangle (sx - s * 0.06f, c.y - s * 1.05f, s * 0.12f, s * 1.05f, s * 0.05f);
        Path flag;
        flag.startNewSubPath (sx, c.y - s * 1.05f);
        flag.cubicTo (sx + s * 0.5f, c.y - s * 0.85f, sx + s * 0.55f, c.y - s * 0.45f, sx + s * 0.25f, c.y - s * 0.25f);
        flag.cubicTo (sx + s * 0.3f, c.y - s * 0.5f, sx + s * 0.2f, c.y - s * 0.68f, sx, c.y - s * 0.7f);
        flag.closeSubPath();
        p.addPath (flag);
    }
    else
    {
        // Pair of beamed quavers.
        for (int k = 0; k < 2; ++k)
        {
            const Point<float> hc (c.x - s * 0.35f + k * s * 0.75f, c.y + k * s * -0.12f);
            Path head;
            head.addEllipse (-headW * 0.45f, -headH * 0.45f, headW * 0.9f, headH * 0.9f);
            head.applyTransform (AffineTransform::rotation (-0.45f).translated (hc));
            p.addPath (head);
            p.addRoundedRectangle (hc.x + headW * 0.3f - s * 0.055f, hc.y - s * 0.95f, s * 0.11f, s * 0.95f, s * 0.04f);
        }
        Path beam;
        const float x0 = c.x - s * 0.35f + headW * 0.3f, x1 = c.x + s * 0.4f + headW * 0.3f;
        beam.startNewSubPath (x0 - s * 0.05f, c.y - s * 0.95f);
        beam.lineTo (x1 + s * 0.05f, c.y - s * 1.07f);
        beam.lineTo (x1 + s * 0.05f, c.y - s * 0.85f);
        beam.lineTo (x0 - s * 0.05f, c.y - s * 0.73f);
        beam.closeSubPath();
        p.addPath (beam);
    }
    return p;
}

void noteStream (Graphics& g, Point<float> from, float angle, float distance, float spread,
                 int count, float time, float speed, float intensity, float size, int seed)
{
    if (intensity <= 0.01f)
        return;

    static const Colour palette[] = { colours::yellow, colours::pink, colours::cyan, colours::slime, colours::orange, colours::cream };
    Random rng (seed);
    for (int i = 0; i < count; ++i)
    {
        const float offset = rng.nextFloat();
        const float wiggle = rng.nextFloat() * 6.28f;
        const float aJit = (rng.nextFloat() * 2.0f - 1.0f) * spread;
        const auto colour = palette[rng.nextInt (6)];
        const float phase = std::fmod (time * speed + offset, 1.0f);
        const float a = angle + aJit;
        const Point<float> dir (std::cos (a), std::sin (a));
        const Point<float> perp (-dir.y, dir.x);
        const auto pos = from + dir * (phase * distance) + perp * (std::sin (time * 3.0f + wiggle) * 8.0f);
        const float alpha = intensity * std::sin (MathConstants<float>::pi * phase);
        const float s = size * (0.7f + 0.6f * phase);
        auto p = note ({ 0, 0 }, s, i);
        p.applyTransform (AffineTransform::rotation (std::sin (time * 2.0f + wiggle) * 0.35f).translated (pos));
        shape (g, p, colour.withAlpha (jlimit (0.0f, 1.0f, alpha)), 2.2f, { 2.0f, 2.0f });
    }
}

//==============================================================================
void drawBuddy (Graphics& g, Point<float> feet, const Buddy& b)
{
    const float w = b.size * (1.0f + 0.12f * b.squash);
    const float h = b.size * 1.15f * (1.0f - 0.12f * b.squash);
    const Rectangle<float> body (feet.x - w * 0.5f, feet.y - h, w, h);
    const float a = b.alpha;
    const auto colour = b.queasy ? b.colour.interpolatedWith (colours::slime, 0.55f) : b.colour;

    Path bodyPath;
    bodyPath.addRoundedRectangle (body, w * 0.45f);
    shape (g, bodyPath, colour.withAlpha (a), 3.5f, { 3.0f, 4.0f });

    if (b.back)
    {
        // The back of a head: a tuft of hair and nothing else to see here.
        Path tuft;
        const float cx = body.getCentreX(), top = body.getY() + 3.0f;
        for (int i = -1; i <= 1; ++i)
        {
            tuft.startNewSubPath (cx + (float) i * w * 0.08f, top + 2.0f);
            tuft.quadraticTo (cx + (float) i * w * 0.14f, top - h * 0.08f, cx + (float) i * w * 0.2f, top - h * 0.12f);
        }
        g.setColour (colours::ink.withAlpha (a));
        g.strokePath (tuft, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (colours::ink.withAlpha (0.15f * a));
        g.fillRoundedRectangle (body.getX() + w * 0.2f, body.getY() + h * 0.55f, w * 0.6f, h * 0.06f, 2.0f);
        return;
    }

    // Belly shine.
    g.setColour (Colours::white.withAlpha (0.25f * a));
    g.fillEllipse (body.getX() + w * 0.18f, body.getY() + h * 0.12f, w * 0.22f, h * 0.18f);

    const float eyeY = body.getY() + h * 0.36f;
    const float eyeR = w * 0.15f;
    for (int side = -1; side <= 1; side += 2)
    {
        const Point<float> ec (body.getCentreX() + side * w * 0.2f, eyeY);
        Path eye;
        eye.addEllipse (ec.x - eyeR, ec.y - eyeR * 1.15f, eyeR * 2, eyeR * 2.3f);
        shape (g, eye, colours::cream.withAlpha (a), 2.5f, {});
        const auto pupil = ec + b.look * eyeR * 0.45f;
        g.setColour (colours::ink.withAlpha (a));
        const float pr = eyeR * (b.mood == Buddy::shocked ? 0.35f : 0.55f);
        g.fillEllipse (pupil.x - pr, pupil.y - pr, pr * 2, pr * 2);

        if (b.mood == Buddy::annoyed)
        {
            // Cross eyebrows.
            g.drawLine (ec.x - eyeR * 1.1f, ec.y - eyeR * (side < 0 ? 1.9f : 1.3f),
                        ec.x + eyeR * 1.1f, ec.y - eyeR * (side < 0 ? 1.3f : 1.9f), 3.0f);
        }
        if (b.mood == Buddy::chill)
        {
            // Sleepy lids.
            Path lid;
            lid.addPieSegment (ec.x - eyeR * 1.05f, ec.y - eyeR * 1.2f, eyeR * 2.1f, eyeR * 2.4f,
                               -MathConstants<float>::halfPi, MathConstants<float>::halfPi, 0.0f);
            shape (g, lid, b.colour.darker (0.2f).withAlpha (a), 2.5f, {});
        }
    }

    // Mouth.
    const Point<float> mc (body.getCentreX(), body.getY() + h * 0.62f);
    Path mouth;
    switch (b.queasy ? Buddy::shocked : b.mood)
    {
        case Buddy::happy:
            mouth.addPieSegment (mc.x - w * 0.18f, mc.y - w * 0.14f, w * 0.36f, w * 0.28f,
                                 MathConstants<float>::halfPi, MathConstants<float>::halfPi * 3.0f, 0.0f);
            shape (g, mouth, colours::ink.withAlpha (a), 0, {});
            break;
        case Buddy::shocked:
            mouth.addEllipse (mc.x - w * 0.08f, mc.y - w * 0.06f, w * 0.16f, w * 0.2f);
            shape (g, mouth, colours::ink.withAlpha (a), 0, {});
            break;
        case Buddy::annoyed:
            mouth.startNewSubPath (mc.x - w * 0.14f, mc.y + 3);
            mouth.quadraticTo (mc.x, mc.y - 5, mc.x + w * 0.14f, mc.y + 3);
            g.setColour (colours::ink.withAlpha (a));
            g.strokePath (mouth, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
            break;
        case Buddy::chill:
            mouth.startNewSubPath (mc.x - w * 0.12f, mc.y);
            mouth.quadraticTo (mc.x, mc.y + 7, mc.x + w * 0.14f, mc.y - 2);
            g.setColour (colours::ink.withAlpha (a));
            g.strokePath (mouth, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
            break;
    }

    if (b.armUp)
    {
        Path arm;
        arm.startNewSubPath (body.getX() + w * 0.1f, body.getY() + h * 0.55f);
        arm.quadraticTo (body.getX() - w * 0.35f, body.getY() + h * 0.4f, body.getX() - w * 0.4f, body.getY() + h * 0.05f);
        g.setColour (colours::ink.withAlpha (a));
        g.strokePath (arm, PathStrokeType (9.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (b.colour.withAlpha (a));
        g.strokePath (arm, PathStrokeType (4.5f, PathStrokeType::curved, PathStrokeType::rounded));
        Path fist;
        fist.addEllipse (body.getX() - w * 0.52f, body.getY() - h * 0.1f, w * 0.26f, w * 0.26f);
        shape (g, fist, b.colour.withAlpha (a), 3.0f, {});
    }
}

//==============================================================================
void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                                    float startAngle, float endAngle, Slider& slider)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (8.0f);
    const float r = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto fill = slider.findColour (Slider::rotarySliderFillColourId);

    // Track + value arc, chunky with ink outlines.
    auto arc = [&] (float from, float to, float thickness, Colour col)
    {
        Path p;
        p.addCentredArc (c.x, c.y, r - 4, r - 4, 0, from, to, true);
        g.setColour (colours::ink);
        g.strokePath (p, PathStrokeType (thickness + 5, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (p, PathStrokeType (thickness, PathStrokeType::curved, PathStrokeType::rounded));
    };
    arc (startAngle, endAngle, 7.0f, colours::purpleDk);
    if (pos > 0.005f)
        arc (startAngle, angle, 7.0f, colours::yellow);

    // Knob body.
    const float kr = r * 0.68f;
    Path body = blob ({ c.x - kr, c.y - kr, kr * 2, kr * 2 }, 0.03f, 7, 12);
    shape (g, body, fill, 4.0f, { 3.5f, 4.5f });
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillEllipse (c.x - kr * 0.6f, c.y - kr * 0.75f, kr * 0.7f, kr * 0.45f);

    // Pointer.
    const Point<float> tip (c.x + std::sin (angle) * kr * 0.72f, c.y - std::cos (angle) * kr * 0.72f);
    g.setColour (colours::ink);
    g.drawLine ({ c, tip }, 7.0f);
    g.fillEllipse (tip.x - 5, tip.y - 5, 10, 10);
    g.fillEllipse (c.x - 4, c.y - 4, 8, 8);
}

//==============================================================================
BlobButton::BlobButton (const String& label, Colour c, int s, float tiltDegrees)
    : Button (label), offLabel (label), onLabel (label), colour (c), seed (s),
      tilt (degreesToRadians (tiltDegrees))
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void BlobButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    const bool on = getToggleState();
    auto area = getLocalBounds().toFloat().reduced (6.0f, 5.0f);
    const Point<float> press = down ? Point<float> (3.0f, 4.0f) : Point<float> (0.0f, 0.0f);
    area = area.translated (press.x, press.y);
    if (! down)
        area = area.translated (0.0f, highlighted ? -1.5f : 0.0f);

    Graphics::ScopedSaveState state (g);
    g.addTransform (AffineTransform::rotation (tilt, area.getCentreX(), area.getCentreY()));

    auto fill = (on && ! onColour.isTransparent()) ? onColour : colour;
    if (radio && ! on)
        fill = colour.withMultipliedSaturation (0.55f).withMultipliedBrightness (0.72f);
    if (highlighted && ! on)
        fill = fill.brighter (0.12f);

    auto p = wobblyRect (area, area.getHeight() * 0.42f, 2.0f, seed);
    if (radio && on)
    {
        // Selected: a yellow "glow" ring behind it.
        g.setColour (colours::yellow);
        g.strokePath (p, PathStrokeType (14.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    shape (g, p, fill, 4.0f, down ? Point<float> (0.5f, 1.0f) : Point<float> (3.5f, 4.5f));

    // Glossy highlight strip.
    g.setColour (Colours::white.withAlpha (0.22f));
    g.fillRoundedRectangle (area.reduced (area.getHeight() * 0.3f, 0).withHeight (area.getHeight() * 0.28f)
                                .translated (0, area.getHeight() * 0.1f), area.getHeight() * 0.14f);

    const auto label = on ? onLabel : offLabel;
    cartoon::text (g, label, fonts().display (fontHeight), area.reduced (10, 0).translated (0, 2), Justification::centred,
          (radio && ! on) ? colours::cream.withAlpha (0.85f) : Colours::white, 2.5f, { 2.0f, 2.5f });
}

} // namespace cartoon
