#pragma once

#include "DSP.h"

// Everything that happens *around* the music: knuckles, rattles, footsteps,
// people chatting, plumbing and passing traffic. All synthesised,
// all allocation-free once prepared.
namespace elsewhere
{
//==============================================================================
// Two-pole resonator: an impulse in gives a decaying sine of unit amplitude.
class Resonator
{
public:
    void set (double sampleRate, float freq, float decaySec) noexcept
    {
        const float w = 2.0f * pi * freq / (float) sampleRate;
        const float r = std::exp (-1.0f / (decaySec * (float) sampleRate));
        a1 = 2.0f * r * std::cos (w);
        a2 = -r * r;
        norm = std::sin (w);
    }

    float process (float x) noexcept
    {
        const float y = a1 * y1 + a2 * y2 + x * norm;
        y2 = y1;
        y1 = y;
        return y;
    }

    void reset() noexcept { y1 = y2 = 0; }

private:
    float a1 = 0, a2 = 0, norm = 0, y1 = 0, y2 = 0;
};

struct Mode { float freq, decay, gain; };

// A struck object: a handful of resonant modes (wall, frame, tiles, metal).
template <int N>
class ModalBody
{
public:
    void prepare (double sampleRate, const std::array<Mode, N>& modes) noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            res[(size_t) i].set (sampleRate, modes[(size_t) i].freq, modes[(size_t) i].decay);
            gains[(size_t) i] = modes[(size_t) i].gain;
        }
    }

    float process (float excite) noexcept
    {
        float s = 0;
        for (int i = 0; i < N; ++i)
            s += gains[(size_t) i] * res[(size_t) i].process (excite);
        return s;
    }

    void reset() noexcept
    {
        for (auto& r : res)
            r.reset();
    }

private:
    std::array<Resonator, N> res;
    std::array<float, N> gains {};
};

// The thing doing the hitting: a short half-sine contact plus a bit of grit.
class Striker
{
public:
    void prepare (double sampleRate, float contactMs, float grit) noexcept
    {
        contact = std::max (2, (int) (contactMs * 0.001f * (float) sampleRate));
        noiseAmount = grit;
        reset();
    }

    void trigger (float gain, int delaySamples = 0) noexcept
    {
        for (auto& p : pending)
            if (p.delay < 0)
            {
                p.delay = delaySamples;
                p.gain = gain;
                return;
            }
    }

    float next() noexcept
    {
        for (auto& p : pending)
            if (p.delay >= 0 && p.delay-- == 0)
            {
                pos = 0;
                gain = p.gain;
                p.delay = -1;
            }

        if (pos >= contact)
            return 0;
        const float t = (float) pos / (float) contact;
        ++pos;
        // Normalised by area, so a hit excites each mode by roughly `gain`
        // whatever the contact time (longer contact = softer, darker hit).
        const float area = 0.6366f * (float) contact;
        return gain * (std::sin (pi * t) / area + noiseAmount * noise.white() * (1.0f - t) / std::sqrt ((float) contact));
    }

    void reset() noexcept
    {
        pos = contact;
        for (auto& p : pending)
            p.delay = -1;
    }

private:
    struct Pending { int delay = -1; float gain = 0; };
    std::array<Pending, 8> pending;
    Noise noise { 31337 };
    int contact = 10, pos = 10;
    float gain = 0, noiseAmount = 0;
};

//==============================================================================
// Knuckles on a door: a dull thud, a woody "tok" and a tiny click. Everything is
// heavily damped (no ringing modes), which is what keeps it from sounding like glass.
class Knocker
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = (float) sampleRate;
        for (auto* f : { &hollow, &tok, &click })
            f->setSampleRate (sampleRate);
        hollow.bandpass (190.0f, 1.6f);
        tok.bandpass (560.0f, 1.0f);
        click.bandpass (2600.0f, 0.8f);
        thumpDecay = std::exp (-1.0f / (0.04f * sr));
        burstDecay = std::exp (-1.0f / (0.006f * sr));
        clickDecay = std::exp (-1.0f / (0.0015f * sr));
        sweepDecay = std::exp (-1.0f / (0.006f * sr));
        reset();
    }

    // Knock-knock-knock, with human timing.
    void knockThreeTimes() noexcept
    {
        float t = 0;
        const float gains[] = { 0.85f, 0.75f, 1.0f };
        for (int i = 0; i < 3; ++i)
        {
            pending[(size_t) i] = { (int) (t * sr), gains[i] * (0.9f + 0.2f * noise.uniform()) };
            t += 0.16f + 0.04f * noise.uniform();
        }
    }

    float process() noexcept
    {
        for (auto& p : pending)
            if (p.delay >= 0 && p.delay-- == 0)
            {
                thumpEnv = burstEnv = clickEnv = p.gain;
                sweep = 1;
                phase = 0;
                p.delay = -1;
            }

        if (thumpEnv < 1.0e-5f)
            return 0;

        // Thud: a low sine that sags in pitch as the knuckle lands.
        phase += 2.0f * pi * (105.0f + 120.0f * sweep) / sr;
        const float thud = std::sin (phase) * thumpEnv;
        const float n = noise.white();
        const float body = hollow.process (0, n * burstEnv) * 2.4f + tok.process (0, n * burstEnv) * 1.1f;
        const float tick = click.process (0, n) * clickEnv * 0.25f;

        thumpEnv *= thumpDecay;
        burstEnv *= burstDecay;
        clickEnv *= clickDecay;
        sweep *= sweepDecay;
        return (thud * 0.75f + body + tick) * 0.8f;
    }

    void reset() noexcept
    {
        for (auto* f : { &hollow, &tok, &click })
            f->reset();
        for (auto& p : pending)
            p.delay = -1;
        thumpEnv = burstEnv = clickEnv = 0;
    }

private:
    struct Pending { int delay = -1; float gain = 0; };
    std::array<Pending, 3> pending;
    Svf hollow, tok, click;
    Noise noise { 4242 };
    float sr = 44100, phase = 0, sweep = 0, thumpEnv = 0, burstEnv = 0, clickEnv = 0;
    float thumpDecay = 0, burstDecay = 0, clickDecay = 0, sweepDecay = 0;
};

//==============================================================================
// Simlish. A breathy glottal pulse through four formants, strung into syllables
// with nasals, stops and the odd "s", and phrases with a falling (or questioning)
// melody. Pitch wobbles and drifts like a real voice. Can also laugh, and shout
// through walls.
class Babbler
{
public:
    void prepare (double sampleRate, float basePitch, uint32_t seed) noexcept
    {
        sr = (float) sampleRate;
        base = basePitch;
        noise = Noise (seed);
        for (auto* f : allFilters())
            f->setSampleRate (sampleRate);
        fricative.bandpass (4800.0f, 1.4f);
        burstBp.bandpass (1800.0f, 0.8f);
        driftLp.lowpass (1.5f, 0.7f);
        formantGlide = 1.0f - std::exp (-16.0f / (0.035f * sr));   // updated every 16 samples
        pitchGlide = 1.0f - std::exp (-1.0f / (0.06f * sr));
        envAttack = 1.0f - std::exp (-1.0f / (0.014f * sr));
        envRelease = 1.0f - std::exp (-1.0f / (0.045f * sr));
        noiseSmooth = 1.0f - std::exp (-1.0f / (0.008f * sr));
        burstDecay = std::exp (-1.0f / (0.005f * sr));
        vibratoRate = lerp (4.5f, 6.0f, noise.uniform());
        reset();
    }

    void reset() noexcept
    {
        talking = false;
        laughsLeft = syllablesLeft = 0;
        exclaimPending = false;
        phase = vibratoPhase = 0;
        env = envTarget = asp = aspTarget = fric = fricTarget = burst = 0;
        stage = Stage::silent;
        countdown = 0;
        for (auto* f : allFilters())
            f->reset();
        setFormants (vowels[5], true);
    }

    void setTalking (bool shouldTalk) noexcept { talking = shouldTalk; }
    void laugh() noexcept { laughsLeft = 4 + (int) (noise.uniform() * 3.0f); syllablesLeft = 0; countdown = 0; }

    // One long, disgusted "EEEW!" / "OOOH!", after a little delay.
    void exclaim (int delaySamples) noexcept { exclaimPending = true; syllablesLeft = 0; countdown = std::max (1, delaySamples); }
    bool isLaughing() const noexcept { return laughsLeft > 0; }
    bool isTalking() const noexcept { return talking; }
    bool isBusy() const noexcept { return talking || laughsLeft > 0 || stage != Stage::silent; }

    void setLoudness (float shout) noexcept { loud = shout; }

    float process() noexcept
    {
        if (--countdown <= 0)
            advance();

        env  += (envTarget - env) * (envTarget > env ? envAttack : envRelease);
        asp  += (aspTarget - asp) * noiseSmooth;
        fric += (fricTarget - fric) * noiseSmooth;
        if (stage == Stage::silent && env < 1.0e-4f && asp < 1.0e-4f && fric < 1.0e-4f && burst < 1.0e-4f)
            return 0;

        if (++controlCounter >= 16)
        {
            controlCounter = 0;
            for (int i = 0; i < 4; ++i)
                fc[i] += (target[i] - fc[i]) * formantGlide;
            f1.bandpass (fc[0], fc[0] / 80.0f);
            f2.bandpass (fc[1], fc[1] / 100.0f);
            f3.bandpass (fc[2], fc[2] / 140.0f);
            f4.bandpass (fc[3], fc[3] / 220.0f);
            vibratoPhase += 16.0f * vibratoRate / sr;
            vibratoPhase -= std::floor (vibratoPhase);
            drift = driftLp.process (0, noise.white()) * 25.0f;
        }

        // Pitch: a gliding melody, a little vibrato, slow drift, and per-cycle jitter.
        pitch += (pitchTarget - pitch) * pitchGlide;
        const float f0 = pitch * (1.0f + 0.011f * std::sin (2.0f * pi * vibratoPhase) + 0.02f * drift) * cycleJitter;
        phase += f0 / sr;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            cycleJitter = 1.0f + 0.005f * noise.white();
            shimmer = 1.0f + 0.07f * noise.white();
        }

        // Glottal flow derivative (Rosenberg pulse): smooth opening, sharp closing, then closed.
        constexpr float open = 0.42f, close = 0.11f;
        float glottal = 0;
        if (phase < open)               glottal =  std::sin (pi * phase / open) * (0.5f * pi / open);
        else if (phase < open + close)  glottal = -std::sin (0.5f * pi * (phase - open) / close) * (0.5f * pi / close);
        const bool flowing = phase < open + close;

        const float n = noise.white();
        const float src = env * shimmer * glottal * 0.16f + asp * n * (flowing ? 1.0f : 0.35f);
        float y = f1.process (0, src) + 0.8f * f2.process (0, src) + 0.75f * f3.process (0, src) + 0.45f * f4.process (0, src);

        y += fric * fricative.process (0, n) * 0.6f;
        if (burst > 1.0e-4f)
        {
            y += burst * burstBp.process (0, n);
            burst *= burstDecay;
        }
        return y * 0.72f * (1.0f + loud);
    }

private:
    enum class Stage { silent, closure, fricative, nasal, vowel, gap, laughH, laughA };

    struct Formants { float f[4]; };
    static constexpr Formants vowels[7] = {
        {{ 750, 1250, 2550, 3500 }},   // a
        {{ 500, 1800, 2550, 3500 }},   // e
        {{ 320, 2200, 2950, 3600 }},   // i
        {{ 520,  900, 2450, 3400 }},   // o
        {{ 350,  850, 2350, 3300 }},   // u
        {{ 620, 1200, 2450, 3400 }},   // uh
        {{ 680, 1550, 2500, 3500 }},   // ae
    };
    static constexpr Formants nasalM {{ 260, 1000, 2300, 3300 }}, nasalN {{ 260, 1600, 2500, 3400 }};

    std::array<Svf*, 7> allFilters() noexcept { return { &f1, &f2, &f3, &f4, &fricative, &burstBp, &driftLp }; }

    void setFormants (const Formants& v, bool instant) noexcept
    {
        // Each voice has its own vocal tract length.
        const float scale = base > 160.0f ? 1.12f : 1.0f;
        for (int i = 0; i < 4; ++i)
        {
            target[i] = v.f[i] * scale;
            if (instant)
                fc[i] = target[i];
        }
    }

    int samples (float seconds) const noexcept { return std::max (1, (int) (seconds * sr)); }
    float rand (float lo, float hi) noexcept { return lerp (lo, hi, noise.uniform()); }

    void voice (float level, float breath) noexcept { envTarget = level; aspTarget = breath; fricTarget = 0; }
    void quiet() noexcept { envTarget = 0; aspTarget = 0; fricTarget = 0; }

    void advance() noexcept
    {
        if (laughsLeft > 0)
        {
            if (stage == Stage::laughH)
            {
                // "HA": breathy and high, tumbling down.
                stage = Stage::laughA;
                setFormants (vowels[0], false);
                pitchTarget = pitch = base * (1.8f - 0.09f * (float) (6 - laughsLeft));
                voice (1.0f, 0.06f);
                countdown = samples (rand (0.08f, 0.12f));
            }
            else
            {
                if (stage == Stage::laughA)
                    --laughsLeft;
                stage = Stage::laughH;
                envTarget = 0;
                aspTarget = 0.22f;
                countdown = samples (rand (0.05f, 0.08f));
                if (laughsLeft == 0)
                {
                    stage = Stage::gap;
                    quiet();
                    countdown = samples (0.35f);
                }
            }
            return;
        }

        switch (stage)
        {
            case Stage::silent:
            case Stage::gap:
            case Stage::laughH:
            case Stage::laughA:
                if (exclaimPending)
                {
                    exclaimPending = false;
                    stage = Stage::vowel;
                    setFormants (vowels[noise.uniform() < 0.5f ? 2 : 4], false);
                    pitchTarget = base * rand (1.6f, 1.9f);
                    voice (1.0f, 0.08f);
                    countdown = samples (rand (0.35f, 0.55f));
                    syllablesLeft = 0;
                    return;
                }
                if (! talking)
                {
                    stage = Stage::silent;
                    quiet();
                    countdown = samples (0.05f);
                    return;
                }
                if (syllablesLeft <= 0)
                {
                    // New phrase: where the melody starts, how long it runs, statement or question.
                    syllablesLeft = 3 + (int) (noise.uniform() * 8.0f);
                    phraseLength = syllablesLeft;
                    phraseStart = base * rand (1.05f, 1.3f);
                    question = noise.uniform() < 0.2f;
                }
                startConsonant();
                return;

            case Stage::closure:
                // Release the stop: a little puff, straight into the vowel.
                burst = rand (0.08f, 0.16f);
                startVowel();
                return;

            case Stage::fricative:
            case Stage::nasal:
                startVowel();
                return;

            case Stage::vowel:
                stage = Stage::gap;
                if (syllablesLeft > 0)
                {
                    // Keep the voice ticking over between syllables, like connected speech.
                    voice (0.25f, 0.02f);
                    countdown = samples (rand (0.015f, 0.045f));
                }
                else
                {
                    quiet();
                    countdown = samples (rand (0.3f, 0.8f));
                }
                return;
        }
    }

    void startConsonant() noexcept
    {
        const float r = noise.uniform();
        if (r < 0.3f)
        {
            // m / n: voiced, dark, closed mouth.
            stage = Stage::nasal;
            setFormants (noise.uniform() < 0.5f ? nasalM : nasalN, false);
            voice (0.4f, 0.01f);
            countdown = samples (rand (0.04f, 0.08f));
        }
        else if (r < 0.6f)
        {
            // b / d / g / k: a short silence, then a pop.
            stage = Stage::closure;
            quiet();
            countdown = samples (rand (0.03f, 0.06f));
        }
        else if (r < 0.72f)
        {
            // s / sh, kept soft.
            stage = Stage::fricative;
            envTarget = 0;
            aspTarget = 0;
            fricTarget = rand (0.03f, 0.07f);
            fricative.bandpass (noise.uniform() < 0.5f ? 4800.0f : 3000.0f, 1.4f);
            countdown = samples (rand (0.05f, 0.09f));
        }
        else
            startVowel();   // a vowel straight after the last one, or an "h" glide
    }

    void startVowel() noexcept
    {
        stage = Stage::vowel;
        setFormants (vowels[(int) (noise.uniform() * 6.99f)], false);

        const float progress = 1.0f - (float) syllablesLeft / (float) std::max (1, phraseLength);
        const bool stressed = noise.uniform() < 0.3f;
        float contour = 1.0f - 0.2f * progress;                       // statements drift down
        if (question && syllablesLeft <= 2) contour = 1.15f + 0.15f * (float) (2 - syllablesLeft);
        pitchTarget = phraseStart * contour * (stressed ? 1.12f : rand (0.95f, 1.03f));

        voice (stressed ? 1.0f : rand (0.6f, 0.85f), 0.025f);
        countdown = samples (stressed ? rand (0.13f, 0.22f) : rand (0.07f, 0.15f));
        --syllablesLeft;
    }

    Svf f1, f2, f3, f4, fricative, burstBp, driftLp;
    Noise noise;
    Stage stage = Stage::silent;
    float sr = 44100, base = 120, pitch = 120, pitchTarget = 120, phase = 0, vibratoPhase = 0, vibratoRate = 5;
    float drift = 0, cycleJitter = 1, shimmer = 1;
    float env = 0, envTarget = 0, asp = 0, aspTarget = 0, fric = 0, fricTarget = 0, burst = 0;
    float fc[4] { 620, 1200, 2450, 3400 }, target[4] { 620, 1200, 2450, 3400 };
    float formantGlide = 0.1f, pitchGlide = 0.001f, envAttack = 0.01f, envRelease = 0.01f, noiseSmooth = 0.01f, burstDecay = 0.99f;
    float loud = 0, phraseStart = 120;
    int countdown = 0, syllablesLeft = 0, phraseLength = 1, laughsLeft = 0, controlCounter = 0;
    bool talking = false, question = false, exclaimPending = false;
};

//==============================================================================
// Running water: urinal flushes, taps.
class Water
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = (float) sampleRate;
        band.setSampleRate (sampleRate);
        hp.setSampleRate (sampleRate);
        hp.highpass (250.0f, 0.7f);
        gurgle.prepare (sampleRate, 0.06f, 818);
        reset();
    }

    void reset() noexcept { remaining = 0; length = 1; band.reset(); hp.reset(); }

    // A flush: a gush that sweeps up and gurgles. A tap: a steady hiss.
    void flush() noexcept { start (3.2f, 450.0f, 1300.0f, 0.9f, 1.0f); }
    void tap() noexcept   { start (2.6f, 2600.0f, 2900.0f, 0.25f, 0.4f); }

    float process() noexcept
    {
        if (remaining <= 0)
            return 0;
        const float u = 1.0f - (float) remaining / (float) length;
        --remaining;
        if (++counter >= 32)
        {
            counter = 0;
            band.bandpass (lerp (fromHz, toHz, std::sqrt (u)) * (1.0f + 0.25f * gurgleDepth * gurgleValue), 0.9f);
        }
        gurgleValue = gurgle.process();
        const float env = std::min (1.0f, u / 0.06f) * std::pow (1.0f - u, 1.5f);
        return level * env * (1.0f + gurgleDepth * 0.6f * gurgleValue) * band.process (0, hp.process (0, noise.white())) * 0.5f;
    }

private:
    void start (float seconds, float from, float to, float gurgleAmount, float gain) noexcept
    {
        length = remaining = (int) (seconds * sr);
        fromHz = from; toHz = to; gurgleDepth = gurgleAmount; level = gain;
    }

    Svf band, hp;
    Drift gurgle;
    Noise noise { 2024 };
    float sr = 44100, fromHz = 500, toHz = 1000, gurgleDepth = 0.5f, gurgleValue = 0, level = 1;
    int remaining = 0, length = 1, counter = 0;
};

//==============================================================================
// People coming and going through a door. Each visitor walks in (door opens,
// door shuts), hangs about at a spot, then walks out again.
struct CrowdSettings
{
    int numSpots = 4;
    float stayMin = 20, stayMax = 60;     // seconds at their spot
    float arriveSlow = 50, arriveFast = 8; // mean gap between arrivals at vibes 0+ .. 1
    int startPeople = 1;
    float pukeChance = 0.05f;             // per visitor, scaled by vibes
    bool sickVisitors = false;            // puking happens by someone coming out specially, at the side spot
    float sickFirst = 40, sickEvery = 150; // seconds at vibes 50%: the first one, then the average gap
};

class Crowd
{
public:
    static constexpr int maxPeople = 5;   // four spots, plus room for one emergency at the bin
    static constexpr float enterTime = 3.0f, enterDoorShut = 1.6f;
    static constexpr float leaveTime = 3.2f, leaveDoorOpen = 1.4f, leaveDoorShut = 3.0f;

    enum State { absent = 0, entering, present, leaving };

    struct Person
    {
        State state = absent;
        float t = 0, stay = 0, pukeAt = -1, nextStep = 0;
        int spot = 0, colour = 0;
        bool puked = false, sick = false;
    };

    // Things that happened during the last advance() (bitmasks over people).
    struct Events
    {
        uint32_t arrived = 0, startedLeaving = 0, puked = 0, stepped = 0;
    };

    void prepare (double sampleRate, const CrowdSettings& s, uint32_t seed) noexcept
    {
        sr = (float) sampleRate;
        settings = s;
        noise = Noise (seed);
    }

    void reset (float vibes) noexcept
    {
        for (auto& p : people)
            p = Person();
        arrivalCountdown = 3.0f + 4.0f * noise.uniform();
        pukeCooldown = 30.0f;
        sickCountdown = settings.sickFirst * (0.7f + 0.6f * noise.uniform());

        // Start with a few people already there so the place doesn't feel empty.
        const int start = vibes > 0.15f ? settings.startPeople : 0;
        for (int i = 0; i < start; ++i)
        {
            auto& p = people[(size_t) i];
            p.state = present;
            p.spot = freeSpot();
            p.colour = freeColour();
            p.stay = lerp (settings.stayMin, settings.stayMax, noise.uniform()) * 0.6f;
            p.t = 0;
        }
    }

    // Advance by a control chunk; returns whether anyone needs the door open.
    bool advance (int numSamples, float vibes, Events& ev) noexcept
    {
        const float dt = (float) numSamples / sr;
        ev = {};
        bool door = false;

        arrivalCountdown -= dt;
        pukeCooldown -= dt;
        if (settings.sickVisitors && vibes > 0.02f)
        {
            // Their own clock, scaled by vibes, so it reliably happens now and then.
            sickCountdown -= dt * vibes * 2.0f;
            if (sickCountdown <= 0)
                sickCountdown = sendOutSick() ? settings.sickEvery * (0.6f + 0.8f * noise.uniform()) : 3.0f;
        }
        if (arrivalCountdown <= 0)
        {
            arrivalCountdown = lerp (settings.arriveSlow, settings.arriveFast, vibes) * (0.5f + noise.uniform());
            if (vibes > 0.02f)
                spawn();
        }

        for (int i = 0; i < maxPeople; ++i)
        {
            auto& p = people[(size_t) i];
            if (p.state == absent)
                continue;
            p.t += dt;

            switch (p.state)
            {
                case entering:
                    door |= p.t < enterDoorShut;
                    step (p, i, ev, p.t > 0.8f);
                    if (p.t >= enterTime)
                    {
                        p.state = present;
                        p.t = 0;
                        ev.arrived |= 1u << i;
                        if (! settings.sickVisitors && pukeCooldown <= 0 && noise.uniform() < settings.pukeChance * vibes)
                        {
                            p.pukeAt = p.stay * lerp (0.2f, 0.6f, noise.uniform());
                            pukeCooldown = 90.0f;
                        }
                    }
                    break;

                case present:
                    if (! p.puked && p.pukeAt >= 0 && p.t >= p.pukeAt)
                    {
                        p.puked = true;
                        ev.puked |= 1u << i;
                    }
                    if (p.t >= p.stay)
                    {
                        p.state = leaving;
                        p.t = 0;
                        ev.startedLeaving |= 1u << i;
                    }
                    break;

                case leaving:
                    door |= p.t > leaveDoorOpen && p.t < leaveDoorShut;
                    step (p, i, ev, p.t < 2.4f);
                    if (p.t >= leaveTime)
                        p.state = absent;
                    break;

                case absent: break;
            }
        }
        return door;
    }

    float progress (int i) const noexcept
    {
        const auto& p = people[(size_t) i];
        switch (p.state)
        {
            case entering: return std::min (1.0f, p.t / enterTime);
            case leaving:  return std::min (1.0f, p.t / leaveTime);
            case present:  return std::min (1.0f, p.t / std::max (1.0f, p.stay));
            case absent:   break;
        }
        return 0;
    }

    int countPresent() const noexcept
    {
        int n = 0;
        for (auto& p : people)
            n += p.state == present && ! p.sick ? 1 : 0;
        return n;
    }

    std::array<Person, maxPeople> people;

private:
    void step (Person& p, int i, Events& ev, bool walking) noexcept
    {
        if (! walking)
            return;
        if (p.t >= p.nextStep)
        {
            ev.stepped |= 1u << i;
            p.nextStep = p.t + 0.42f + 0.06f * noise.uniform();
        }
    }

    int freeSpot() noexcept
    {
        for (int tries = 0; tries < 16; ++tries)
        {
            const int s = (int) (noise.uniform() * (float) settings.numSpots * 0.999f);
            bool taken = false;
            for (auto& p : people)
                taken |= p.state != absent && p.spot == s;
            if (! taken)
                return s;
        }
        return -1;
    }

    // A colour nobody else here is wearing.
    int freeColour() noexcept
    {
        int c = (int) (noise.uniform() * 5.99f);
        for (int tries = 0; tries < 6; ++tries, c = (c + 1) % 6)
        {
            bool taken = false;
            for (auto& p : people)
                taken |= p.state != absent && p.colour == c;
            if (! taken)
                break;
        }
        return c;
    }

    // Someone staggers out green, heads for the bin at the side, and goes back in after.
    bool sendOutSick() noexcept
    {
        for (auto& p : people)
            if (p.state != absent && p.sick)
                return false;
        for (auto& p : people)
            if (p.state == absent)
            {
                p = Person();
                p.state = entering;
                p.spot = settings.numSpots;
                p.colour = freeColour();
                p.stay = lerp (7.0f, 9.0f, noise.uniform());
                p.pukeAt = 1.4f;
                p.sick = true;
                return true;
            }
        return false;
    }

    void spawn() noexcept
    {
        for (auto& p : people)
            if (p.state == absent)
            {
                const int s = freeSpot();
                if (s < 0)
                    return;
                p = Person();
                p.state = entering;
                p.spot = s;
                p.colour = freeColour();
                p.stay = lerp (settings.stayMin, settings.stayMax, noise.uniform());
                return;
            }
    }

    CrowdSettings settings;
    Noise noise;
    float sr = 44100, arrivalCountdown = 5, pukeCooldown = 30, sickCountdown = 40;
};

//==============================================================================
// A city street: cars passing with Doppler, the odd horn, a distant roar and
// the occasional siren. Positions are exposed so the cartoon can follow along.
class CityTraffic
{
public:
    static constexpr int maxCars = 4;

    struct Car
    {
        bool active = false;
        float x = 0, v = 0, lane = 4, engineHz = 40, phase = 0, honkPhase = 0;
        float honkAt = 999, honkLeft = 0;
        int colour = 0;
        Svf tyre, engine, air, horn;
    };

    void prepare (double sampleRate, uint32_t seed) noexcept
    {
        sr = (float) sampleRate;
        noise = Noise (seed);
        for (auto& c : cars)
            for (auto* f : { &c.tyre, &c.engine, &c.air, &c.horn })
                f->setSampleRate (sampleRate);
        for (auto* f : { &roarLp, &sirenLp })
            f->setSampleRate (sampleRate);
        roarLp.lowpass (420.0f, 0.6f);
        sirenLp.lowpass (1800.0f, 0.7f);
        roarDrift.prepare (sampleRate, 3.0f, seed + 1);
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : cars)
            c.active = false;
        spawnCountdown = 1.5f;
        sirenCountdown = 40.0f + 60.0f * noise.uniform();
        sirenTime = -1;
        roarLp.reset();
        sirenLp.reset();
    }

    // Adds the street into l/r (as heard standing on the pavement).
    void process (float* l, float* r, int n, float vibes) noexcept
    {
        if (vibes < 0.01f)
            return;

        const float dt = (float) n / sr;
        spawnCountdown -= dt;
        if (spawnCountdown <= 0)
        {
            spawnCountdown = lerp (16.0f, 2.2f, vibes) * (0.4f + 1.2f * noise.uniform());
            spawn();
        }
        sirenCountdown -= dt;
        if (sirenCountdown <= 0 && sirenTime < 0)
        {
            sirenTime = 0;
            sirenCountdown = lerp (240.0f, 70.0f, vibes) * (0.6f + 0.8f * noise.uniform());
        }

        for (auto& c : cars)
        {
            if (! c.active)
                continue;
            const float d = std::sqrt (c.x * c.x + c.lane * c.lane);
            c.air.lowpass (9000.0f / (1.0f + d / 14.0f), 0.6f);
            if (c.honkLeft <= 0 && std::abs (c.x - c.honkAt) < 1.0f)
            {
                c.honkLeft = noise.uniform() < 0.5f ? 0.35f : 0.9f;
                c.honkAt = 999;
            }
        }

        const float roarLevel = vibes * (0.018f + 0.006f * roarDrift.advance (n));

        for (int i = 0; i < n; ++i)
        {
            float yl = 0, yr = 0;
            for (auto& c : cars)
            {
                if (! c.active)
                    continue;
                c.x += c.v / sr;
                const float d = std::sqrt (c.x * c.x + c.lane * c.lane);
                const float receding = c.v * c.x / d;
                const float doppler = 343.0f / (343.0f + receding);

                c.phase += c.engineHz * doppler / sr;
                c.phase -= std::floor (c.phase);
                const float growl = c.engine.process (0, c.phase < 0.3f ? 1.0f : -0.43f);
                const float tyres = c.tyre.process (0, noise.pink() * 3.0f);
                float src = 0.55f * tyres + 0.45f * growl;

                if (c.honkLeft > 0)
                {
                    c.honkLeft -= 1.0f / sr;
                    c.honkPhase += 415.0f * doppler / sr;
                    c.honkPhase -= std::floor (c.honkPhase);
                    const float sq = (c.honkPhase < 0.5f ? 1.0f : -1.0f) + (std::fmod (c.honkPhase * 1.26f, 1.0f) < 0.5f ? 1.0f : -1.0f);
                    // A double beep for the long ones.
                    const bool gap = c.honkLeft > 0.4f && c.honkLeft < 0.55f;
                    src += gap ? 0.0f : c.horn.process (0, sq) * 0.5f;
                }

                const float g = 0.55f * 4.0f / std::max (d, 2.0f);
                const float y = c.air.process (0, src) * g;
                const float pan = juce::jlimit (-1.0f, 1.0f, c.x / d);
                yl += y * std::cos ((pan + 1.0f) * pi * 0.25f);
                yr += y * std::sin ((pan + 1.0f) * pi * 0.25f);

                if ((c.v > 0 && c.x > 80.0f) || (c.v < 0 && c.x < -80.0f))
                    c.active = false;
            }

            // The city itself: a low roar, and now and then a siren a few blocks away.
            const float roar = roarLevel * roarLp.process (0, noise.pink());
            yl += roar;
            yr += roar;

            if (sirenTime >= 0)
            {
                sirenTime += 1.0f / sr;
                const float wail = 0.5f + 0.5f * std::sin (2.0f * pi * sirenTime / 1.3f);
                sirenPhase += lerp (650.0f, 1350.0f, wail) / sr;
                sirenPhase -= std::floor (sirenPhase);
                const float env = std::min (1.0f, sirenTime / 2.0f) * std::min (1.0f, std::max (0.0f, (9.0f - sirenTime) / 3.0f));
                const float s = sirenLp.process (0, std::sin (2.0f * pi * sirenPhase)) * env * 0.012f * vibes;
                yl += s * 0.8f;
                yr += s;
                if (sirenTime > 9.0f)
                    sirenTime = -1;
            }

            l[i] += yl;
            r[i] += yr;
        }
    }

    std::array<Car, maxCars> cars;

private:
    void spawn() noexcept
    {
        for (auto& c : cars)
            if (! c.active)
            {
                const bool right = noise.uniform() < 0.5f;
                c.active = true;
                c.v = (right ? 1.0f : -1.0f) * lerp (9.0f, 15.0f, noise.uniform());
                c.x = right ? -80.0f : 80.0f;
                c.lane = right ? 3.5f : 7.0f;
                c.engineHz = lerp (28.0f, 55.0f, noise.uniform());
                c.colour = (int) (noise.uniform() * 5.99f);
                c.honkAt = noise.uniform() < 0.15f ? lerp (-12.0f, 12.0f, noise.uniform()) : 999.0f;
                c.honkLeft = 0;
                c.tyre.bandpass (lerp (550.0f, 900.0f, noise.uniform()), 0.6f);
                c.engine.lowpass (260.0f, 0.8f);
                c.horn.bandpass (1100.0f, 1.2f);
                for (auto* f : { &c.tyre, &c.engine, &c.air, &c.horn })
                    f->reset();
                return;
            }
    }

    Noise noise;
    Svf roarLp, sirenLp;
    Drift roarDrift;
    float sr = 44100, spawnCountdown = 2, sirenCountdown = 60, sirenTime = -1, sirenPhase = 0;
};

//==============================================================================
// Footsteps on a given floor.
class Footsteps
{
public:
    void prepare (double sampleRate, const std::array<Mode, 3>& floor) noexcept
    {
        striker.prepare (sampleRate, 2.5f, 0.6f);
        body.prepare (sampleRate, floor);
    }

    void step (float gain) noexcept { striker.trigger (gain * (0.7f + 0.3f * noise.uniform())); }
    float process() noexcept { return body.process (striker.next()); }
    void reset() noexcept { striker.reset(); body.reset(); }

private:
    Striker striker;
    ModalBody<3> body;
    Noise noise { 5150 };
};

} // namespace elsewhere
