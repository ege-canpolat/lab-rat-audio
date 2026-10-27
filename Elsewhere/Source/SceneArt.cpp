#include "SceneArt.h"

namespace art
{
using namespace juce;
using namespace cartoon;

namespace
{
constexpr float twoPi = MathConstants<float>::twoPi;

float frac (float x) { return x - std::floor (x); }

float smoothstep (float t)
{
    t = jlimit (0.0f, 1.0f, t);
    return t * t * (3.0f - 2.0f * t);
}

// Pop-in scale for comic bursts: overshoot, then settle.
float popScale (float age)
{
    if (age < 0.12f) return 0.3f + (age / 0.12f) * 0.9f;
    return 1.2f - 0.2f * jmin (1.0f, (age - 0.12f) / 0.15f);
}

Path rect (Rectangle<float> r) { Path p; p.addRectangle (r); return p; }

void comicBurst (Graphics& g, Point<float> c, float radius, const String& words, Colour fill, float age, int seed)
{
    if (age > 1.0f)
        return;
    const float alpha = age < 0.75f ? 1.0f : 1.0f - (age - 0.75f) / 0.25f;
    const float s = popScale (age);

    // Fade the whole burst as one layer, so overlapping outlines don't show through.
    Graphics::ScopedSaveState save (g);
    g.beginTransparencyLayer (alpha);
    g.addTransform (AffineTransform::scale (s, s, c.x, c.y).rotated (-0.12f, c.x, c.y));

    Random rng (seed);
    Path star;
    const int spikes = 14;
    for (int i = 0; i < spikes * 2; ++i)
    {
        const float a = twoPi * (float) i / (float) (spikes * 2);
        const float r = (i % 2 == 0 ? radius : radius * 0.68f) * (0.9f + 0.2f * rng.nextFloat());
        const Point<float> p (c.x + std::cos (a) * r * 1.5f, c.y + std::sin (a) * r * 0.8f);
        if (i == 0) star.startNewSubPath (p); else star.lineTo (p);
    }
    star.closeSubPath();
    shape (g, star, fill, 4.0f, { 4.0f, 5.0f });
    text (g, words, fonts().display (radius * 0.55f), { c.x - radius * 1.3f, c.y - radius * 0.5f, radius * 2.6f, radius },
          Justification::centred, colours::red, 2.5f, { 2.0f, 2.5f });
    g.endTransparencyLayer();
}

void speechBubble (Graphics& g, Rectangle<float> r, Point<float> tail, const String& words, float alpha, float fontH = 20.0f)
{
    if (alpha <= 0.01f)
        return;

    Graphics::ScopedSaveState save (g);
    g.beginTransparencyLayer (alpha);

    const Path body = wobblyRect (r, jmin (16.0f, r.getHeight() * 0.45f), 1.5f, 3);
    Path t;
    const float base = jmin (12.0f, r.getWidth() * 0.15f);
    const float tx = jlimit (r.getX() + base + 8, r.getRight() - base - 8, tail.x);
    t.startNewSubPath (tx - base, r.getBottom() - 8);
    t.lineTo (tail);
    t.lineTo (tx + base, r.getBottom() - 8);
    t.closeSubPath();

    // Shadow, then outlines, then fills on top: the seam between tail and body vanishes.
    g.setColour (colours::ink.withAlpha (0.35f));
    g.fillPath (body, AffineTransform::translation (3, 4));
    g.fillPath (t, AffineTransform::translation (3, 4));
    g.setColour (colours::ink);
    const PathStrokeType stroke (7.0f, PathStrokeType::curved, PathStrokeType::rounded);
    g.strokePath (body, stroke);
    g.strokePath (t, stroke);
    g.setColour (colours::cream);
    g.fillPath (body);
    g.fillPath (t);

    text (g, words, fonts().display (fontH), r.reduced (8, 3).translated (0, 1.5f), Justification::centred,
          colours::ink, 0, {}, 0, 2);
    g.endTransparencyLayer();
}

void stripes (Graphics& g, Rectangle<float> area, Colour c, float w, float step)
{
    g.setColour (c);
    for (float x = area.getX(); x < area.getRight(); x += step)
        g.fillRect (x, area.getY(), w, area.getHeight());
}

void clubInterior (Graphics& g, Rectangle<float> r, const State& s)
{
    g.setColour (Colour (0xff2a0f4d));
    g.fillRect (r);

    Random rng (99);
    static const Colour lights[] = { colours::pink, colours::cyan, colours::yellow, colours::slime, colours::orange };
    for (int i = 0; i < 9; ++i)
    {
        const float x = r.getX() + rng.nextFloat() * r.getWidth();
        const float y = r.getY() + rng.nextFloat() * r.getHeight() * 0.6f;
        const float rad = 8.0f + rng.nextFloat() * 18.0f;
        const float flash = 0.5f + 0.5f * std::sin (s.time * (6.0f + (float) i) + (float) i * 2.1f);
        g.setColour (lights[i % 5].withAlpha ((0.25f + 0.6f * s.level) * flash));
        g.fillEllipse (x - rad, y - rad, rad * 2, rad * 2);
    }

    // The crowd, bouncing.
    for (int i = 0; i < 6; ++i)
    {
        const float x = r.getX() + (float) (i + 0.5f) * r.getWidth() / 6.0f;
        const float bob = std::sin (s.time * 9.0f + (float) i * 1.3f) * (2.0f + 6.0f * s.bass);
        const float y = r.getBottom() - 34 + bob + (float) (i % 2) * 8.0f;
        g.setColour (Colour (0xff12061f));
        g.fillEllipse (x - 13, y - 13, 26, 26);
        g.fillRoundedRectangle (x - 16, y + 10, 32, 50, 12);
        if (i % 2 == 0)
        {
            g.drawLine (x - 10, y + 16, x - 22, y - 14 + bob, 6.0f);
            g.drawLine (x + 10, y + 16, x + 22, y - 14 + bob, 6.0f);
        }
    }
}

// A door hinged on its left edge that swings towards us.
void doorway (Graphics& g, Rectangle<float> frame, float o, Colour leafColour, const State& s,
              const std::function<void (Graphics&, const Path&)>& leafDecor)
{
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (frame.toNearestInt());
        clubInterior (g, frame, s);
    }

    const float swing = o * degreesToRadians (78.0f);
    const float freeX = frame.getX() + frame.getWidth() * std::cos (swing);
    const float grow = std::sin (swing) * frame.getHeight() * 0.07f;

    Path leaf;
    leaf.startNewSubPath (frame.getX(), frame.getY());
    leaf.lineTo (freeX, frame.getY() - grow);
    leaf.lineTo (freeX, frame.getBottom() + grow);
    leaf.lineTo (frame.getX(), frame.getBottom());
    leaf.closeSubPath();

    // Frame.
    g.setColour (colours::ink);
    g.drawRect (frame.expanded (3), 5.0f);

    shape (g, leaf, leafColour.darker (0.45f * o), 4.0f, { 3.0f, 3.0f });
    if (leafDecor)
        leafDecor (g, leaf);

    // Handle on the free edge.
    const float hx = freeX - 14.0f * std::cos (swing);
    g.setColour (colours::yellow);
    g.fillEllipse (hx - 6, frame.getCentreY() - 6, 12, 12);
    g.setColour (colours::ink);
    g.drawEllipse (hx - 6, frame.getCentreY() - 6, 12, 12, 2.5f);
}

void lightSpill (Graphics& g, Rectangle<float> frame, float floorY, float o, Colour c)
{
    if (o < 0.01f)
        return;
    Path p;
    p.startNewSubPath (frame.getX(), frame.getBottom());
    p.lineTo (frame.getRight(), frame.getBottom());
    p.lineTo (frame.getRight() + 80, floorY);
    p.lineTo (frame.getX() - 50, floorY);
    p.closeSubPath();
    g.setColour (c.withAlpha (0.35f * o));
    g.fillPath (p);
}

//==============================================================================
void drawNextDoor (Graphics& g, const State& s)
{
    g.fillAll (Colour (0xffffd23f));
    stripes (g, { 0, 0, width, 250 }, colours::pink.withAlpha (0.35f), 16, 40);

    // Floor + skirting.
    g.setColour (Colour (0xff8b5a2b));
    g.fillRect (0.0f, 256.0f, width, height - 256.0f);
    g.setColour (colours::ink.withAlpha (0.35f));
    for (float x = -20; x < width; x += 70)
        g.drawLine (x, 256, x + 30, height, 2.0f);
    shape (g, rect ({ -5, 246, width + 10, 12 }), colours::cream, 3.0f, {});

    const float shake = jlimit (0.0f, 1.0f, s.bass * (0.3f + s.vibes) * (1.0f - 0.7f * s.duck));

    // Wonky picture frame that rattles with the bass.
    {
        const Point<float> c (150.0f, 104.0f + std::sin (s.time * 47.0f) * 2.0f * shake);
        Graphics::ScopedSaveState save (g);
        g.addTransform (AffineTransform::rotation (0.04f + std::sin (s.time * 31.0f) * 0.07f * shake, c.x, c.y));
        const Rectangle<float> f (c.x - 58, c.y - 44, 116, 88);
        shape (g, wobblyRect (f, 6, 1.5f, 8), colours::orange, 4.0f);
        const auto inner = f.reduced (13);
        shape (g, rect (inner), Colour (0xff7ad7ff), 3.0f, {});
        {
            // The little landscape stays inside its frame.
            Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (rect (inner));
            g.setColour (colours::slime);
            g.fillEllipse (inner.getX() - 10, inner.getBottom() - 18, inner.getWidth() + 20, 40);
            g.setColour (colours::yellow);
            g.fillEllipse (inner.getX() + 8, inner.getY() + 6, 22, 22);
        }
        g.setColour (colours::ink);
        g.strokePath (rect (inner), PathStrokeType (3.0f));
        // The nail.
        g.setColour (colours::ink);
        g.drawLine (c.x, f.getY(), c.x, f.getY() - 18, 2.0f);
        g.fillEllipse (c.x - 3, f.getY() - 22, 6, 6);
    }

    // Little portrait.
    {
        const Point<float> c (270.0f, 70.0f);
        Graphics::ScopedSaveState save (g);
        g.addTransform (AffineTransform::rotation (-0.06f + std::sin (s.time * 37.0f + 1.0f) * 0.05f * shake, c.x, c.y));
        shape (g, blob ({ c.x - 30, c.y - 36, 60, 72 }, 0.03f, 4, 10), colours::cyan, 4.0f);
        shape (g, blob ({ c.x - 20, c.y - 26, 40, 52 }, 0.02f, 5, 10), colours::cream, 3.0f, {});
        Buddy b; b.size = 26; b.colour = colours::pink; b.mood = Buddy::happy;
        drawBuddy (g, { c.x, c.y + 20 }, b);
    }

    // Plant.
    shape (g, wobblyRect ({ 30, 210, 46, 44 }, 6, 1, 12), colours::orange, 3.5f);
    for (int i = 0; i < 5; ++i)
    {
        Path leaf;
        leaf.addEllipse (-7, -24, 14, 34);
        leaf.applyTransform (AffineTransform::rotation (-0.9f + 0.45f * (float) i + std::sin (s.time * 2.0f + (float) i) * 0.05f)
                                 .translated (53, 208));
        shape (g, leaf, colours::slime, 3.0f, {});
    }

    // Where the music comes from: the shared wall.
    const Point<float> source (400, 120);
    for (int k = 0; k < 3; ++k)
    {
        const float ph = frac (s.time * 0.8f + (float) k / 3.0f);
        const float r = 20 + ph * 120;
        g.setColour (colours::pink.withAlpha ((1 - ph) * 0.6f * (0.25f + s.bass) * (1 - 0.6f * s.duck)));
        g.drawEllipse (source.x - r, source.y - r * 0.8f, r * 2, r * 1.6f, 5.0f);
    }
    Path crack;
    crack.startNewSubPath (410, 0);
    crack.lineTo (418, 22); crack.lineTo (406, 40); crack.lineTo (421, 62); crack.lineTo (412, 78);
    crack.startNewSubPath (418, 22); crack.lineTo (432, 30);
    g.setColour (colours::ink);
    g.strokePath (crack, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    noteStream (g, source, -MathConstants<float>::halfPi - 0.4f, 120, 1.4f, 7, s.time, 0.35f,
                (0.35f + 0.65f * s.bass) * (1 - 0.7f * s.duck) * 0.85f, 22, 11);

    // You, annoyed, until they turn it down.
    Buddy you;
    you.colour = colours::cyan;
    you.size = 72;
    you.mood = s.duck > 0.4f ? Buddy::happy : Buddy::annoyed;
    you.armUp = s.eventAge < 0.8f;
    you.squash = s.bass * 0.3f;
    you.look = { -0.9f, -0.4f };
    drawBuddy (g, { 490, 292 }, you);

    // One burst per knuckle, popping in time with the knocks.
    const Point<float> knockSpots[] = { { 344, 196 }, { 408, 150 }, { 452, 206 } };
    for (int i = 0; i < 3; ++i)
        if (s.eventAge >= 0.17f * (float) i)
            comicBurst (g, knockSpots[i], 32, "KNOCK!", colours::yellow, (s.eventAge - 0.17f * (float) i) * 1.1f, 5 + i);
    speechBubble (g, { 300, 24, 170, 46 }, { 360, 92 }, "OK OK, SORRY!!", jlimit (0.0f, 1.0f, (s.duck - 0.2f) * 2.5f));
}

//==============================================================================
// Visitors: walking in through a door, hanging about, walking out again.
Colour visitorColour (int index)
{
    // Built on first use: a namespace-scope copy of the palette can be initialised before the palette is.
    static const Colour palette[] = { colours::orange, colours::pink, colours::cyan, colours::yellow, colours::red, colours::blue };
    return palette[(size_t) std::abs (index) % 6];
}

struct Placement
{
    Point<float> feet;
    float alpha;
    bool walking;
};

Placement placeVisitor (const State::Person& p, Point<float> door, Point<float> spot)
{
    const float u = p.progress;
    if (p.state == 1)   // entering: appear in the doorway, then walk over
    {
        if (u < 0.3f) return { door, smoothstep (u / 0.3f), false };
        return { door + (spot - door) * smoothstep ((u - 0.3f) / 0.7f), 1.0f, u < 0.97f };
    }
    if (p.state == 3)   // leaving: walk to the door, then vanish through it
    {
        if (u < 0.55f) return { spot + (door - spot) * smoothstep (u / 0.55f), 1.0f, true };
        return { door, jlimit (0.0f, 1.0f, (0.9f - u) / 0.35f), false };
    }
    return { spot, 1.0f, false };
}

bool isQueasy (const State& s, const State::Person& p)
{
    return p.state == 2 && p.spot == s.pukeSpot && s.pukeAge < 3.0f;
}

void pukePuddle (Graphics& g, const State& s, Point<float> at)
{
    if (s.pukeSpot < 0 || s.pukeAge > 40.0f)
        return;
    const float alpha = jmin (1.0f, (40.0f - s.pukeAge) / 5.0f) * jmin (1.0f, jmax (0.0f, s.pukeAge - 1.3f) * 4.0f);
    if (alpha <= 0.01f)
        return;
    Graphics::ScopedSaveState save (g);
    g.beginTransparencyLayer (alpha);
    auto p = splat (at, 24, 77, 9);
    p.applyTransform (AffineTransform::scale (1.0f, 0.45f, at.x, at.y));
    shape (g, p, Colour (0xffb5d82a), 3.0f, {});
    g.setColour (Colour (0xffe8f56a));
    g.fillEllipse (at.x - 8, at.y - 4, 7, 4);
    g.fillEllipse (at.x + 6, at.y - 1, 5, 3);
    g.endTransparencyLayer();
}

//==============================================================================
void drawToilets (Graphics& g, const State& s)
{
    g.fillAll (Colour (0xff7fe3d0));
    g.setColour (colours::ink.withAlpha (0.16f));
    for (float x = 0; x < width; x += 28) g.drawLine (x, 0, x, 250, 2.0f);
    for (float y = 0; y < 250; y += 28) g.drawLine (0, y, width, y, 2.0f);

    // Checkerboard floor.
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 21; ++col)
        {
            g.setColour (((row + col) % 2 == 0) ? colours::ink : colours::cream);
            g.fillRect ((float) col * 28.0f - (float) row * 6.0f, 250.0f + (float) row * 21.0f, 28.0f, 21.0f);
        }
    g.setColour (colours::ink);
    g.drawLine (0, 250, width, 250, 4.0f);

    // Mirror + sink.
    shape (g, blob ({ 22, 34, 108, 88 }, 0.02f, 9, 12), Colour (0xffb8e6ff), 4.0f);
    g.setColour (Colours::white.withAlpha (0.7f));
    g.drawLine (48, 58, 68, 46, 5.0f);
    g.drawLine (54, 74, 84, 54, 4.0f);
    shape (g, wobblyRect ({ 64, 178, 26, 72 }, 4, 1, 3), colours::cream, 3.5f);
    shape (g, wobblyRect ({ 26, 150, 102, 32 }, 14, 1.5f, 4), colours::cream, 4.0f);
    shape (g, wobblyRect ({ 72, 136, 12, 18 }, 3, 0.5f, 6), Colour (0xffc0c8d8), 3.0f);

    text (g, "BASS 4 EVER <3", fonts().display (22), { 168, 34, 180, 40 }, Justification::centred,
          colours::pink, 0, {}, -0.1f);
    text (g, "DJ WAS HERE", fonts().body (13), { 120, 210, 80, 24 }, Justification::centred,
          colours::purple, 0, {}, 0.15f);

    // Three urinals with their pipes, and little modesty panels between them.
    for (int i = 0; i < 3; ++i)
    {
        const float x = 205.0f + 50.0f * (float) i;
        shape (g, rect ({ x - 3, 98, 6, 52 }), Colour (0xffc0c8d8), 2.5f, {});
        shape (g, blob ({ x - 8, 90, 16, 12 }, 0.02f, 3 + i, 8), Colour (0xffe1e6ef), 2.5f, {});
        shape (g, wobblyRect ({ x - 17, 146, 34, 64 }, 14, 1.0f, 30 + i), colours::cream, 3.5f);
        g.setColour (Colour (0xff9fd8e8));
        g.fillEllipse (x - 10, 160, 20, 36);
        g.setColour (colours::ink.withAlpha (0.5f));
        g.fillEllipse (x - 3, 192, 6, 4);
        if (i < 2)
            shape (g, wobblyRect ({ x + 22, 138, 6, 84 }, 3, 0.5f, 40 + i), Colour (0xff8892a6), 2.5f, {});
    }

    const Rectangle<float> frame (352, 48, 120, 202);
    lightSpill (g, frame, height, s.openness, colours::yellow);
    shape (g, wobblyRect ({ 379, 18, 66, 24 }, 5, 1, 7), colours::slime, 3.0f);
    text (g, "EXIT", fonts().display (18), { 379, 20, 66, 24 }, Justification::centred, Colours::white, 1.5f, { 1, 1.5f });
    doorway (g, frame, s.openness, colours::blue, s, nullptr);

    noteStream (g, { 412, 150 }, MathConstants<float>::pi, 280, 0.7f, 10, s.time, 0.3f,
                0.15f + 0.85f * s.openness, 18 + 6 * s.openness, 21);

    const Point<float> door (412, 262);
    const Point<float> spots[] = { { 205, 268 }, { 255, 268 }, { 305, 268 }, { 78, 272 } };
    if (s.pukeSpot >= 0)
        pukePuddle (g, s, spots[jlimit (0, 3, s.pukeSpot)] + Point<float> (26, 18));

    for (int k = 0; k < (int) s.people.size(); ++k)
    {
        const auto& p = s.people[(size_t) k];
        if (p.state == 0)
            continue;
        const auto place = placeVisitor (p, door, spots[jlimit (0, 3, p.spot)]);
        Buddy b;
        b.colour = visitorColour (p.colour);
        b.size = 50;
        b.alpha = place.alpha;
        b.queasy = isQueasy (s, p);
        b.back = p.state == 2 && ! b.queasy;   // facing the urinal / the mirror
        b.mood = Buddy::happy;
        b.look = { place.walking ? (p.state == 1 ? -1.0f : 1.0f) : 0.0f, 0 };
        const float bob = place.walking ? std::abs (std::sin (s.time * 9.0f + (float) k)) * 4.0f : 0.0f;
        drawBuddy (g, place.feet - Point<float> (0, bob), b);
    }

    Buddy you;
    you.colour = colours::slime;
    you.size = 54;
    you.mood = s.openness > 0.5f ? Buddy::happy : Buddy::chill;
    you.squash = s.bass * (0.15f + 0.4f * s.openness);
    you.look = { -0.9f, 0.0f };
    drawBuddy (g, { 520, 296 }, you);

    comicBurst (g, { 420, 120 }, 36, "SLAM!", colours::yellow, s.eventAge, 9);
    if (s.pukeSpot >= 0)
        comicBurst (g, spots[jlimit (0, 3, s.pukeSpot)] + Point<float> (0, -88), 32, "BLEGH!", colours::slime, s.pukeAge * 0.4f, 23);
}

//==============================================================================
void drawSmokingArea (Graphics& g, const State& s)
{
    g.setGradientFill (ColourGradient (Colour (0xff1b1464), 0, 0, Colour (0xff5b2a86), 0, 250, false));
    g.fillAll();

    Random rng (5);
    for (int i = 0; i < 34; ++i)
    {
        const float x = rng.nextFloat() * width, y = rng.nextFloat() * 200;
        const float tw = 0.5f + 0.5f * std::sin (s.time * (1.5f + rng.nextFloat() * 3) + (float) i);
        g.setColour (colours::cream.withAlpha (0.3f + 0.7f * tw));
        const float r = 1.0f + rng.nextFloat() * 1.8f;
        g.fillEllipse (x - r, y - r, r * 2, r * 2);
    }
    {
        // Crescent: a yellow disc with a sky-coloured bite taken out.
        Path moon;
        moon.addEllipse (480, 26, 52, 52);
        shape (g, moon, colours::yellow, 3.5f, {});
        g.setColour (Colour (0xff1f1668));
        g.fillEllipse (496, 16, 52, 52);
    }

    // The club wall.
    const Rectangle<float> wall (-10, 40, 400, 216);
    shape (g, rect (wall), Colour (0xffd9452b), 4.0f, {});
    g.setColour (colours::ink.withAlpha (0.28f));
    for (int row = 0; row < 14; ++row)
    {
        const float y = wall.getY() + (float) row * 16.0f;
        g.drawLine (wall.getX(), y, wall.getRight(), y, 2.0f);
        for (float x = wall.getX() + (row % 2 == 0 ? 0.0f : 20.0f); x < wall.getRight(); x += 40)
            g.drawLine (x, y, x, y + 16, 2.0f);
    }

    // Neon sign, flickering.
    const bool flickerOff = std::fmod (s.time, 7.3f) > 7.0f && std::fmod (s.time * 20, 2.0f) > 1.0f;
    const float neon = flickerOff ? 0.25f : 0.85f + 0.15f * std::sin (s.time * 13.0f);
    const Rectangle<float> sign (160, 62, 124, 44);
    g.setColour (colours::pink.withAlpha (0.25f * neon));
    g.fillRoundedRectangle (sign.expanded (12), 20);
    shape (g, wobblyRect (sign, 10, 1, 17), Colour (0xff2a0f4d), 3.5f);
    text (g, "CLUB", fonts().display (32), sign.translated (0, 3), Justification::centred,
          colours::pink.brighter (0.4f).withAlpha (neon), 0, {});

    // Pavement.
    g.setColour (Colour (0xff3a3550));
    g.fillRect (0.0f, 254.0f, width, height - 254.0f);
    g.setColour (colours::ink);
    g.drawLine (0, 254, width, 254, 4.0f);

    const Rectangle<float> frame (176, 122, 92, 132);
    lightSpill (g, frame, height, s.openness, colours::yellow);
    doorway (g, frame, s.openness, Colour (0xff8892a6), s, [] (Graphics& gg, const Path& leaf)
    {
        // Push bar across the leaf.
        auto b = leaf.getBounds();
        gg.setColour (colours::ink);
        gg.drawLine (b.getX() + 6, b.getCentreY() + 8, b.getRight() - 6, b.getCentreY() + 8, 5.0f);
    });

    noteStream (g, { 222, 186 }, -0.35f, 300, 0.9f, 11, s.time, 0.32f, 0.08f + 0.92f * s.openness, 20, 31);

    const Point<float> door (222, 296);
    // Four places to stand and smoke, and one by the bin for emergencies.
    const Point<float> spots[] = { { 330, 300 }, { 396, 300 }, { 462, 300 }, { 524, 300 }, { 76, 300 } };

    // The bin.
    shape (g, wobblyRect ({ -6, 236, 46, 66 }, 6, 1.5f, 51), Colour (0xff2fa36b), 4.0f);
    shape (g, wobblyRect ({ -10, 228, 54, 12 }, 4, 1, 52), Colour (0xff1f7a4f), 3.5f, {});
    if (s.pukeSpot >= 0)
        pukePuddle (g, s, spots[jlimit (0, 4, s.pukeSpot)] + (s.pukeSpot == 4 ? Point<float> (-34, 6) : Point<float> (-30, 4)));

    // You, leaning on the wall.
    Buddy you;
    you.colour = colours::slime;
    you.size = 56;
    you.mood = s.openness > 0.5f ? Buddy::happy : Buddy::chill;
    you.squash = s.bass * 0.25f * s.openness;
    you.look = { 0.9f, 0.0f };
    drawBuddy (g, { 140, 300 }, you);

    // Somebody's about to be sick (or just was): everyone else stops and stares.
    int sickOne = -1;
    for (int k = 0; k < (int) s.people.size(); ++k)
        if (s.people[(size_t) k].state != 0 && s.people[(size_t) k].sick)
            sickOne = k;
    const bool aftermath = s.pukeSpot == 4 && s.pukeAge < 6.0f;
    const bool brewing = sickOne >= 0 && ! s.people[(size_t) sickOne].puked;
    static const char* reactions[] = { "EWWWW!", "MATE...", "not on my shoes!!", "who gave him tequila?", "that's Dave",
                                       "every. single. time.", "LEGEND", "I'm gonna be sick too", "and he's going back in?!",
                                       "classic Friday", "I can't unsee that", "bless him" };
    int watcher = 0;

    for (int k = 0; k < (int) s.people.size(); ++k)
    {
        const auto& p = s.people[(size_t) k];
        if (p.state == 0)
            continue;
        const auto place = placeVisitor (p, door, spots[jlimit (0, 4, p.spot)]);
        // The sick one staggers; everyone else just walks.
        const float sway = p.sick && ! p.puked ? std::sin (s.time * 5.0f + (float) k) * 5.0f : 0.0f;
        const float bob = place.walking ? std::abs (std::sin (s.time * (p.sick ? 6.0f : 9.0f) + (float) k)) * 4.0f : 0.0f;
        const auto feet = place.feet + Point<float> (sway, -bob);

        Buddy b;
        b.colour = visitorColour (p.colour);
        b.size = 52;
        b.alpha = place.alpha;

        if (p.sick)
        {
            // Green on the way out; bent over the bin; sheepish on the way back in.
            const bool heaving = p.puked && s.pukeAge < 1.6f;
            b.queasy = ! p.puked || heaving;
            b.mood = p.puked && ! heaving ? Buddy::chill : Buddy::shocked;
            b.look = { place.walking ? (p.state == 1 ? -1.0f : 1.0f) : -1.0f, heaving ? 0.8f : 0.2f };
            {
                Graphics::ScopedSaveState lean (g);
                if (heaving)
                    g.addTransform (AffineTransform::rotation (-0.5f * smoothstep (s.pukeAge / 0.2f), feet.x, feet.y));
                drawBuddy (g, feet, b);
            }
            if (heaving && s.pukeAge < 1.2f)
            {
                // The cartoon stream: out of the (leaning) mouth, down to the floor by the bin.
                const Point<float> mouth (feet.x - 13, feet.y - 19), floor (feet.x - 46, feet.y);
                Path stream;
                stream.startNewSubPath (mouth);
                stream.quadraticTo (mouth.x - 30, mouth.y + 2, floor.x, floor.y);
                const float w = 11.0f * jmin (1.0f, s.pukeAge / 0.1f) * jmin (1.0f, (1.2f - s.pukeAge) / 0.3f);
                g.setColour (colours::ink);
                g.strokePath (stream, PathStrokeType (w + 5, PathStrokeType::curved, PathStrokeType::rounded));
                g.setColour (Colour (0xffb5d82a));
                g.strokePath (stream, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
            }
            continue;
        }

        const bool staring = (aftermath || brewing) && p.state == 2;
        b.mood = staring ? Buddy::shocked : (p.laughing || p.talking ? Buddy::happy : Buddy::chill);
        b.look = { place.walking ? (p.state == 1 ? 1.0f : -1.0f) : -0.5f, 0.1f };
        if (staring)
            b.look = { -1.0f, 0.0f };
        drawBuddy (g, feet, b);

        if (p.state != 2)
            continue;

        // Reactions take turns, so the bubbles don't pile up.
        if (staring)
        {
            const int order = watcher++;
            String line;
            if (brewing && order == 0 && sickOne >= 0 && s.people[(size_t) sickOne].state == 2)
                line = "uh oh...";
            else if (aftermath)
            {
                const float start = 0.4f + 1.1f * (float) order;
                if (s.pukeAge > start && s.pukeAge < start + 1.8f)
                    line = reactions[(size_t) (order + s.pukeCount * 3) % std::size (reactions)];
            }
            if (line.isNotEmpty())
            {
                const Rectangle<float> r (jlimit (6.0f, width - 136.0f, feet.x - 64), feet.y - 134, 130, 46);
                speechBubble (g, r, { feet.x - 6, feet.y - 66 }, line, 1.0f, 14.0f);
            }
        }

        // Cigarette and a lazy trail of smoke.
        const Point<float> cig (feet.x - 34, feet.y - 30);
        g.setColour (colours::cream);
        g.fillRoundedRectangle (cig.x, cig.y, 16, 4.5f, 2);
        g.setColour (colours::orange);
        g.fillEllipse (cig.x - 3, cig.y, 5, 5);
        for (int i = 0; i < 4; ++i)
        {
            const float ph = frac (s.time * 0.25f + (float) i / 4.0f + (float) k * 0.31f);
            const float r = 4 + ph * 14;
            g.setColour (colours::cream.withAlpha ((1 - ph) * 0.4f));
            g.fillEllipse (cig.x - ph * 22 + std::sin (s.time + (float) (i + k)) * 5 - r, cig.y - 6 - ph * 110 - r, r * 2, r * 2);
        }

        if ((p.talking || p.laughing) && ! staring)
        {
            static const char* laughs[] = { "HA HA HA!", "LMAOOO", "STOPPP", "HAHA NO" };
            const String words = p.laughing ? String (laughs[(k + p.colour) % 4]) : p.words;
            const Rectangle<float> r (jlimit (6.0f, width - 136.0f, feet.x - 64), feet.y - 134, 130, 46);
            if (words.isNotEmpty())
                speechBubble (g, r, { feet.x - 6, feet.y - 66 }, words, 1.0f, 14.0f);
        }
    }

    comicBurst (g, { 222, 104 }, 34, "SLAM!", colours::yellow, s.eventAge, 13);
    if (s.pukeSpot >= 0)
        comicBurst (g, spots[jlimit (0, 4, s.pukeSpot)] + Point<float> (s.pukeSpot == 4 ? 10.0f : 0.0f, -100), 32, "BLEGH!", colours::slime, s.pukeAge * 0.4f, 29);
}

//==============================================================================
constexpr float parkedScale = 0.66f;

void drawParkedCar (Graphics& g, const State& s)
{
    g.setGradientFill (ColourGradient (Colour (0xff4fd1ff), 0, 0, Colour (0xffb8f0ff), 0, 220, false));
    g.fillAll();

    // Sun with spinning rays.
    {
        const Point<float> c (64, 52);
        g.setColour (colours::yellow.withAlpha (0.8f));
        for (int i = 0; i < 10; ++i)
        {
            const float a = s.time * 0.3f + twoPi * (float) i / 10.0f;
            g.drawLine (c.x + std::cos (a) * 36, c.y + std::sin (a) * 36, c.x + std::cos (a) * 52, c.y + std::sin (a) * 52, 5.0f);
        }
        shape (g, blob ({ c.x - 28, c.y - 28, 56, 56 }, 0.03f, 1, 10), colours::yellow, 3.5f, {});
    }

    // Town.
    struct Block { float x, w, h; Colour c; };
    const Block blocks[] = { { 120, 70, 120, colours::pink }, { 196, 54, 160, colours::purple },
                             { 256, 90, 100, colours::orange }, { 352, 60, 140, colours::teal },
                             { 418, 80, 110, colours::yellow }, { 504, 70, 150, colours::pink } };
    for (auto& b : blocks)
    {
        const Rectangle<float> r (b.x, 226 - b.h, b.w, b.h);
        shape (g, wobblyRect (r, 4, 1.5f, (int) b.x), b.c, 3.5f, {});
        g.setColour (colours::cream.withAlpha (0.8f));
        for (float y = r.getY() + 12; y < r.getBottom() - 16; y += 24)
            for (float x = r.getX() + 10; x < r.getRight() - 14; x += 20)
                g.fillRect (x, y, 9.0f, 12.0f);
    }

    // Pavement and road.
    g.setColour (Colour (0xffc9c3d9));
    g.fillRect (0.0f, 224.0f, width, 22.0f);
    g.setColour (Colour (0xff3a3550));
    g.fillRect (0.0f, 246.0f, width, height - 246.0f);
    g.setColour (colours::ink);
    g.drawLine (0, 224, width, 224, 3.5f);
    g.drawLine (0, 246, width, 246, 3.5f);
    g.setColour (colours::yellow);
    for (float x = 10; x < width; x += 60)
        g.fillRoundedRectangle (x, 296, 34, 6, 3);

    const float o = s.openness;
    const float bounce = s.bass * 6.0f;
    const float jitter = o * s.vibes * s.bass * 2.5f * std::sin (s.time * 70.0f);
    const float squash = s.bass * 0.04f;

    // Parked at the kerb, further from the camera than the traffic going past.
    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::scale (parkedScale, parkedScale, 270, 268)
                        .translated (jitter, -bounce * 0.5f)
                        .scaled (1.0f + squash, 1.0f - squash, 270, 262));

    // Bass rings when you're outside listening.
    for (int k = 0; k < 3; ++k)
    {
        const float ph = frac (s.time * 1.2f + (float) k / 3.0f);
        const float r = 150 + ph * 80;
        g.setColour (colours::pink.withAlpha ((1 - ph) * 0.5f * o * (0.2f + s.bass)));
        g.drawEllipse (270 - r, 205 - r * 0.45f, r * 2, r * 0.9f, 4.0f);
    }

    Path cabin;
    cabin.startNewSubPath (170, 190); cabin.lineTo (214, 142); cabin.lineTo (338, 142); cabin.lineTo (384, 190);
    cabin.closeSubPath();
    shape (g, cabin, colours::red, 4.0f);

    Path windows;
    windows.startNewSubPath (190, 186); windows.lineTo (222, 152); windows.lineTo (270, 152); windows.lineTo (270, 186);
    windows.closeSubPath();
    windows.startNewSubPath (282, 186); windows.lineTo (282, 152); windows.lineTo (332, 152); windows.lineTo (362, 186);
    windows.closeSubPath();
    shape (g, windows, Colour (0xffaee9ff), 3.0f, {});

    // Your head bobbing behind the windscreen.
    if (o < 0.99f)
    {
        Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (windows);
        Buddy you; you.colour = colours::slime; you.size = 40; you.mood = Buddy::happy;
        you.alpha = 1.0f - o; you.look = { 0.6f, 0 };
        drawBuddy (g, { 310, 204 + std::sin (s.time * 8.0f) * 2.0f * (0.3f + s.bass) - s.bass * 6.0f }, you);
    }

    shape (g, wobblyRect ({ 124, 184, 292, 58 }, 22, 1.5f, 77), colours::red, 4.0f);
    g.setColour (Colours::white.withAlpha (0.3f));
    g.fillRoundedRectangle (150, 194, 200, 8, 4);
    shape (g, blob ({ 394, 198, 22, 16 }, 0.02f, 2, 8), colours::yellow, 3.0f, {});
    shape (g, wobblyRect ({ 118, 214, 22, 14 }, 4, 0.5f, 5), colours::orange, 3.0f, {});

    for (float wx : { 194.0f, 350.0f })
    {
        Path wheel;
        wheel.addEllipse (wx - 26, 216 + bounce * 0.5f, 52, 52);
        shape (g, wheel, colours::ink, 0, { 3.0f, 3.0f });
        g.setColour (Colour (0xffc0c8d8));
        g.fillEllipse (wx - 11, 231 + bounce * 0.5f, 22, 22);
    }

    noteStream (g, { 300, 150 }, -MathConstants<float>::halfPi, 140, 1.1f, 7, s.time, 0.4f,
                (0.3f + 0.6f * s.bass) * (1.0f - 0.6f * o), 18, 41);

    if (o > 0.5f && s.bass > 0.5f)
        text (g, "BOOM", fonts().display (40 + 20 * s.bass), { 190, 70, 160, 60 }, Justification::centred,
              colours::yellow.withAlpha (jmin (1.0f, (o - 0.5f) * 4.0f)), 3.0f, { 3, 4 }, -0.1f);

    // You, outside on the pavement.
    if (o > 0.01f)
    {
        Buddy you; you.colour = colours::slime; you.size = 60; you.alpha = o;
        you.mood = s.bass > 0.6f ? Buddy::shocked : Buddy::happy; you.look = { -0.9f, -0.2f };
        drawBuddy (g, { 545 - jitter, 262 + bounce * 0.5f }, you);
    }
}

// A little car passing by on the street.
void drawMiniCar (Graphics& g, float cx, float baseline, float scale, int dir, Colour c)
{
    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::scale ((float) dir * scale, scale).translated (cx, baseline));
    Path cabin;
    cabin.startNewSubPath (-42, -46); cabin.lineTo (-22, -72); cabin.lineTo (30, -72); cabin.lineTo (50, -46);
    cabin.closeSubPath();
    shape (g, cabin, c, 4.0f, {});
    Path glass;
    glass.startNewSubPath (-32, -48); glass.lineTo (-18, -66); glass.lineTo (2, -66); glass.lineTo (2, -48); glass.closeSubPath();
    glass.startNewSubPath (10, -48); glass.lineTo (10, -66); glass.lineTo (26, -66); glass.lineTo (40, -48); glass.closeSubPath();
    shape (g, glass, Colour (0xffaee9ff), 3.0f, {});
    shape (g, wobblyRect ({ -72, -50, 144, 36 }, 14, 1.0f, c.getARGB() & 0xff), c, 4.0f, { 3, 4 });
    shape (g, blob ({ 58, -42, 14, 10 }, 0.02f, 2, 8), colours::yellow, 2.5f, {});
    for (float wx : { -42.0f, 44.0f })
    {
        Path wheel;
        wheel.addEllipse (wx - 15, -28, 30, 30);
        shape (g, wheel, colours::ink, 0, {});
        g.setColour (Colour (0xffc0c8d8));
        g.fillEllipse (wx - 6, -19, 12, 12);
    }
}

void drawCar (Graphics& g, const State& s)
{
    drawParkedCar (g, s);

    // Traffic between us and the camera: the lane next to us heads right, the far lane
    // (nearest the camera, so biggest) heads left. Same perspective as our car: a car is
    // about 4.4 m long, and pixels-per-metre grows as the lane gets nearer the camera.
    static const Colour carColours[] = { colours::yellow, colours::blue, colours::slime, colours::orange, colours::purple, colours::cyan };
    const float ourLength = 292.0f * parkedScale, miniLength = 144.0f;
    for (int pass = 0; pass < 2; ++pass)
    {
        const int dir = pass == 0 ? 1 : -1;
        const float depth = pass == 0 ? 1.15f : 1.35f;
        const float scale = ourLength * depth / miniLength;
        const float pxPerMetre = ourLength * depth / 4.4f;
        const float baseline = pass == 0 ? 290.0f : 316.0f;
        for (const auto& v : s.vehicles)
        {
            if (! v.active || v.dir != dir)
                continue;
            const float x = 270.0f + v.x * pxPerMetre;
            if (x < -90.0f * scale || x > width + 90.0f * scale)
                continue;
            const float bump = std::abs (std::sin (s.time * 14.0f + v.x)) * 1.5f;
            drawMiniCar (g, x, baseline - bump, scale, dir, carColours[v.colour % 6]);
            if (v.honking)
                text (g, "HONK!", fonts().display (30), { x - 60, baseline - 72 * scale - 40, 120, 36 },
                      Justification::centred, colours::yellow, 3.0f, { 2, 2.5f }, std::sin (s.time * 30.0f) * 0.1f);
        }
    }
}

//==============================================================================
// Life, one buzz at a time. Read in order, it's a story.
struct Message { const char* from; const char* text; Colour (*icon)(); };

Colour iconPink()   { return colours::pink; }
Colour iconCyan()   { return colours::cyan; }
Colour iconSlime()  { return colours::slime; }
Colour iconYellow() { return colours::yellow; }
Colour iconOrange() { return colours::orange; }
Colour iconRed()    { return colours::red; }
Colour iconBlue()   { return colours::blue; }

const Message messages[] = {
    { "Mum",          "did you eat today?",                     iconSlime },
    { "Babe <3",      "dinner at 8? x",                         iconPink },
    { "Reminders",    "don't forget the groceries!",            iconYellow },
    { "Bank",         "Your balance is looking a bit sad",      iconBlue },
    { "Gym",          "We miss you! It's been 94 days",         iconOrange },
    { "Jessica",      "last night was fun ;)",                  iconRed },
    { "Babe <3",      "who is Jessica?",                        iconPink },
    { "Jessica",      "is she gone yet?",                       iconRed },
    { "Babe <3",      "?????",                                  iconPink },
    { "Dad",          "how do I make the text bigger",          iconSlime },
    { "Landlord",     "rent was due yesterday",                 iconBlue },
    { "Reminders",    "milk, eggs, a good excuse",              iconYellow },
    { "The Lads",     "one pint?",                              iconOrange },
    { "The Lads",     "Dave is crying in a kebab shop",         iconOrange },
    { "Dentist",      "see you tomorrow at 8am!",               iconCyan },
    { "Mum",          "why is Babe calling me?",                iconSlime },
    { "Pizza Place",  "your pizza is 2 min away",               iconRed },
    { "Pizza Place",  "your pizza is now 40 min away",          iconRed },
    { "Calendar",     "Mum's birthday. TODAY.",                 iconCyan },
    { "Screen Time",  "you averaged 9h a day last week",        iconBlue },
    { "Babe <3",      "we need to talk",                        iconPink },
    { "Dad",          "HI",                                     iconSlime },
    { "Jessica",      "my husband's back early, don't text",    iconRed },
    { "Weather",      "rain. obviously.",                       iconCyan },
};

void phoneNotification (Graphics& g, const State& s)
{
    if (s.notification < 0 || s.eventAge > 4.0f)
        return;
    const auto& m = messages[(size_t) s.notification % std::size (messages)];

    // Slides down from the top like a real banner, sits there, then fades.
    const float slide = smoothstep (s.eventAge / 0.25f);
    const float alpha = jmin (1.0f, (4.0f - s.eventAge) * 2.0f);
    const Rectangle<float> card (110, -70 + 84 * slide, 320, 66);

    Graphics::ScopedSaveState save (g);
    g.beginTransparencyLayer (alpha);
    shape (g, wobblyRect (card, 16, 1, 21), colours::cream, 4.0f, { 3, 4 });
    Path icon;
    icon.addRoundedRectangle (card.getX() + 12, card.getY() + 13, 40, 40, 11);
    shape (g, icon, m.icon(), 3.0f, {});
    text (g, String (m.from).substring (0, 1).toUpperCase(), fonts().display (24), { card.getX() + 12, card.getY() + 16, 40, 40 },
          Justification::centred, Colours::white, 2.0f, {});
    text (g, m.from, fonts().display (17), { card.getX() + 62, card.getY() + 8, 190, 22 }, Justification::centredLeft,
          colours::ink, 0, {});
    text (g, "now", fonts().body (12), { card.getRight() - 52, card.getY() + 8, 40, 22 }, Justification::centredRight,
          colours::ink.withAlpha (0.5f), 0, {});
    text (g, m.text, fonts().body (15), { card.getX() + 62, card.getY() + 30, card.getWidth() - 72, 30 }, Justification::centredLeft,
          colours::ink, 0, {});
    g.endTransparencyLayer();
}

//==============================================================================
void drawPhone (Graphics& g, const State& s)
{
    g.fillAll (Colour (0xffffb84d));
    // Backsplash tiles.
    g.setColour (colours::cream);
    g.fillRect (0.0f, 118.0f, width, 52.0f);
    g.setColour (colours::ink.withAlpha (0.2f));
    for (float x = 0; x < width; x += 26) g.drawLine (x, 118, x, 170, 2.0f);
    g.drawLine (0, 144, width, 144, 2.0f);

    // Window.
    shape (g, wobblyRect ({ 380, 16, 140, 92 }, 6, 1.5f, 2), Colour (0xff7ad7ff), 4.0f);
    g.setColour (colours::cream);
    g.drawLine (450, 18, 450, 106, 5.0f);
    g.drawLine (382, 62, 518, 62, 5.0f);
    shape (g, blob ({ 396, 30, 40, 18 }, 0.1f, 3, 8), colours::cream, 2.0f, {});

    // Table, in a bit of perspective.
    Path table;
    table.startNewSubPath (40, 166); table.lineTo (520, 166); table.lineTo (590, 320); table.lineTo (-30, 320);
    table.closeSubPath();
    shape (g, table, Colour (0xffc77a3a), 4.0f, {});
    g.setColour (colours::ink.withAlpha (0.25f));
    for (int i = 1; i < 5; ++i)
    {
        const float t = (float) i / 5.0f;
        g.drawLine (40 + 480 * t, 166, -30 + 620 * t, 320, 2.0f);
    }

    // Mug with steam.
    shape (g, wobblyRect ({ 76, 184, 46, 52 }, 8, 1, 4), colours::cyan, 3.5f);
    Path handle;
    handle.addCentredArc (124, 210, 12, 13, 0, 0, MathConstants<float>::pi, true);
    g.setColour (colours::ink);
    g.strokePath (handle, PathStrokeType (9.0f));
    g.setColour (colours::cyan);
    g.strokePath (handle, PathStrokeType (4.0f));
    for (int i = 0; i < 2; ++i)
    {
        Path steam;
        const float x = 92 + (float) i * 14, ph = s.time * 2.0f + (float) i;
        steam.startNewSubPath (x, 178);
        steam.cubicTo (x - 8 + std::sin (ph) * 3, 162, x + 8, 150, x + std::sin (ph) * 4, 134);
        g.setColour (colours::cream.withAlpha (0.8f));
        g.strokePath (steam, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // The phone, buzzing on the table.
    const float buzz = s.level * 1.8f * std::sin (s.time * 90.0f) * (1.0f - s.openness);
    {
        Graphics::ScopedSaveState save (g);
        g.addTransform (AffineTransform::rotation (-0.1f, 290, 232).translated (buzz, 0));
        const Rectangle<float> body (215, 196, 152, 74);
        shape (g, wobblyRect (body, 14, 1, 19), Colour (0xff22202e), 4.0f);
        const auto screen = body.reduced (8, 7).withTrimmedRight (14);
        g.setGradientFill (ColourGradient (colours::pink, screen.getX(), screen.getY(),
                                           colours::purple, screen.getRight(), screen.getBottom(), false));
        g.fillRoundedRectangle (screen, 6);

        // Equaliser bars.
        static const Colour bars[] = { colours::yellow, colours::cyan, colours::slime };
        for (int i = 0; i < 9; ++i)
        {
            const float n = 0.5f + 0.5f * std::sin (s.time * (7.0f + (float) i * 1.7f) + (float) i * 2.3f);
            const float h = (screen.getHeight() - 12) * jlimit (0.08f, 1.0f, (0.25f + 0.75f * n) * (0.2f + s.level * 1.3f));
            const float bw = (screen.getWidth() - 16) / 9.0f;
            g.setColour (bars[i % 3]);
            g.fillRoundedRectangle (screen.getX() + 8 + (float) i * bw, screen.getBottom() - 6 - h, bw - 3, h, 2);
        }
        g.setColour (Colour (0xff55526a));
        for (int i = 0; i < 3; ++i)
            g.fillEllipse (body.getRight() - 14, body.getCentreY() - 12 + (float) i * 9, 5, 5);
    }

    noteStream (g, { 360, 222 }, -1.25f, 150, 0.8f, 9, s.time, 0.55f,
                (0.3f + 0.7f * s.level) * (1.0f - 0.75f * s.openness), 13, 51);

    // Pocket slides up over the phone.
    if (s.openness > 0.01f)
    {
        const float top = jmap (s.openness, 330.0f, 186.0f);

        // The phone, stood up and peeking out of the pocket.
        const Rectangle<float> peek (262, top - 50, 58, 110);
        shape (g, wobblyRect (peek, 10, 1, 29), Colour (0xff22202e), 4.0f);
        g.setColour (colours::purple.withAlpha (0.5f + 0.5f * s.level));
        g.fillRoundedRectangle (peek.reduced (7, 9), 5);

        Path pocket;
        pocket.startNewSubPath (150, top);
        pocket.lineTo (430, top);
        pocket.quadraticTo (440, top + 150, 290, top + 200);
        pocket.quadraticTo (140, top + 150, 150, top);
        pocket.closeSubPath();
        shape (g, pocket, Colour (0xff3b5bdb), 4.0f);
        Path stitch;
        stitch.startNewSubPath (164, top + 14);
        stitch.lineTo (416, top + 14);
        stitch.quadraticTo (422, top + 140, 290, top + 184);
        stitch.quadraticTo (158, top + 140, 164, top + 14);
        Path dashed;
        const float dashes[] = { 8.0f, 6.0f };
        PathStrokeType (2.5f).createDashedStroke (dashed, stitch, dashes, 2);
        g.setColour (colours::yellow.withAlpha (0.9f));
        g.fillPath (dashed);
        for (float rx : { 160.0f, 420.0f })
        {
            g.setColour (colours::orange);
            g.fillEllipse (rx - 6, top - 2, 12, 12);
        }
    }

    phoneNotification (g, s);
}

//==============================================================================
void drawFestival (Graphics& g, const State& s)
{
    ColourGradient sky (colours::purple, 0, 0, Colour (0xffff8a3d), 0, 200, false);
    sky.addColour (0.55, colours::pink);
    g.setGradientFill (sky);
    g.fillAll();

    // Sinking sun.
    g.setColour (colours::yellow);
    g.fillEllipse (380, 120, 104, 104);

    // Stage light beams sweeping the sky.
    static const Colour beams[] = { colours::cyan, colours::yellow, colours::slime, colours::cream };
    for (int i = 0; i < 4; ++i)
    {
        const float a = -MathConstants<float>::halfPi + std::sin (s.time * 0.7f + (float) i * 1.7f) * 0.6f;
        Path beam;
        beam.startNewSubPath (305, 122);
        beam.lineTo (305 + std::cos (a - 0.04f) * 260, 122 + std::sin (a - 0.04f) * 260);
        beam.lineTo (305 + std::cos (a + 0.04f) * 260, 122 + std::sin (a + 0.04f) * 260);
        beam.closeSubPath();
        g.setColour (beams[i].withAlpha (0.12f + 0.4f * s.level));
        g.fillPath (beam);
    }

    auto hill = [&] (float baseY, float peakX, float peakY, Colour c)
    {
        Path p;
        p.startNewSubPath (-40, height + 10);
        p.lineTo (-40, baseY);
        p.quadraticTo (peakX, peakY, width + 40, baseY);
        p.lineTo (width + 40, height + 10);
        p.closeSubPath();
        shape (g, p, c, 4.0f, {});
    };
    hill (190, 300, 80, Colour (0xff8a4fd6));

    // Tiny stage.
    shape (g, rect ({ 286, 114, 40, 22 }), colours::ink, 2.0f, {});
    Path roof;
    roof.startNewSubPath (280, 116); roof.lineTo (306, 100); roof.lineTo (332, 116); roof.closeSubPath();
    shape (g, roof, colours::ink, 2.0f, {});
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (beams[i].withAlpha (0.5f + 0.5f * std::sin (s.time * 10.0f + (float) i)));
        g.fillEllipse (290.0f + (float) i * 9.0f, 118, 4, 4);
    }

    hill (240, 140, 160, Colour (0xff5bbf3a));
    hill (262, 420, 220, colours::slime);

    // Bunting across the top.
    {
        Path line;
        line.startNewSubPath (-10, 14);
        line.quadraticTo (280, 74, width + 10, 22);
        g.setColour (colours::ink);
        g.strokePath (line, PathStrokeType (2.5f));
        static const Colour flags[] = { colours::red, colours::yellow, colours::cyan, colours::slime, colours::orange };
        for (int i = 0; i < 16; ++i)
        {
            const float t = ((float) i + 0.5f) / 16.0f;
            const auto p = line.getPointAlongPath (line.getLength() * t);
            Path flag;
            flag.startNewSubPath (-9, 0); flag.lineTo (9, 0); flag.lineTo (0, 20); flag.closeSubPath();
            const float sway = std::sin (s.time * 4.0f + (float) i) * (0.08f + 0.5f * s.wind);
            flag.applyTransform (AffineTransform::rotation (sway).translated (p));
            shape (g, flag, flags[i % 5], 2.0f, {});
        }
    }

    // Tents, flapping in the wind.
    auto tent = [&] (float x, float baseY, float w, float h, Colour c, int i)
    {
        const float lean = s.wind * 10.0f * std::sin (s.time * 3.0f + (float) i) + s.wind * 6.0f;
        Path p;
        p.startNewSubPath (x - w * 0.5f, baseY);
        p.lineTo (x + lean, baseY - h);
        p.lineTo (x + w * 0.5f, baseY);
        p.closeSubPath();
        shape (g, p, c, 4.0f);
        Path flap;
        flap.startNewSubPath (x + lean * 0.5f, baseY - h * 0.5f);
        flap.lineTo (x - w * 0.12f, baseY);
        flap.lineTo (x + w * 0.12f, baseY);
        flap.closeSubPath();
        shape (g, flap, colours::ink.withAlpha (0.8f), 0, {});
    };
    tent (70, 272, 116, 74, colours::orange, 0);
    tent (180, 258, 80, 50, colours::cyan, 1);
    tent (480, 278, 124, 80, colours::yellow, 2);

    noteStream (g, { 306, 112 }, -0.5f + s.wind * 0.6f, 160, 0.6f, 6, s.time, 0.25f, 0.3f + 0.5f * s.level, 12, 61);

    Buddy you;
    you.colour = colours::pink;
    you.size = 52;
    you.mood = s.eventAge < 1.5f ? Buddy::shocked : Buddy::chill;
    you.squash = s.bass * 0.2f;
    you.look = { 0.3f, -0.6f };
    drawBuddy (g, { 320, 296 }, you);

    // Wind streaks.
    const int streaks = 3 + (int) (s.wind * 10.0f);
    for (int i = 0; i < streaks; ++i)
    {
        const float y = 50 + std::fmod ((float) i * 37.0f, 220.0f);
        const float x = frac (s.time * (0.25f + s.wind * 0.9f) + (float) i * 0.37f) * 720 - 80;
        Path w;
        w.startNewSubPath (x, y);
        w.cubicTo (x + 20, y - 8, x + 40, y + 8, x + 70, y);
        g.setColour (colours::cream.withAlpha (0.15f + 0.6f * s.wind));
        g.strokePath (w, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    comicBurst (g, { 150, 120 }, 40, "WHOOOSH!", colours::cyan, s.eventAge * 0.7f, 17);
}

//==============================================================================
void drawStudio (Graphics& g, const State& s)
{
    const Colour top[] = { colours::cream, colours::yellow, colours::cyan, colours::slime,
                           colours::pink, colours::red, colours::blue };
    const float bw = width / 7.0f;
    for (int i = 0; i < 7; ++i)
    {
        g.setColour (top[i]);
        g.fillRect ((float) i * bw, 0.0f, bw + 1, 220.0f);
    }
    const Colour bottom[] = { colours::blue, colours::ink, colours::pink, colours::ink,
                              colours::cyan, colours::ink, Colour (0xff8892a6) };
    for (int i = 0; i < 7; ++i)
    {
        g.setColour (bottom[i]);
        g.fillRect ((float) i * bw, 220.0f, bw + 1, height - 220.0f);
    }

    const float bob = std::sin (s.time * 2.0f) * 3.0f;
    shape (g, splat ({ width * 0.5f, 150 + bob }, 150, 3), colours::orange, 4.5f, { 5, 6 });
    text (g, "IN THE STUDIO", fonts().display (46), { 100, 112 + bob, 360, 60 }, Justification::centred,
          Colours::white, 3.5f, { 3, 4 }, -0.05f);
    text (g, "no teleporting. just your mix.", fonts().body (19), { 120, 168 + bob, 320, 30 }, Justification::centred,
          colours::ink, 0, {});
}

void drawStatic (Graphics& g, float amount, float time)
{
    if (amount <= 0)
        return;
    Random rng ((int64) (time * 1000.0f));
    const float cell = 8.0f;
    for (float y = 0; y < height; y += cell)
        for (float x = 0; x < width; x += cell)
        {
            const float v = rng.nextFloat();
            g.setColour (Colour::greyLevel (v).withAlpha (amount));
            g.fillRect (x, y, cell, cell);
        }
}

} // namespace

const std::vector<std::vector<const char*>>& smallTalk()
{
    static const std::vector<std::vector<const char*>> topics {
        { "is that my ex??", "DON'T look", "with the bassist?!", "classic" },
        { "kebab after?", "obviously", "the good one", "garlic sauce x2" },
        { "I have work at 9", "call in sick", "I'm the boss", "LOL" },
        { "who's the DJ?", "my cousin's flatmate", "he's good tho", "only plays his own tracks" },
        { "where's Dave?", "last seen in the toilets", "oh no", "not again" },
        { "my phone's on 2%", "mine's dead", "how do we get home?", "walk?? lol" },
        { "you're SUCH a Leo", "I'm a Virgo", "that explains it", "rude" },
        { "I put my rent in crypto", "and??", "I live here now" },
        { "I'm starting a podcast", "about what?", "this. us. tonight.", "groundbreaking" },
        { "my mum texted 'ok.'", "with a full stop?", "you're dead", "I know" },
        { "I quit smoking btw", "...", "this one doesn't count", "sure" },
        { "the queue was 2 hours", "I came in the back", "how??", "I know the bouncer" },
        { "my landlord's in there", "dancing??", "like a maniac", "rent's going up" },
        { "did you feed the cat?", "we don't have a cat", "then whose cat is it", "..." },
        { "I lost a shoe", "how", "no idea", "it's fine" },
        { "one more then home?", "you said that at 11", "and I meant it", "sure babe" },
        { "got a light?", "here", "you're a legend", "I know" },
        { "this song slaps", "who is it?", "no idea", "go ask him" },
        { "I think I'm in love", "with who?", "the coat check guy", "he gave you a ticket" },
        { "I texted my ex", "NO", "I said 'hey'", "delete your phone" },
        { "is it Sunday yet", "it's Saturday", "it's 4am Sunday", "oh no" },
        { "I'm a DJ now btw", "since when?", "Tuesday", "booking you for my birthday" },
        { "my uber is 40 min", "surge pricing?", "it's a horse", "LOL" },
        { "you look amazing", "it's the lighting", "it's a car park", "still" },
        { "I left my coat inside", "go get it", "the queue though", "it's your coat" },
        { "they're doing a mashup", "of what?", "everything", "respect" },
        { "my mum's on facebook", "and?", "she found my photos", "RIP" },
        { "I have a theory", "no", "about pigeons", "...go on" },
        { "I lost my friends", "we're right here", "not you lot", "rude" },
        { "the bass is so good", "you can't hear it out here", "I can FEEL it", "that's the bin lorry" },
        { "do you work here?", "no", "you look like you work here", "thanks?" },
        { "my horoscope said this", "said what?", "'avoid smokers'", "and yet" },
        { "I'm vegan now", "since when?", "this morning", "you're eating a kebab" },
        { "who invited Gary?", "Gary invited Gary", "classic Gary", "where IS Gary" },
        { "rate my outfit", "7", "only 7?!", "the shoes are a 3" },
        { "I'm sober tonight", "you're holding two drinks", "for a friend", "which friend" },
        { "you know the DJ?", "we went to school", "was he cool?", "he was the ICT kid" },
        { "I'm writing a novel", "what's it about", "this terrace", "write me in" },
        { "my therapist's here", "where??", "on the dancefloor", "go say hi" },
        { "they ran out of ice", "it's a club", "it's a crisis", "it's a disaster" },
        { "I'm moving to Berlin", "you said that last year", "and I meant it", "classic" },
        { "is that the sun?", "it's a street light", "oh", "go home" },
        { "I met a guy in the toilets", "romantic", "he sold me gum", "even better" },
        { "do I smell like smoke", "you are smoking", "but do I SMELL like it", "yes" },
        { "I booked a flight", "where to?", "I don't know yet", "iconic" },
        { "the bouncer winked at me", "he has a twitch", "let me have this", "fair" },
        { "what time is it", "late", "how late", "breakfast late" },
        { "I forgot my keys", "again?", "they're in my other jeans", "you're wearing shorts" },
        { "I'm so hungry", "kebab?", "I said HUNGRY", "two kebabs" },
    };
    return topics;
}

void draw (Graphics& g, int scene, const State& s)
{
    if (s.studio)
        drawStudio (g, s);
    else
    {
        switch (scene)
        {
            case 0:  drawNextDoor (g, s); break;
            case 1:  drawToilets (g, s); break;
            case 2:  drawSmokingArea (g, s); break;
            case 3:  drawCar (g, s); break;
            case 4:  drawPhone (g, s); break;
            default: drawFestival (g, s); break;
        }
    }

    // Channel-change static.
    if (s.switchAge < 0.3f)
        drawStatic (g, 1.0f - s.switchAge / 0.3f, s.time);

    // Scanlines for that CRT feel.
    g.setColour (Colours::black.withAlpha (0.06f));
    for (float y = 0; y < height; y += 4)
        g.fillRect (0.0f, y, width, 2.0f);
}

} // namespace art
