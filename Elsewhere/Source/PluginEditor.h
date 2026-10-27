#pragma once

#include "PluginProcessor.h"
#include "Cartoon.h"
#include "SceneArt.h"

// The TV screen showing where you are.
class TvScreen : public juce::Component
{
public:
    TvScreen() { setOpaque (true); }

    void paint (juce::Graphics&) override;

    int scene = 2;
    art::State state;
};

// The big splat you smash to knock / open the door / step outside.
class ActionButton : public juce::Component
{
public:
    ActionButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onPress, onRelease;
    juce::String label;
    bool engaged = false;
    bool pressed = false;
    float time = 0;
};

class CartoonKnob : public juce::Component
{
public:
    CartoonKnob (const juce::String& name, juce::Colour colour, float tiltDegrees);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String title;
    float tilt;
};

//==============================================================================
class MainView : public juce::Component
{
public:
    static constexpr int baseWidth = 940, baseHeight = 660;

    explicit MainView (ElsewhereProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void tick (float dt);

    static void paintLogo (juce::Graphics&);
    static juce::Rectangle<float> logoBounds();

private:
    void setScene (int index);
    void setAction (bool on);
    void releaseActionSoon();
    void paintBackdrop (juce::Graphics&);
    juce::String nextSmallTalk();

    ElsewhereProcessor& proc;
    cartoon::LookAndFeel lookAndFeel;

    TvScreen tv;
    juce::OwnedArray<cartoon::BlobButton> sceneButtons;
    cartoon::BlobButton studioButton { "BACK TO STUDIO", cartoon::colours::cyan, 404, 2.0f };
    CartoonKnob mixKnob { "MIX", cartoon::colours::pink, -3.0f };
    CartoonKnob vibesKnob { "VIBES", cartoon::colours::slime, 2.0f };
    CartoonKnob volumeKnob { "VOLUME", cartoon::colours::cyan, -1.5f };
    ActionButton actionButton;

    juce::ParameterAttachment sceneAttachment, actionAttachment;
    juce::ButtonParameterAttachment studioAttachment;
    juce::SliderParameterAttachment mixAttachment, vibesAttachment, volumeAttachment;

    juce::Image backdrop;
    int currentScene = 2;
    bool actionOn = false;
    double pressStartMs = 0;

    // Animation bookkeeping.
    float time = 0, bassPeak = 0.05f, bassSmooth = 0, levelSmooth = 0, levelPeak = 0.05f;
    int lastEvents = 0;
    float eventAge = 99, switchAge = 99;
    int lastPukes = 0, pukeSpot = -1, notification = -1, topic = -1, topicLine = 0;
    float pukeAge = 99;
    std::array<bool, 5> wasTalking {};
    juce::Random chatRng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

//==============================================================================
class ElsewhereEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ElsewhereEditor (ElsewhereProcessor&);
    ~ElsewhereEditor() override;

    void resized() override;

    MainView& getView() { return view; }

private:
    void timerCallback() override;

    MainView view;
    double lastTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElsewhereEditor)
};
