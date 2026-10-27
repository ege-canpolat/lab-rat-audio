#pragma once

#include "DSP.h"
#include "Life.h"
#include "SceneInfo.h"

namespace elsewhere
{
struct Controls
{
    float vibes = 0.5f;   // 0..1: how much "life" happens around you
    bool action = false;  // the big button
};

// Written by the audio thread, read by the UI for the cartoon animations.
struct Telemetry
{
    std::atomic<float> openness { 0 };  // door / outside / pocket, 0..1
    std::atomic<float> wind { 0 };      // festival wind, 0..1
    std::atomic<float> duck { 0 };      // neighbour turned it down, 0..1
    std::atomic<int> events { 0 };      // knocks, slams, pings, gusts

    // Visitors in the toilets / on the terrace.
    struct PersonView
    {
        std::atomic<int> state { 0 }, spot { 0 }, colour { 0 }, phrase { 0 };
        std::atomic<float> progress { 0 };
        std::atomic<bool> talking { false }, laughing { false }, sick { false }, puked { false };
    };
    std::array<PersonView, Crowd::maxPeople> people;
    std::atomic<int> pukes { 0 }, pukeSpot { -1 };

    // Passing traffic outside the car.
    struct CarView
    {
        std::atomic<bool> active { false }, honking { false };
        std::atomic<float> x { 0 };
        std::atomic<int> dir { 1 }, colour { 0 };
    };
    std::array<CarView, CityTraffic::maxCars> cars;
};

class Scene
{
public:
    explicit Scene (juce::dsp::ConvolutionMessageQueue& q) : queue (q) {}
    virtual ~Scene() = default;

    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;

    // In place, stereo. Never allocates.
    virtual void process (float* l, float* r, int numSamples, const Controls&, Telemetry&) = 0;

protected:
    juce::dsp::ConvolutionMessageQueue& queue;
};

std::unique_ptr<Scene> createScene (int index, juce::dsp::ConvolutionMessageQueue&);

// Per-scene gain so flipping between places doesn't jump in loudness.
// Measured with tools/Render against a full-range test mix.
float sceneMakeupDb (int index);

} // namespace elsewhere
