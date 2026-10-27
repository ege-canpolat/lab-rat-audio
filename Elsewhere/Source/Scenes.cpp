#include "Scenes.h"

namespace elsewhere
{
namespace
{
constexpr int controlChunk = 32;   // filter coefficients update this often

void sumToMono (float* l, float* r, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        l[i] = r[i] = 0.5f * (l[i] + r[i]);
}

void setWidth (float& l, float& r, float width) noexcept
{
    const float m = 0.5f * (l + r), s = 0.5f * (l - r) * width;
    l = m + s;
    r = m - s;
}

void prepareAll (double sr, std::initializer_list<Svf*> filters)
{
    for (auto* f : filters)
    {
        f->setSampleRate (sr);
        f->reset();
    }
}

void resetAll (std::initializer_list<Svf*> filters)
{
    for (auto* f : filters)
        f->reset();
}

//==============================================================================
// A club PA in a big room: the source for the toilets and the smoking area.
class ClubPA
{
public:
    explicit ClubPA (juce::dsp::ConvolutionMessageQueue& q) : room (q) {}

    void prepare (double sr, int maxBlock)
    {
        prepareAll (sr, { &hp, &sub });
        hp.highpass (32.0f, 0.7f);
        sub.lowShelf (70.0f, 0.7f, 4.0f);

        RoomSpec spec;
        spec.lengthSec = 2.4f;   spec.rt60 = 1.5f;      spec.predelayMs = 14.0f;
        spec.erCount = 18;       spec.erSpreadMs = 60;  spec.erGain = 0.3f;
        spec.brightStart = 6500; spec.brightEnd = 1500; spec.highpass = 70;
        spec.direct = 0.75f;     spec.wet = 0.6f;       spec.width = 0.85f;
        spec.seed = 101;
        room.prepare (sr, maxBlock, spec);
    }

    void reset() { resetAll ({ &hp, &sub }); room.reset(); }

    void process (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            // Loud PA, slightly pushed.
            l[i] = std::tanh (1.25f * sub.process (0, hp.process (0, l[i]))) * 0.8f;
            r[i] = std::tanh (1.25f * sub.process (1, hp.process (1, r[i]))) * 0.8f;
        }
        room.process (l, r, n);
    }

private:
    Svf hp, sub;
    RoomStage room;
};

//==============================================================================
class NextDoor : public Scene
{
public:
    using Scene::Scene;

    void prepare (double sampleRate, int maxBlock) override
    {
        sr = (float) sampleRate;
        prepareAll (sampleRate, { &hp, &lpA, &lpB, &res, &res2, &leak });
        hp.highpass (35.0f, 0.7f);
        lpA.lowpass (240.0f, 0.6f);     // walls let the bass through...
        lpB.lowpass (520.0f, 0.75f);    // ...and eat everything above it
        res.bell (95.0f, 1.2f, 5.0f);   // plasterboard boom
        res2.bell (170.0f, 1.5f, 2.5f);
        leak.bandpass (1400.0f, 0.8f);  // a hint of vocal through the gaps round the door

        knock.prepare (sampleRate);
        neighbour.prepare (sampleRate, 125.0f, 808);
        neighbour.setLoudness (0.8f);

        RoomSpec theirs;
        theirs.lengthSec = 0.9f;    theirs.rt60 = 0.55f;     theirs.predelayMs = 3;
        theirs.erCount = 14;        theirs.erSpreadMs = 22;  theirs.erGain = 0.45f;
        theirs.brightStart = 6000;  theirs.brightEnd = 2500; theirs.highpass = 60;
        theirs.direct = 1;          theirs.wet = 0.45f;      theirs.width = 0.6f;
        theirs.seed = 3;
        theirRoom.prepare (sampleRate, maxBlock, theirs);

        RoomSpec yours;
        yours.lengthSec = 0.6f;     yours.rt60 = 0.35f;      yours.predelayMs = 1.5f;
        yours.erCount = 10;         yours.erSpreadMs = 12;   yours.erGain = 0.35f;
        yours.brightStart = 2500;   yours.brightEnd = 900;   yours.highpass = 40;
        yours.direct = 1;           yours.wet = 0.3f;        yours.width = 1;
        yours.seed = 5;
        yourRoom.prepare (sampleRate, maxBlock, yours);

        downCoef = 1.0f - std::exp (-1.0f / (0.6f * sr));
        upCoef   = 1.0f - std::exp (-1.0f / (5.0f * sr));
    }

    void reset() override
    {
        resetAll ({ &hp, &lpA, &lpB, &res, &res2, &leak });
        knock.reset(); neighbour.reset(); theirRoom.reset(); yourRoom.reset();
        duckGain = 1; duckCountdown = holdCountdown = 0; voiceStart = voiceStop = -1; lastAction = false;
    }

    void process (float* l, float* r, int n, const Controls& c, Telemetry& t) override
    {
        sumToMono (l, r, n);

        if (c.action && ! lastAction)
        {
            // Knock knock knock... "MMF MMF!"... and then they turn it down.
            knock.knockThreeTimes();
            voiceStart = (int) (0.85f * sr);
            voiceStop = (int) (1.9f * sr);
            duckCountdown = (int) (2.1f * sr);
            t.events.fetch_add (1);
        }
        lastAction = c.action;

        // The neighbour shouts back from their room, so it goes through the wall too.
        for (int i = 0; i < n; ++i)
        {
            if (voiceStart >= 0 && voiceStart-- == 0) neighbour.setTalking (true);
            if (voiceStop >= 0 && voiceStop-- == 0)   neighbour.setTalking (false);
            const float v = neighbour.process() * 0.7f;
            l[i] += v;
            r[i] += v;
        }

        theirRoom.process (l, r, n);

        for (int i = 0; i < n; ++i)
        {
            if (duckCountdown > 0 && --duckCountdown == 0)
                holdCountdown = (int) (7.0f * sr);
            float duckTarget = 1.0f;
            if (holdCountdown > 0)
            {
                --holdCountdown;
                duckTarget = 0.3f;
            }
            duckGain += (duckTarget - duckGain) * (duckTarget < duckGain ? downCoef : upCoef);

            float wl = res2.process (0, res.process (0, lpB.process (0, lpA.process (0, hp.process (0, l[i])))));
            float wr = res2.process (1, res.process (1, lpB.process (1, lpA.process (1, hp.process (1, r[i])))));
            wl = (wl + 0.018f * leak.process (0, l[i])) * duckGain;
            wr = (wr + 0.018f * leak.process (1, r[i])) * duckGain;

            const float k = knock.process();
            l[i] = wl + k;
            r[i] = wr + k * 0.9f;
        }

        yourRoom.process (l, r, n);
        t.duck.store ((1.0f - duckGain) / 0.7f);
        t.openness.store (0);
    }

private:
    Svf hp, lpA, lpB, res, res2, leak;
    Knocker knock;
    Babbler neighbour;
    RoomStage theirRoom { queue }, yourRoom { queue };
    float sr = 44100, duckGain = 1, downCoef = 0, upCoef = 0;
    int duckCountdown = 0, holdCountdown = 0, voiceStart = -1, voiceStop = -1;
    bool lastAction = false;
};

//==============================================================================
// Shared by the toilets and the smoking area: club PA -> door -> where you are,
// with people wandering in and out of that door.
struct DoorSettings
{
    float closedHz, openHz, closedDb, openDb, closedWidth, openWidth, thumpDb;
    float openSec, closeSec;
    float slamGain;   // scaled against the scene's makeup gain so the slam lands at a sane level
    float leakGain;   // what sneaks through the gaps round a shut door
    float lifeGain;   // footsteps, plumbing, lighters; also scaled against the makeup gain
    float voiceGain;  // people talking right next to you
    float doorPan;
};

enum class Venue { toilets, terrace };

class BehindTheDoor : public Scene
{
public:
    BehindTheDoor (juce::dsp::ConvolutionMessageQueue& q, Venue v, DoorSettings s, RoomSpec spaceSpec,
                   CrowdSettings crowdSettings, std::array<float, 5> pans, float ambience)
        : Scene (q), venue (v), settings (s), spec (std::move (spaceSpec)), crowdSpec (crowdSettings),
          spotPans (pans), ambienceLevel (ambience) {}

    void prepare (double sampleRate, int maxBlock) override
    {
        sr = (float) sampleRate;
        pa.prepare (sampleRate, maxBlock);
        prepareAll (sampleRate, { &lpA, &lpB, &thump, &humLp, &leak, &cityLp, &murmurLp });
        humLp.lowpass (320.0f, 0.6f);
        cityLp.lowpass (2500.0f, 0.6f);    // the street is round the corner
        murmurLp.lowpass (1500.0f, 0.6f);  // other groups, a few metres off
        leak.bandpass (1100.0f, 0.7f);
        door.prepare (sampleRate, settings.openSec, settings.closeSec);
        door.reset();
        slam.prepare (sampleRate, 68.0f, 130.0f, 900.0f, 22.0f);
        space.prepare (sampleRate, maxBlock, spec);

        crowd.prepare (sampleRate, crowdSpec, spec.seed + 9);
        const std::array<Mode, 3> tiles    {{ { 190.0f, 0.03f, 1.0f }, { 1100.0f, 0.015f, 0.45f }, { 2700.0f, 0.008f, 0.3f } }};
        const std::array<Mode, 3> concrete {{ { 140.0f, 0.035f, 1.0f }, { 600.0f, 0.02f, 0.4f }, { 1800.0f, 0.01f, 0.2f } }};
        const float pitches[] = { 110.0f, 205.0f, 135.0f, 230.0f, 160.0f };
        for (int k = 0; k < Crowd::maxPeople; ++k)
        {
            steps[(size_t) k].prepare (sampleRate, venue == Venue::toilets ? tiles : concrete);
            voices[(size_t) k].prepare (sampleRate, pitches[k], spec.seed + 100 + (uint32_t) k);
            water[(size_t) k].prepare (sampleRate);
        }
        const float murmurPitches[] = { 112.0f, 195.0f, 150.0f };
        for (size_t k = 0; k < murmur.size(); ++k)
            murmur[k].prepare (sampleRate, murmurPitches[k], spec.seed + 200 + (uint32_t) k);
        city.prepare (sampleRate, spec.seed + 300);
        cityL.assign ((size_t) maxBlock, 0.0f);
        cityR.assign ((size_t) maxBlock, 0.0f);
        reset();
    }

    void reset() override
    {
        pa.reset(); resetAll ({ &lpA, &lpB, &thump, &humLp, &leak, &cityLp, &murmurLp });
        for (auto& m : murmur)
            m.reset();
        murmurCountdown = {};
        city.reset();
        door.reset(); slam.reset(); space.reset();
        crowd.reset (lastVibes);
        for (int k = 0; k < Crowd::maxPeople; ++k)
        {
            steps[(size_t) k].reset();
            voices[(size_t) k].reset();
            water[(size_t) k].reset();
        }
        talker = -1;
        turnCountdown = 1.0f;
        updateFilters (0);
    }

    void process (float* l, float* r, int n, const Controls& c, Telemetry& t) override
    {
        pa.process (l, r, n);
        lastVibes = c.vibes;
        const float fade = smoothstep (c.vibes * 5.0f);
        const float life = fade * settings.lifeGain;
        const float voiceLevel = fade * settings.voiceGain;
        const auto doorGains = panGains (settings.doorPan);

        // The world beyond the terrace: the city, and other people chatting further off.
        const bool outdoors = venue == Venue::terrace;
        if (outdoors)
        {
            std::fill (cityL.begin(), cityL.begin() + n, 0.0f);
            std::fill (cityR.begin(), cityR.begin() + n, 0.0f);
            city.process (cityL.data(), cityR.data(), n, c.vibes);
        }
        const float cityLevel = 0.07f * fade, murmurLevel = 0.03f * fade;

        for (int start = 0; start < n; start += controlChunk)
        {
            const int len = std::min (controlChunk, n - start);

            Crowd::Events ev;
            const bool someoneAtTheDoor = crowd.advance (len, c.vibes, ev);
            const bool wantOpen = c.action || (someoneAtTheDoor && c.vibes > 0.02f);
            handleEvents (ev, t);
            if (outdoors)
            {
                converse (len);
                murmurOn (len);
            }

            std::array<std::pair<float, float>, Crowd::maxPeople> stepGains, spotGains;
            for (int k = 0; k < Crowd::maxPeople; ++k)
            {
                stepGains[(size_t) k] = panGains (walkingPan (k));
                spotGains[(size_t) k] = panGains (spotPans[(size_t) juce::jlimit (0, 4, crowd.people[(size_t) k].spot)]);
            }

            const float o = door.openness();
            updateFilters (o);
            const float leakAmount = settings.leakGain * (1.0f - o);

            for (int i = start; i < start + len; ++i)
            {
                if (door.process (wantOpen))
                {
                    // Some people let it slam, some don't.
                    slam.trigger (settings.slamGain * (0.35f + 0.65f * noise.uniform()));
                    t.events.fetch_add (1);
                }

                float xl = gain * thump.process (0, lpB.process (0, lpA.process (0, l[i]))) + leakAmount * leak.process (0, l[i]);
                float xr = gain * thump.process (1, lpB.process (1, lpA.process (1, r[i]))) + leakAmount * leak.process (1, r[i]);
                setWidth (xl, xr, width);

                // Everyone around you.
                float ll = 0, lr = 0;
                auto add = [&] (float x, const std::pair<float, float>& g) { ll += x * g.first; lr += x * g.second; };
                for (size_t k = 0; k < (size_t) Crowd::maxPeople; ++k)
                {
                    add (steps[k].process() * 0.1f * life, stepGains[k]);
                    add (voices[k].process() * voiceLevel, spotGains[k]);
                    add (water[k].process() * 0.8f * life, spotGains[k]);
                }

                if (outdoors)
                {
                    float m = 0, ml = 0, mr = 0;
                    for (size_t k = 0; k < murmur.size(); ++k)
                    {
                        const float v = murmur[k].process();
                        m += v;
                        ml += v * murmurGains[k].first;
                        mr += v * murmurGains[k].second;
                    }
                    // Low-passed as a whole, but keep each group's place in the stereo field.
                    const float dull = murmurLp.process (0, m) - m;
                    ll += murmurLevel * (ml + 0.5f * dull) + cityLevel * cityLp.process (0, cityL[(size_t) i]);
                    lr += murmurLevel * (mr + 0.5f * dull) + cityLevel * cityLp.process (1, cityR[(size_t) i]);
                }

                const float s = slam.process();
                const float hum = ambienceLevel * c.vibes * humLp.process (0, noise.pink());
                l[i] = xl + s * doorGains.first + hum + ll;
                r[i] = xr + s * doorGains.second + hum + lr;
            }

            t.openness.store (o);
            for (int k = 0; k < Crowd::maxPeople; ++k)
            {
                auto& view = t.people[(size_t) k];
                const auto& p = crowd.people[(size_t) k];
                view.state.store ((int) p.state);
                view.spot.store (p.spot);
                view.colour.store (p.colour);
                view.progress.store (crowd.progress (k));
                view.talking.store (talker == k && voices[(size_t) k].isTalking());
                view.laughing.store (voices[(size_t) k].isLaughing());
                view.sick.store (p.sick);
                view.puked.store (p.puked);
            }
        }

        space.process (l, r, n);
    }

private:
    static std::pair<float, float> panGains (float pan) noexcept
    {
        const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * pi * 0.25f;
        return { std::cos (a), std::sin (a) };
    }

    // Where someone is between the door and their spot, as a stereo position.
    float walkingPan (int k) const noexcept
    {
        const auto& p = crowd.people[(size_t) k];
        const float spot = spotPans[(size_t) juce::jlimit (0, 4, p.spot)];
        const float u = crowd.progress (k);
        if (p.state == Crowd::entering) return lerp (settings.doorPan, spot, juce::jlimit (0.0f, 1.0f, (u - 0.3f) / 0.7f));
        if (p.state == Crowd::leaving)  return lerp (spot, settings.doorPan, juce::jlimit (0.0f, 1.0f, u / 0.6f));
        return spot;
    }

    void handleEvents (const Crowd::Events& ev, Telemetry& t) noexcept
    {
        for (int k = 0; k < Crowd::maxPeople; ++k)
        {
            const uint32_t bit = 1u << k;
            const int spot = crowd.people[(size_t) k].spot;
            if (ev.stepped & bit)
                steps[(size_t) k].step (crowd.people[(size_t) k].state == Crowd::entering && crowd.progress (k) < 0.4f ? 0.4f : 1.0f);
            if (ev.startedLeaving & bit)
            {
                voices[(size_t) k].setTalking (false);
                if (venue == Venue::toilets)
                {
                    // Urinals flush, the sink runs (someone washed their hands!).
                    if (spot < 3) water[(size_t) k].flush();
                    else          water[(size_t) k].tap();
                }
            }
            if (ev.puked & bit)
            {
                // Cartoon only: a synthesised puke never sounded like one. But everyone reacts.
                if (venue == Venue::terrace)
                {
                    float delay = 0.25f;
                    for (int j = 0; j < Crowd::maxPeople; ++j)
                    {
                        const auto& other = crowd.people[(size_t) j];
                        if (j == k || other.state != Crowd::present || other.sick)
                            continue;
                        voices[(size_t) j].setTalking (false);
                        if (noise.uniform() < 0.4f) voices[(size_t) j].laugh();
                        else                        voices[(size_t) j].exclaim ((int) (delay * sr));
                        delay += lerp (0.3f, 0.8f, noise.uniform());
                    }
                    talker = -1;
                    turnCountdown = 3.5f;   // a stunned pause before anyone picks the chat back up
                }
                t.pukeSpot.store (spot);
                t.pukes.fetch_add (1);
            }
        }
    }

    // Terrace small talk: one person talks at a time, others sometimes laugh.
    void converse (int len) noexcept
    {
        turnCountdown -= (float) len / sr;
        for (int k = 0; k < Crowd::maxPeople; ++k)
            if (crowd.people[(size_t) k].state != Crowd::present || crowd.people[(size_t) k].sick)
                voices[(size_t) k].setTalking (false);
        if (talker >= 0 && crowd.people[(size_t) talker].state != Crowd::present)
            talker = -1;

        if (turnCountdown > 0)
            return;

        int candidates[Crowd::maxPeople];
        int count = 0;
        for (int k = 0; k < Crowd::maxPeople; ++k)
            if (crowd.people[(size_t) k].state == Crowd::present && ! crowd.people[(size_t) k].sick && k != talker)
                candidates[count++] = k;

        if (talker >= 0)
        {
            voices[(size_t) talker].setTalking (false);
            if (count > 0 && noise.uniform() < 0.35f)
                voices[(size_t) candidates[(int) (noise.uniform() * (float) count * 0.999f)]].laugh();
        }

        if (crowd.countPresent() >= 2 && count > 0 && noise.uniform() > 0.1f)
        {
            talker = candidates[(int) (noise.uniform() * (float) count * 0.999f)];
            voices[(size_t) talker].setTalking (true);
            turnCountdown = lerp (1.8f, 4.5f, noise.uniform());
        }
        else
        {
            talker = -1;
            turnCountdown = lerp (0.3f, 1.0f, noise.uniform());
        }
    }

    // Other groups on the terrace: on and off, never quite silent for long.
    void murmurOn (int len) noexcept
    {
        for (size_t k = 0; k < murmur.size(); ++k)
        {
            murmurCountdown[k] -= (float) len / sr;
            if (murmurCountdown[k] > 0)
                continue;
            const bool talk = noise.uniform() < 0.7f;
            murmur[k].setTalking (talk);
            if (talk && noise.uniform() < 0.15f)
                murmur[k].laugh();
            murmurCountdown[k] = talk ? lerp (2.0f, 6.0f, noise.uniform()) : lerp (0.5f, 2.5f, noise.uniform());
        }
    }

    void updateFilters (float o) noexcept
    {
        const float fc = expLerp (settings.closedHz, settings.openHz, o);
        lpA.lowpass (fc, 0.6f);
        lpB.lowpass (fc * 1.35f, 0.75f);
        thump.bell (65.0f, 1.0f, settings.thumpDb * (1.0f - o));
        gain = dbToGain (lerp (settings.closedDb, settings.openDb, o));
        width = lerp (settings.closedWidth, settings.openWidth, o);
    }

    Venue venue;
    DoorSettings settings;
    RoomSpec spec;
    CrowdSettings crowdSpec;
    std::array<float, 5> spotPans;
    float ambienceLevel;
    ClubPA pa { queue };
    Svf lpA, lpB, thump, humLp, leak, cityLp, murmurLp;
    Door door;
    Impact slam;
    Noise noise { 404 };
    RoomStage space { queue };

    Crowd crowd;
    std::array<Footsteps, Crowd::maxPeople> steps;
    std::array<Babbler, Crowd::maxPeople> voices;
    std::array<Water, Crowd::maxPeople> water;
    std::array<Babbler, 3> murmur;
    std::array<float, 3> murmurCountdown {};
    const std::array<std::pair<float, float>, 3> murmurGains { panGains (-0.8f), panGains (0.9f), panGains (0.55f) };
    CityTraffic city;
    std::vector<float> cityL, cityR;
    float sr = 44100, gain = 1, width = 1, lastVibes = 0.5f, turnCountdown = 1;
    int talker = -1;
};

std::unique_ptr<Scene> makeToilets (juce::dsp::ConvolutionMessageQueue& q)
{
    DoorSettings d { 450.0f, 9000.0f, -13.0f, -7.0f, 0.15f, 0.6f, 5.0f, 0.35f, 0.55f, 0.2f, 0.035f, 0.78f, 0.11f, 0.46f };

    RoomSpec tiles;   // small, hard, bright
    tiles.lengthSec = 1.4f;   tiles.rt60 = 1.0f;       tiles.predelayMs = 2;
    tiles.erCount = 24;       tiles.erSpreadMs = 14;   tiles.erGain = 0.6f;
    tiles.brightStart = 9000; tiles.brightEnd = 3500;  tiles.highpass = 120;
    tiles.direct = 1;         tiles.wet = 0.45f;       tiles.width = 1;
    tiles.seed = 21;

    CrowdSettings people;
    people.stayMin = 12; people.stayMax = 40;
    people.arriveSlow = 45; people.arriveFast = 7;
    people.startPeople = 1; people.pukeChance = 0.08f;

    // Spots: three urinals, then the sink.
    return std::make_unique<BehindTheDoor> (q, Venue::toilets, d, tiles, people,
                                            std::array<float, 5> { -0.27f, -0.09f, 0.09f, -0.68f, 0.0f }, 0.0f);
}

std::unique_ptr<Scene> makeSmokingArea (juce::dsp::ConvolutionMessageQueue& q)
{
    DoorSettings d { 170.0f, 7000.0f, -18.0f, -12.0f, 0.1f, 0.35f, 5.0f, 0.45f, 0.6f, 0.06f, 0.012f, 0.31f, 0.16f, -0.2f };

    RoomSpec outside;   // open air, a building across the alley throwing back slaps
    outside.lengthSec = 2.0f;   outside.rt60 = 1.3f;      outside.predelayMs = 3;
    outside.erCount = 5;        outside.erSpreadMs = 40;  outside.erGain = 0.25f;
    outside.brightStart = 5000; outside.brightEnd = 1200; outside.highpass = 80;
    outside.direct = 1;         outside.wet = 0.22f;      outside.width = 1;
    outside.echoes = { { 68, 0.35f }, { 141, 0.18f }, { 233, 0.1f }, { 390, 0.06f } };
    outside.echoCutoff = 3500;  outside.seed = 31;

    CrowdSettings people;
    people.stayMin = 40; people.stayMax = 120;
    people.arriveSlow = 35; people.arriveFast = 7;
    people.startPeople = 3;
    people.sickVisitors = true;

    return std::make_unique<BehindTheDoor> (q, Venue::terrace, d, outside, people,
                                            std::array<float, 5> { 0.18f, 0.41f, 0.64f, 0.86f, -0.75f }, 0.02f);
}

//==============================================================================
class InTheCar : public Scene
{
public:
    using Scene::Scene;

    void prepare (double sampleRate, int maxBlock) override
    {
        sr = (float) sampleRate;
        prepareAll (sampleRate, { &inHp, &cabinGain, &doorPanel, &seatDip, &midScoop, &tweeter, &inLp,
                                  &outHp, &outLpA, &outLpB, &boom, &idleLp, &cityLpA, &cityLpB });
        inHp.highpass (25.0f, 0.7f);
        cabinGain.lowShelf (65.0f, 0.8f, 9.0f);   // small sealed cabin: huge low-end lift
        doorPanel.bell (130.0f, 1.4f, 3.0f);      // woofers in flexy door panels
        seatDip.bell (320.0f, 1.2f, -4.0f);       // seats and bodies soak up the low mids
        midScoop.bell (900.0f, 1.0f, -1.5f);
        tweeter.bell (4200.0f, 2.0f, 4.0f);       // dash tweeters firing at the glass
        inLp.lowpass (12000.0f, 0.7f);

        outHp.highpass (30.0f, 0.7f);
        outLpA.lowpass (200.0f, 0.6f);
        outLpB.lowpass (480.0f, 0.7f);
        boom.bell (58.0f, 1.1f, 7.0f);
        idleLp.lowpass (110.0f, 0.7f);
        cityLpA.lowpass (380.0f, 0.6f);   // the street through closed windows
        cityLpB.lowpass (700.0f, 0.7f);

        side.prepare (sampleRate, 0.55f, 0.55f);
        side.reset();
        city.prepare (sampleRate, 81);

        taps.prepare ((int) (0.005f * sr));
        windscreenL = (int) (0.0011f * sr);
        windscreenR = (int) (0.0016f * sr);
        driverSeat = (int) (0.0013f * sr);   // you sit nearer the left speakers

        RoomSpec cabinSpec;
        cabinSpec.lengthSec = 0.25f;  cabinSpec.rt60 = 0.12f;     cabinSpec.predelayMs = 0.5f;
        cabinSpec.erCount = 16;       cabinSpec.erSpreadMs = 5;   cabinSpec.erGain = 0.5f;
        cabinSpec.brightStart = 7000; cabinSpec.brightEnd = 2500; cabinSpec.highpass = 60;
        cabinSpec.direct = 1;         cabinSpec.wet = 0.3f;       cabinSpec.width = 0.9f;
        cabinSpec.seed = 41;
        cabin.prepare (sampleRate, maxBlock, cabinSpec);

        RoomSpec streetSpec;
        streetSpec.lengthSec = 1.2f;   streetSpec.rt60 = 0.7f;      streetSpec.predelayMs = 2;
        streetSpec.erCount = 4;        streetSpec.erSpreadMs = 20;  streetSpec.erGain = 0.2f;
        streetSpec.brightStart = 3000; streetSpec.brightEnd = 900;  streetSpec.highpass = 50;
        streetSpec.direct = 1;         streetSpec.wet = 0.2f;       streetSpec.width = 1;
        streetSpec.echoes = { { 35, 0.3f }, { 80, 0.15f } };
        streetSpec.seed = 43;
        street.prepare (sampleRate, maxBlock, streetSpec);

        for (auto* b : { &outL, &outR, &cityL, &cityR })
            b->assign ((size_t) maxBlock, 0.0f);
    }

    void reset() override
    {
        resetAll ({ &inHp, &cabinGain, &doorPanel, &seatDip, &midScoop, &tweeter, &inLp,
                    &outHp, &outLpA, &outLpB, &boom, &idleLp, &cityLpA, &cityLpB });
        side.reset(); cabin.reset(); street.reset(); city.reset(); taps.reset();
    }

    void process (float* l, float* r, int n, const Controls& c, Telemetry& t) override
    {
        // Outside path works on its own copy: mono, through the car body.
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            const float body = boom.process (0, outLpB.process (0, outLpA.process (0, outHp.process (0, m))));
            outL[(size_t) i] = outR[(size_t) i] = body * 0.75f;
        }
        street.process (outL.data(), outR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float xl = inLp.process (0, tweeter.process (0, midScoop.process (0, seatDip.process (0,
                             doorPanel.process (0, cabinGain.process (0, inHp.process (0, l[i])))))));
            const float xr = inLp.process (1, tweeter.process (1, midScoop.process (1, seatDip.process (1,
                             doorPanel.process (1, cabinGain.process (1, inHp.process (1, r[i])))))));
            taps.push (xl, xr);
            // Windscreen bounce, and the far speaker arriving a touch later and quieter.
            l[i] = xl + 0.3f * taps.read (0, windscreenL);
            r[i] = 0.75f * (taps.read (1, driverSeat) + 0.3f * taps.read (1, driverSeat + windscreenR));
        }
        cabin.process (l, r, n);

        std::fill (cityL.begin(), cityL.begin() + n, 0.0f);
        std::fill (cityR.begin(), cityR.begin() + n, 0.0f);
        city.process (cityL.data(), cityR.data(), n, c.vibes);
        juce::FloatVectorOperations::multiply (cityL.data(), 0.5f, n);
        juce::FloatVectorOperations::multiply (cityR.data(), 0.5f, n);

        const float idleLevel = smoothstep (c.vibes * 5.0f);
        for (int i = 0; i < n; ++i)
        {
            side.process (c.action);
            const float o = side.openness();
            const float gIn = std::cos (o * 0.5f * pi), gOut = std::sin (o * 0.5f * pi);

            // Our own engine ticking over.
            idlePhase += 26.0f * (1.0f + 0.02f * noise.white()) / sr;
            idlePhase -= std::floor (idlePhase);
            const float idle = idleLp.process (0, 2.0f * idlePhase - 1.0f) * idleLevel * lerp (0.03f, 0.018f, o);

            const float cl = lerp (cityLpB.process (0, cityLpA.process (0, cityL[(size_t) i])) * 0.14f, cityL[(size_t) i], o);
            const float cr = lerp (cityLpB.process (1, cityLpA.process (1, cityR[(size_t) i])) * 0.14f, cityR[(size_t) i], o);

            l[i] = l[i] * gIn + outL[(size_t) i] * gOut + cl + idle;
            r[i] = r[i] * gIn + outR[(size_t) i] * gOut + cr + idle;
        }

        t.openness.store (side.openness());
        for (int k = 0; k < CityTraffic::maxCars; ++k)
        {
            const auto& car = city.cars[(size_t) k];
            auto& view = t.cars[(size_t) k];
            view.active.store (car.active);
            view.x.store (car.x);
            view.dir.store (car.v > 0 ? 1 : -1);
            view.colour.store (car.colour);
            view.honking.store (car.honkLeft > 0);
        }
    }

private:
    Svf inHp, cabinGain, doorPanel, seatDip, midScoop, tweeter, inLp, outHp, outLpA, outLpB, boom, idleLp, cityLpA, cityLpB;
    Door side;   // not a slamming door here, just "in" <-> "out"
    CityTraffic city;
    TapDelay taps;
    Noise noise { 909 };
    RoomStage cabin { queue }, street { queue };
    std::vector<float> outL, outR, cityL, cityR;
    float sr = 44100, idlePhase = 0;
    int windscreenL = 50, windscreenR = 70, driverSeat = 60;
};
//==============================================================================
class PhoneSpeaker : public Scene
{
public:
    using Scene::Scene;

    void prepare (double sampleRate, int maxBlock) override
    {
        sr = (float) sampleRate;
        prepareAll (sampleRate, { &hpA, &hpB, &body, &presence, &lp, &postLp, &table,
                                  &pocketA, &pocketB });
        hpA.highpass (480.0f, 0.9f);   // tiny speaker: nothing below ~500 Hz
        hpB.highpass (480.0f, 0.6f);
        body.bell (950.0f, 1.0f, 2.0f);
        presence.bell (3300.0f, 1.6f, 4.5f);
        lp.lowpass (7500.0f, 0.7f);
        postLp.lowpass (9500.0f, 0.7f);
        table.bell (190.0f, 2.0f, 4.0f);   // the table resonates a little
        pocketA.lowpass (1100.0f, 0.7f);
        pocketB.lowpass (1800.0f, 0.5f);

        limiter.prepare (sampleRate, 2.0f, 90.0f);
        pocket.prepare (sampleRate, 0.5f, 0.5f);
        pocket.reset();

        pingPhase1 = pingPhase2 = 0;
        pingEnv1 = pingEnv2 = 0;
        pingDecay1 = std::exp (-1.0f / (0.09f * sr));
        pingDecay2 = std::exp (-1.0f / (0.25f * sr));
        pingCountdown = (int) (6.0f * sr);
        secondTone = -1;

        RoomSpec kitchen;
        kitchen.lengthSec = 0.8f;   kitchen.rt60 = 0.45f;     kitchen.predelayMs = 1;
        kitchen.erCount = 10;       kitchen.erSpreadMs = 18;  kitchen.erGain = 0.45f;
        kitchen.brightStart = 7000; kitchen.brightEnd = 2500; kitchen.highpass = 150;
        kitchen.direct = 1;         kitchen.wet = 0.3f;       kitchen.width = 1;
        kitchen.seed = 51;
        room.prepare (sampleRate, maxBlock, kitchen);
    }

    void reset() override
    {
        resetAll ({ &hpA, &hpB, &body, &presence, &lp, &postLp, &table, &pocketA, &pocketB });
        limiter.reset(); pocket.reset(); room.reset();
        pingEnv1 = pingEnv2 = 0; secondTone = -1;
    }

    void process (float* l, float* r, int n, const Controls& c, Telemetry& t) override
    {
        maybeNotify (n, c.vibes, t);
        const float drive = 1.6f + 3.0f * c.vibes;

        for (int i = 0; i < n; ++i)
        {
            float x = 0.5f * (l[i] + r[i]) + nextPing();
            x = lp.process (0, presence.process (0, body.process (0, hpB.process (0, hpA.process (0, x)))));

            // Phones squash everything: fast limiter, then the speaker strains.
            const float envDb = juce::Decibels::gainToDecibels (limiter.process (x), -100.0f);
            const float overDb = envDb + 22.0f;
            const float grDb = overDb > 0 ? overDb / 4.0f - overDb : 0.0f;
            x *= dbToGain (grDb + 6.0f);
            x = std::tanh (drive * x) / drive * 1.4f;
            x = table.process (0, postLp.process (0, x));

            pocket.process (c.action);
            const float o = pocket.openness();
            const float muffled = pocketB.process (0, pocketA.process (0, x)) * 0.7f;
            x = lerp (x, muffled, o);

            l[i] = r[i] = x;
        }

        room.process (l, r, n);
        t.openness.store (pocket.openness());
    }

private:
    void maybeNotify (int n, float vibes, Telemetry& t) noexcept
    {
        if (vibes < 0.05f)
            return;
        pingCountdown -= n;
        if (pingCountdown <= 0)
        {
            pingEnv1 = 0.12f;
            pingPhase1 = 0;
            secondTone = (int) (0.11f * sr);
            pingCountdown = (int) (lerp (40.0f, 5.0f, vibes) * (0.6f + 0.8f * noise.uniform()) * sr);
            t.events.fetch_add (1);
        }
    }

    float nextPing() noexcept
    {
        if (secondTone >= 0 && secondTone-- == 0)
        {
            pingEnv2 = 0.1f;
            pingPhase2 = 0;
        }
        if (pingEnv1 < 1.0e-5f && pingEnv2 < 1.0e-5f)
            return 0;

        const float y = std::sin (pingPhase1) * pingEnv1 + std::sin (pingPhase2) * pingEnv2;
        pingPhase1 += 2.0f * pi * 1318.5f / sr;   // E6
        pingPhase2 += 2.0f * pi * 1975.5f / sr;   // B6
        pingEnv1 *= pingDecay1;
        pingEnv2 *= pingDecay2;
        return y;
    }

    Svf hpA, hpB, body, presence, lp, postLp, table, pocketA, pocketB;
    Follower limiter;
    Door pocket;
    Noise noise { 515 };
    RoomStage room { queue };
    float sr = 44100;
    float pingPhase1 = 0, pingPhase2 = 0, pingEnv1 = 0, pingEnv2 = 0, pingDecay1 = 0, pingDecay2 = 0;
    int pingCountdown = 0, secondTone = -1;
};

//==============================================================================
class Festival : public Scene
{
public:
    using Scene::Scene;

    void prepare (double sampleRate, int maxBlock) override
    {
        sr = (float) sampleRate;
        prepareAll (sampleRate, { &hp, &subs, &airA, &airB, &carry, &rumble, &whoosh });
        hp.highpass (35.0f, 0.7f);
        subs.lowShelf (80.0f, 0.7f, 3.0f);
        carry.lowShelf (130.0f, 0.7f, 3.0f);   // the bass is what makes it to the tents
        rumble.lowpass (220.0f, 0.7f);
        whoosh.bandpass (650.0f, 0.7f);

        wind.prepare (sampleRate, 2.5f, 71);
        wobble.prepare (sampleRate, 1.3f, 73);
        gusts.prepare (sampleRate, 75);
        delay.prepare ((int) (0.02f * sr));
        gust = 0;
        gustCountdown = 0;
        gustUp   = 1.0f - std::exp (-1.0f / (0.5f * sr));
        gustDown = 1.0f - std::exp (-1.0f / (2.5f * sr));
        gainSmooth = 1;

        RoomSpec valley;
        valley.lengthSec = 3.2f;   valley.rt60 = 2.6f;      valley.predelayMs = 20;
        valley.erCount = 0;        valley.erSpreadMs = 80;  valley.erGain = 0;
        valley.brightStart = 3000; valley.brightEnd = 800;  valley.highpass = 100;
        valley.direct = 1;         valley.wet = 0.3f;       valley.width = 1;
        valley.echoes = { { 190, 0.32f }, { 340, 0.22f }, { 560, 0.14f }, { 820, 0.08f }, { 1150, 0.05f } };
        valley.echoCutoff = 1800;  valley.seed = 61;
        hills.prepare (sampleRate, maxBlock, valley);
        updateAir (0, 0);
    }

    void reset() override
    {
        resetAll ({ &hp, &subs, &airA, &airB, &carry, &rumble, &whoosh });
        delay.reset(); gusts.reset(); hills.reset();
        gust = 0; gustCountdown = 0; lastAction = false; gainSmooth = 1;
    }

    void process (float* l, float* r, int n, const Controls& c, Telemetry& t) override
    {
        if (c.action && ! lastAction)
        {
            gustCountdown = (int) (1.6f * sr);
            t.events.fetch_add (1);
        }
        lastAction = c.action;

        for (int start = 0; start < n; start += controlChunk)
        {
            const int len = std::min (controlChunk, n - start);
            const float windAmt = juce::jlimit (0.0f, 1.0f, 0.5f + 0.7f * wind.advance (len)) * c.vibes;
            const bool autoGust = gusts.process (len, std::max (0.0f, c.vibes - 0.45f) * 0.6f, 1.0f, 2.5f);
            updateAir (windAmt, gust);
            const float targetGain = dbToGain (-4.5f * windAmt - 8.0f * gust);

            for (int i = start; i < start + len; ++i)
            {
                const bool gusting = gustCountdown > 0 || autoGust;
                if (gustCountdown > 0)
                    --gustCountdown;
                gust += ((gusting ? 1.0f : 0.0f) - gust) * (gusting ? gustUp : gustDown);
                gainSmooth += (targetGain - gainSmooth) * 0.002f;

                float x = 0.5f * (l[i] + r[i]);
                x = carry.process (0, airB.process (0, airA.process (0, subs.process (0, hp.process (0, x)))));

                // The wind bends the sound: a slow, tiny pitch wander.
                const float d = (0.0016f + 0.0005f * wobble.process() * c.vibes + 0.0007f * gust) * sr;
                const float yl = delay.process (0, x, d) * gainSmooth;
                const float yr = delay.process (1, x, d) * gainSmooth;

                const float windLevel = 0.004f * c.vibes * (0.4f + windAmt) + 0.05f * gust;
                const float nl = noise.pink(), nr = noiseR.pink();
                l[i] = yl + windLevel * (0.6f * rumble.process (0, nl) + 0.4f * whoosh.process (0, nl));
                r[i] = yr + windLevel * (0.6f * rumble.process (1, nr) + 0.4f * whoosh.process (1, nr));
            }
            t.wind.store (juce::jlimit (0.0f, 1.0f, windAmt + gust));
        }

        hills.process (l, r, n);
        t.openness.store (0);
    }

private:
    void updateAir (float windAmt, float gustAmt) noexcept
    {
        const float fc = 2400.0f * (1.0f - 0.35f * windAmt - 0.4f * gustAmt);
        airA.lowpass (fc, 0.55f);
        airB.lowpass (fc * 1.7f, 0.7f);
    }

    Svf hp, subs, airA, airB, carry, rumble, whoosh;
    Drift wind, wobble;
    Traffic gusts;
    WobbleDelay delay;
    Noise noise { 616 }, noiseR { 717 };
    RoomStage hills { queue };
    float sr = 44100, gust = 0, gustUp = 0, gustDown = 0, gainSmooth = 1;
    int gustCountdown = 0;
    bool lastAction = false;
};

} // namespace

//==============================================================================
std::unique_ptr<Scene> createScene (int index, juce::dsp::ConvolutionMessageQueue& q)
{
    switch (index)
    {
        case 0:  return std::make_unique<NextDoor> (q);
        case 1:  return makeToilets (q);
        case 2:  return makeSmokingArea (q);
        case 3:  return std::make_unique<InTheCar> (q);
        case 4:  return std::make_unique<PhoneSpeaker> (q);
        default: return std::make_unique<Festival> (q);
    }
}

float sceneMakeupDb (int index)
{
    static constexpr float table[numScenes] { -8.6f, 2.2f, 10.1f, -4.0f, -4.6f, -6.3f };
    return table[juce::jlimit (0, numScenes - 1, index)];
}

} // namespace elsewhere
