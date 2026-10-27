#include "PluginEditor.h"

using namespace juce;
using namespace cartoon;

namespace
{
const Rectangle<float> tvBezel  { 20, 118, 600, 406 };
const Rectangle<int>   tvScreen { 40, 138, 560, 312 };

Colour sceneColour (int index)
{
    // Built on first use: a namespace-scope copy of the palette can be initialised before the palette is.
    static const Colour palette[] = { colours::orange, colours::cyan, colours::pink,
                                      colours::red, colours::blue, colours::slimeDk };
    return palette[index];
}

double nowMs() { return Time::getMillisecondCounterHiRes(); }
}

//==============================================================================
void TvScreen::paint (Graphics& g)
{
    g.fillAll (Colour (0xff2d6b0a));
    Graphics::ScopedSaveState save (g);
    Path screen;
    screen.addRoundedRectangle (getLocalBounds().toFloat(), 16.0f);
    g.reduceClipRegion (screen);
    art::draw (g, scene, state);

    // Glass glare.
    Path glare;
    glare.startNewSubPath (30, 0); glare.lineTo (110, 0); glare.lineTo (0, 110); glare.lineTo (0, 30);
    glare.closeSubPath();
    glare.startNewSubPath (130, 0); glare.lineTo (150, 0); glare.lineTo (0, 150); glare.lineTo (0, 130);
    glare.closeSubPath();
    g.setColour (Colours::white.withAlpha (0.08f));
    g.fillPath (glare);
}

//==============================================================================
void ActionButton::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float bob = std::sin (time * 2.2f) * 0.035f;
    const float scale = pressed ? 0.92f : 1.0f + 0.025f * std::sin (time * 4.4f);
    const auto c = b.getCentre();

    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::rotation (bob - 0.04f, c.x, c.y).scaled (scale, scale, c.x, c.y));

    const auto fill = engaged ? colours::slime : colours::red;
    const auto offset = pressed ? Point<float> (2, 3) : Point<float> (0, 0);
    const float radius = jmin (b.getWidth() * 0.36f, (b.getHeight() * 0.5f - 12.0f) / (0.62f * 1.2f * 1.2f));
    auto p = splat (c + offset, radius, 23, 12);
    p.applyTransform (AffineTransform::scale (1.0f, 1.2f, c.x, c.y));
    shape (g, p, fill, 4.5f, pressed ? Point<float> (1.5f, 2.0f) : Point<float> (5.0f, 6.0f));

    g.setColour (Colours::white.withAlpha (0.25f));
    g.fillEllipse (c.x - radius * 0.6f, c.y - radius * 0.62f, radius * 0.6f, radius * 0.2f);

    text (g, label, fonts().display (label.length() > 9 ? 26.0f : 32.0f),
          b.withSizeKeepingCentre (radius * 1.8f, radius * 1.1f).translated (offset.x, offset.y + 3), Justification::centred,
          engaged ? colours::cream : colours::yellow, 3.0f, { 2.5f, 3.0f }, 0.0f, 2);
}

void ActionButton::mouseDown (const MouseEvent&)
{
    pressed = true;
    repaint();
    if (onPress) onPress();
}

void ActionButton::mouseUp (const MouseEvent&)
{
    pressed = false;
    repaint();
    if (onRelease) onRelease();
}

//==============================================================================
CartoonKnob::CartoonKnob (const String& name, Colour colour, float tiltDegrees)
    : title (name), tilt (degreesToRadians (tiltDegrees))
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setColour (Slider::rotarySliderFillColourId, colour);
    slider.setMouseCursor (MouseCursor::UpDownResizeCursor);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void CartoonKnob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedBottom (34).reduced (6, 0));
}

void CartoonKnob::paint (Graphics& g)
{
    auto area = getLocalBounds().toFloat().removeFromBottom (36);
    text (g, title, fonts().display (24), area.removeFromTop (24), Justification::centred,
          colours::cream, 2.5f, { 2.0f, 2.5f }, tilt);
    text (g, slider.getTextFromValue (slider.getValue()), fonts().body (13), area.translated (0, -1),
          Justification::centred, colours::yellow, 0, {});
}

//==============================================================================
MainView::MainView (ElsewhereProcessor& p)
    : proc (p),
      sceneAttachment (*p.apvts.getParameter (ParamIDs::scene), [this] (float v) { setScene ((int) v); }),
      actionAttachment (*p.apvts.getParameter (ParamIDs::action), [this] (float v)
      {
          actionOn = v > 0.5f;
          actionButton.engaged = actionOn && elsewhere::sceneInfos[(size_t) currentScene].mode == elsewhere::ActionMode::toggle;
          const auto& info = elsewhere::sceneInfos[(size_t) currentScene];
          actionButton.label = actionButton.engaged ? info.actionOn : info.action;
          actionButton.repaint();
      }),
      studioAttachment (*p.apvts.getParameter (ParamIDs::studio), studioButton),
      mixAttachment (*p.apvts.getParameter (ParamIDs::mix), mixKnob.slider),
      vibesAttachment (*p.apvts.getParameter (ParamIDs::vibes), vibesKnob.slider),
      volumeAttachment (*p.apvts.getParameter (ParamIDs::output), volumeKnob.slider)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (tv);

    for (int i = 0; i < elsewhere::numScenes; ++i)
    {
        auto* b = sceneButtons.add (new BlobButton (elsewhere::sceneInfos[(size_t) i].name, sceneColour (i),
                                                    100 + i, (i % 2 == 0) ? -1.2f : 1.2f));
        b->setSelectedStyle (true);
        b->setFontHeight (25.0f);
        b->onClick = [this, i] { sceneAttachment.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }

    studioButton.setClickingTogglesState (true);
    studioButton.setLabels ("BACK TO STUDIO", "GO ELSEWHERE!");
    studioButton.setFontHeight (24.0f);
    studioButton.setOnColour (colours::yellow);
    addAndMakeVisible (studioButton);

    for (auto* k : { &mixKnob, &vibesKnob, &volumeKnob })
        addAndMakeVisible (k);
    mixKnob.slider.setDoubleClickReturnValue (true, 100.0);
    vibesKnob.slider.setDoubleClickReturnValue (true, 50.0);
    volumeKnob.slider.setDoubleClickReturnValue (true, 0.0);
    mixKnob.slider.setTooltip ("How far you teleport. 100% = fully there.");

    actionButton.onPress = [this]
    {
        if (elsewhere::sceneInfos[(size_t) currentScene].mode == elsewhere::ActionMode::toggle)
            setAction (! actionOn);
        else
        {
            pressStartMs = nowMs();
            setAction (true);
        }
    };
    actionButton.onRelease = [this]
    {
        if (elsewhere::sceneInfos[(size_t) currentScene].mode != elsewhere::ActionMode::toggle)
            releaseActionSoon();
    };
    addAndMakeVisible (actionButton);

    sceneAttachment.sendInitialUpdate();
    actionAttachment.sendInitialUpdate();
    studioAttachment.sendInitialUpdate();
    mixAttachment.sendInitialUpdate();
    vibesAttachment.sendInitialUpdate();
    volumeAttachment.sendInitialUpdate();
    switchAge = 99;
    lastEvents = proc.telemetry.events.load();
    setSize (baseWidth, baseHeight);
}

MainView::~MainView()
{
    setLookAndFeel (nullptr);
}

void MainView::setScene (int index)
{
    index = jlimit (0, elsewhere::numScenes - 1, index);
    const bool changed = index != currentScene;
    currentScene = index;

    for (int i = 0; i < sceneButtons.size(); ++i)
        sceneButtons[i]->setToggleState (i == index, dontSendNotification);

    // The button means something different in each place.
    if (changed && actionOn)
        setAction (false);

    const auto& info = elsewhere::sceneInfos[(size_t) index];
    actionButton.engaged = false;
    actionButton.label = info.action;
    actionButton.repaint();

    tv.scene = index;
    switchAge = 0;
    eventAge = 99;
    pukeAge = 99;
    pukeSpot = -1;
    backdrop = {};
    repaint();
}

void MainView::setAction (bool on)
{
    actionAttachment.setValueAsCompleteGesture (on ? 1.0f : 0.0f);
}

void MainView::releaseActionSoon()
{
    // Keep quick clicks down long enough for the audio thread to notice.
    const int wait = jmax (0, 150 - (int) (nowMs() - pressStartMs));
    Timer::callAfterDelay (wait, [safe = Component::SafePointer<MainView> (this)]
    {
        if (safe != nullptr && ! safe->actionButton.pressed)
            safe->setAction (false);
    });
}

void MainView::resized()
{
    tv.setBounds (tvScreen);
    for (int i = 0; i < sceneButtons.size(); ++i)
        sceneButtons[i]->setBounds (640, 158 + i * 61, 280, 60);
    studioButton.setBounds (664, 28, 258, 66);

    mixKnob.setBounds (40, 530, 128, 126);
    vibesKnob.setBounds (178, 530, 128, 126);
    volumeKnob.setBounds (316, 530, 128, 126);
    actionButton.setBounds (630, 500, 300, 160);
}

void MainView::tick (float dt)
{
    time += dt;
    eventAge += dt;
    switchAge += dt;

    // Normalise the bass so the cartoon dances whatever the track's level.
    const float bass = proc.bassLevel.load();
    bassPeak = jmax (bass, bassPeak * std::pow (0.5f, dt / 3.0f), 0.02f);
    bassSmooth += (jlimit (0.0f, 1.0f, bass / bassPeak) - bassSmooth) * jmin (1.0f, dt * 18.0f);

    const float level = proc.outputLevel.load();
    levelPeak = jmax (level, levelPeak * std::pow (0.5f, dt / 3.0f), 0.02f);
    levelSmooth += (jlimit (0.0f, 1.0f, level / levelPeak) - levelSmooth) * jmin (1.0f, dt * 12.0f);

    const int events = proc.telemetry.events.load();
    if (events != lastEvents)
    {
        lastEvents = events;
        eventAge = 0;
        if (currentScene == 4)
            ++notification;   // the next message in the phone's little soap opera
    }

    auto& s = tv.state;
    s.time = time;
    s.bass = bassSmooth * bassSmooth;   // a bit more punch: only the hits really move things
    s.level = levelSmooth;
    s.openness = proc.telemetry.openness.load();
    s.wind = proc.telemetry.wind.load();
    s.duck = jlimit (0.0f, 1.0f, proc.telemetry.duck.load());
    s.vibes = proc.apvts.getRawParameterValue (ParamIDs::vibes)->load() * 0.01f;
    s.studio = proc.apvts.getRawParameterValue (ParamIDs::studio)->load() > 0.5f;
    s.eventAge = eventAge;
    s.events = events;
    s.switchAge = switchAge;
    s.notification = notification;

    // Who's around, and whether anyone has disgraced themselves.
    auto& t = proc.telemetry;
    pukeAge += dt;
    const int pukes = t.pukes.load();
    if (pukes != lastPukes)
    {
        lastPukes = pukes;
        pukeAge = 0;
        pukeSpot = t.pukeSpot.load();
    }
    s.pukeAge = pukeAge;
    s.pukeSpot = pukeSpot;
    s.pukeCount = lastPukes;

    for (size_t k = 0; k < s.people.size(); ++k)
    {
        auto& p = s.people[k];
        const auto& view = t.people[k];
        p.state = view.state.load();
        p.spot = view.spot.load();
        p.colour = view.colour.load();
        p.progress = view.progress.load();
        p.talking = view.talking.load();
        p.laughing = view.laughing.load();
        p.sick = view.sick.load();
        p.puked = view.puked.load();
        if (p.talking && ! wasTalking[k])
            p.words = nextSmallTalk();   // the next line of whatever they're on about
        wasTalking[k] = p.talking;
    }

    for (size_t k = 0; k < s.vehicles.size(); ++k)
    {
        auto& v = s.vehicles[k];
        const auto& view = t.cars[k];
        v.active = view.active.load();
        v.x = view.x.load();
        v.dir = view.dir.load();
        v.colour = view.colour.load();
        v.honking = view.honking.load();
    }
    tv.repaint();

    actionButton.time = time;
    actionButton.repaint();
}

String MainView::nextSmallTalk()
{
    const auto& topics = art::smallTalk();
    if (topic < 0 || topicLine >= (int) topics[(size_t) topic].size())
    {
        // New topic, never the same one twice in a row.
        const int previous = topic;
        do topic = chatRng.nextInt ((int) topics.size());
        while (topic == previous);
        topicLine = 0;
    }
    return topics[(size_t) topic][(size_t) topicLine++];
}

//==============================================================================
void MainView::paint (Graphics& g)
{
    // The static artwork is cached at the current physical resolution.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int w = roundToInt ((float) baseWidth * scale), h = roundToInt ((float) baseHeight * scale);
    if (backdrop.getWidth() != w || backdrop.getHeight() != h)
    {
        backdrop = Image (Image::ARGB, w, h, true);
        Graphics bg (backdrop);
        bg.addTransform (AffineTransform::scale (scale));
        paintBackdrop (bg);
    }
    g.drawImage (backdrop, getLocalBounds().toFloat());
}

void MainView::paintBackdrop (Graphics& g)
{
    const auto& info = elsewhere::sceneInfos[(size_t) currentScene];
    const auto W = (float) baseWidth, H = (float) baseHeight;

    g.fillAll (colours::purple);

    // Confetti and squiggles.
    Random rng (2001);
    static const Colour confetti[] = { colours::pink, colours::cyan, colours::yellow, colours::slime, colours::orange };
    for (int i = 0; i < 70; ++i)
    {
        const Point<float> p (rng.nextFloat() * W, rng.nextFloat() * H);
        const auto c = confetti[rng.nextInt (5)].withAlpha (0.22f);
        g.setColour (c);
        switch (rng.nextInt (3))
        {
            case 0: g.fillEllipse (p.x - 5, p.y - 5, 10, 10); break;
            case 1:
            {
                Path t;
                t.addTriangle (p.x, p.y - 8, p.x + 8, p.y + 6, p.x - 8, p.y + 6);
                t.applyTransform (AffineTransform::rotation (rng.nextFloat() * 6.0f, p.x, p.y));
                g.fillPath (t);
                break;
            }
            default:
            {
                Path s;
                s.startNewSubPath (p);
                for (int k = 1; k <= 4; ++k)
                    s.lineTo (p.x + (float) k * 9.0f, p.y + ((k % 2) ? -7.0f : 7.0f));
                s.applyTransform (AffineTransform::rotation (rng.nextFloat() * 6.0f, p.x, p.y));
                g.strokePath (s, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
                break;
            }
        }
    }

    // Slime oozing off the top edge.
    {
        Path slime;
        slime.startNewSubPath (-10, -10);
        slime.lineTo (W + 10, -10);
        slime.lineTo (W + 10, 12);
        Random drip (7);
        float x = W + 10;
        while (x > -10)
        {
            const float gap = 24.0f + drip.nextFloat() * 40.0f;
            const float len = drip.nextFloat() < 0.5f ? 0.0f : 14.0f + drip.nextFloat() * 46.0f;
            const float dw = 8.0f + drip.nextFloat() * 8.0f;
            slime.quadraticTo (x - gap * 0.5f, 14 + drip.nextFloat() * 6.0f, x - gap, 12);
            x -= gap;
            if (len > 0)
            {
                slime.lineTo (x - 2, 12 + len);
                slime.cubicTo (x - 2, 12 + len + dw * 1.2f, x - dw - 2, 12 + len + dw * 1.2f, x - dw - 2, 12 + len);
                slime.lineTo (x - dw, 12);
                x -= dw;
            }
        }
        slime.lineTo (-10, 12);
        slime.closeSubPath();
        shape (g, slime, colours::slime, 4.0f, { 0, 5 });
        g.setColour (Colours::white.withAlpha (0.35f));
        g.fillRoundedRectangle (40, 2, 220, 5, 2.5f);
    }

    // TV antenna, behind the set.
    g.setColour (colours::ink);
    g.drawLine (480, 122, 448, 46, 6.0f);
    g.drawLine (500, 122, 560, 40, 6.0f);
    for (auto p : { Point<float> (448, 46), Point<float> (560, 40) })
        shape (g, blob ({ p.x - 9, p.y - 9, 18, 18 }, 0.02f, 3, 8), colours::pink, 3.5f, {});

    // The TV.
    shape (g, wobblyRect (tvBezel, 30, 2.5f, 11), colours::slime, 5.0f, { 6, 8 });
    g.setColour (Colours::white.withAlpha (0.3f));
    g.fillRoundedRectangle (tvBezel.getX() + 40, tvBezel.getY() + 7, 260, 7, 3.5f);
    shape (g, wobblyRect (tvScreen.toFloat().expanded (9), 22, 1.0f, 12), Colour (0xff2d6b0a), 4.0f, {});

    // Who made the telly: the maker's badge on the bezel.
    text (g, "LAB RAT AUDIO", fonts().display (13), { 340, 119.5f, 130, 17 }, Justification::centred,
          colours::cream, 2.0f, { 1.0f, 1.5f });

    // Channel caption on the bottom of the set.
    const Rectangle<float> strip (40, 456, 560, 62);
    shape (g, wobblyRect ({ strip.getX(), strip.getY() + 10, 74, 36 }, 12, 1, 4), colours::ink, 0, {});
    text (g, "CH " + String (currentScene + 1), fonts().display (21), { strip.getX(), strip.getY() + 12, 74, 36 },
          Justification::centred, colours::yellow, 0, {});
    text (g, info.name, fonts().display (30), { strip.getX() + 86, strip.getY() + 2, 400, 34 },
          Justification::centredLeft, Colours::white, 3.0f, { 2.5f, 3.0f });
    text (g, info.tagline, fonts().body (14.5f), { strip.getX() + 87, strip.getY() + 34, 420, 22 },
          Justification::centredLeft, colours::ink, 0, {});
    for (int i = 0; i < 2; ++i)
        shape (g, blob ({ 548.0f + (float) i * 30.0f - 13.0f, strip.getY() + 16, 24, 24 }, 0.02f, 40 + i, 8),
               i == 0 ? colours::orange : colours::pink, 3.0f, {});

    paintLogo (g);

    text (g, "WHERE ARE YOU?", fonts().display (30), { 640, 116, 280, 40 }, Justification::centred,
          colours::yellow, 3.0f, { 2.5f, 3.0f }, -0.03f);

    // A hint pointing at the big button.
    const String hint = info.mode == elsewhere::ActionMode::toggle ? "CLICK IT!" : "SMASH IT!";
    text (g, hint, fonts().display (26), { 470, 548, 160, 40 }, Justification::centred,
          colours::yellow, 2.5f, { 2, 2.5f }, -0.12f);
    Path arrow;
    arrow.startNewSubPath (520, 594);
    arrow.quadraticTo (560, 646, 668, 614);
    g.setColour (colours::ink);
    g.strokePath (arrow, PathStrokeType (9.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (colours::yellow);
    g.strokePath (arrow, PathStrokeType (4.5f, PathStrokeType::curved, PathStrokeType::rounded));
    Path head;
    head.addTriangle (680, 607, 660, 600, 666, 624);
    shape (g, head, colours::yellow, 2.5f, {});
}

// The logo: a big orange splat. Also exported for the website, so keep it self-contained.
void MainView::paintLogo (Graphics& g)
{
    const Point<float> c (232, 62);
    auto p = splat (c, 150, 1987, 13);
    p.applyTransform (AffineTransform::scale (1.0f, 0.78f, c.x, c.y));
    shape (g, p, colours::orange, 5.0f, { 6, 7 });
    text (g, "ELSEWHERE", fonts().display (68), { 52, 22, 360, 74 }, Justification::centred,
          Colours::white, 4.0f, { 4.0f, 5.0f }, -0.05f);

    const Rectangle<float> pill (112, 92, 250, 26);
    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::rotation (0.025f, pill.getCentreX(), pill.getCentreY()));
    shape (g, wobblyRect (pill, 13, 1, 5), colours::pink, 3.5f, { 3, 4 });
    text (g, "HEAR YOUR MIX FROM SOMEWHERE ELSE", fonts().display (14.5f), pill.translated (0, 2),
          Justification::centred, Colours::white, 1.5f, { 1, 1.5f });
}

Rectangle<float> MainView::logoBounds()
{
    auto p = splat ({ 232, 62 }, 150, 1987, 13);
    p.applyTransform (AffineTransform::scale (1.0f, 0.78f, 232, 62));
    // The splat, the word (which is wider than the splat) and the pill, plus room for outlines and hard shadows.
    return p.getBounds().getUnion ({ 52, 22, 360, 74 }).getUnion ({ 112, 92, 250, 26 })
               .expanded (6).withTrimmedRight (-7).withTrimmedBottom (-8);
}

//==============================================================================
ElsewhereEditor::ElsewhereEditor (ElsewhereProcessor& p)
    : AudioProcessorEditor (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) MainView::baseWidth / MainView::baseHeight);
        c->setSizeLimits (MainView::baseWidth / 2, MainView::baseHeight / 2,
                          MainView::baseWidth * 2, MainView::baseHeight * 2);
    }
    setSize (MainView::baseWidth, MainView::baseHeight);
    lastTick = Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

ElsewhereEditor::~ElsewhereEditor()
{
    stopTimer();
}

void ElsewhereEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::baseWidth;
    view.setTransform (AffineTransform::scale (scale));
    view.setBounds (0, 0, MainView::baseWidth, MainView::baseHeight);
}

void ElsewhereEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = (float) jlimit (0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;
    view.tick (dt);
}
