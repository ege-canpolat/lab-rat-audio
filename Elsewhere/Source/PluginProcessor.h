#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Scenes.h"

namespace ParamIDs
{
inline constexpr const char* scene  = "scene";
inline constexpr const char* mix    = "mix";
inline constexpr const char* vibes  = "vibes";
inline constexpr const char* action = "action";
inline constexpr const char* output = "output";
inline constexpr const char* studio = "studio";
}

class ElsewhereProcessor : public juce::AudioProcessor
{
public:
    ElsewhereProcessor();
    ~ElsewhereProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Elsewhere"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // For the cartoon: what's happening "out there" right now.
    elsewhere::Telemetry telemetry;
    std::atomic<float> bassLevel { 0 }, outputLevel { 0 };

private:
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int maxChunk = 512;

    juce::dsp::ConvolutionMessageQueue convolutionQueue { 64 };
    std::array<std::unique_ptr<elsewhere::Scene>, elsewhere::numScenes> scenes;
    elsewhere::Telemetry fadingTelemetry;   // the scene we're leaving reports here

    juce::AudioBuffer<float> dry, wet, fading;
    int current = 2, previous = -1;
    float fade = 1, fadeStep = 0;
    bool ignoreActionUntilReleased = false;

    juce::SmoothedValue<float> mixSmooth, outSmooth, studioSmooth;
    elsewhere::Svf bassFilter;
    elsewhere::Follower bassFollower, levelFollower;

    std::atomic<float>* sceneParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* vibesParam = nullptr;
    std::atomic<float>* actionParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* studioParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElsewhereProcessor)
};
