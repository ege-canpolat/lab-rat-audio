#pragma once

#include "Cartoon.h"

// The animated cartoon inside the TV. Everything is drawn in a fixed
// 560 x 312 space; the editor scales the whole window.
namespace art
{
inline constexpr float width = 560.0f, height = 312.0f;

struct State
{
    float time = 0;        // seconds
    float bass = 0;        // 0..1, normalised low-end energy of the input
    float level = 0;       // 0..1, output level
    float openness = 0;    // door / outside / pocket
    float wind = 0;
    float duck = 0;        // neighbour turned it down
    float vibes = 0.5f;
    float eventAge = 99;   // seconds since the last knock / slam / ping / gust
    int events = 0;
    float switchAge = 99;  // seconds since the channel changed (for static)
    bool studio = false;

    // Visitors in the toilets / on the terrace (state: 0 absent, 1 entering, 2 there, 3 leaving).
    struct Person
    {
        int state = 0, spot = 0, colour = 0;
        float progress = 0;
        bool talking = false, laughing = false;
        bool sick = false, puked = false;   // the one who came out to be sick
        juce::String words;   // what their speech bubble says this turn
    };
    std::array<Person, 5> people;
    float pukeAge = 99;    // seconds since someone was sick (the puddle hangs about)
    int pukeSpot = -1;
    int pukeCount = 0;     // picks the reactions, so they change each time

    // Passing traffic outside the car. x is in metres along the street.
    struct Vehicle
    {
        bool active = false, honking = false;
        float x = 0;
        int dir = 1, colour = 0;
    };
    std::array<Vehicle, 4> vehicles;

    int notification = -1;   // which phone message is showing (counts up, so the story unfolds)
};

void draw (juce::Graphics&, int scene, const State&);

// Terrace small talk: each topic is a little back-and-forth, one line per talk turn.
const std::vector<std::vector<const char*>>& smallTalk();

} // namespace art
