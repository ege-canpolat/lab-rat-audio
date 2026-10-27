#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cmath>

// Small, allocation-free building blocks shared by all the scenes.
namespace elsewhere
{
constexpr float pi = juce::MathConstants<float>::pi;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

// Exponential interpolation, for sweeping filter cutoffs so they sound even.
inline float expLerp (float a, float b, float t) noexcept { return a * std::pow (b / a, t); }

inline float smoothstep (float t) noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return t * t * (3.0f - 2.0f * t);
}

// Gentle ceiling so stacked boosts can never slam the host's output.
inline float softClip (float x) noexcept
{
    const float knee = 0.85f;
    const float a = std::abs (x);
    if (a <= knee)
        return x;
    return std::copysign (knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee)), x);
}

//==============================================================================
// Cytomic / Simper trapezoidal state-variable filter. Stereo state, shared
// coefficients. Stays well behaved under fast modulation (doors, wind).
class Svf
{
public:
    void setSampleRate (double newRate) noexcept { sr = (float) newRate; }

    void lowpass (float f, float q = 0.707f) noexcept  { setG (f, q); m0 = 0; m1 = 0;  m2 = 1; }
    void highpass (float f, float q = 0.707f) noexcept { setG (f, q); m0 = 1; m1 = -k; m2 = -1; }
    void bandpass (float f, float q = 0.707f) noexcept { setG (f, q); m0 = 0; m1 = k;  m2 = 0; }

    void bell (float f, float q, float db) noexcept
    {
        const float A = std::pow (10.0f, db / 40.0f);
        g = std::tan (pi * limit (f) / sr);
        k = 1.0f / (q * A);
        m0 = 1; m1 = k * (A * A - 1); m2 = 0;
        update();
    }

    void lowShelf (float f, float q, float db) noexcept
    {
        const float A = std::pow (10.0f, db / 40.0f);
        g = std::tan (pi * limit (f) / sr) / std::sqrt (A);
        k = 1.0f / q;
        m0 = 1; m1 = k * (A - 1); m2 = A * A - 1;
        update();
    }

    void highShelf (float f, float q, float db) noexcept
    {
        const float A = std::pow (10.0f, db / 40.0f);
        g = std::tan (pi * limit (f) / sr) * std::sqrt (A);
        k = 1.0f / q;
        m0 = A * A; m1 = k * (1 - A) * A; m2 = 1 - A * A;
        update();
    }

    void reset() noexcept
    {
        ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0;
    }

    float process (int ch, float v0) noexcept
    {
        const float v3 = v0 - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2 * v1 - ic1[ch];
        ic2[ch] = 2 * v2 - ic2[ch];
        return m0 * v0 + m1 * v1 + m2 * v2;
    }

private:
    float limit (float f) const noexcept { return juce::jlimit (10.0f, sr * 0.47f, f); }
    void setG (float f, float q) noexcept { g = std::tan (pi * limit (f) / sr); k = 1.0f / q; update(); }
    void update() noexcept { a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }

    float sr = 44100, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, m0 = 0, m1 = 0, m2 = 1;
    float ic1[2] {}, ic2[2] {};
};

//==============================================================================
struct Noise
{
    uint32_t state = 0x9E3779B9u;

    explicit Noise (uint32_t seed = 1) : state (seed * 2654435761u + 12345u) {}

    float white() noexcept
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) state * (2.0f / 4294967296.0f) - 1.0f;
    }

    float uniform() noexcept { return 0.5f * (white() + 1.0f); }

    // Paul Kellet's economy pink filter.
    float pink() noexcept
    {
        const float w = white();
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * 0.2f;
    }

    float b0 = 0, b1 = 0, b2 = 0;
};

//==============================================================================
class Follower
{
public:
    void prepare (double sr, float attackMs, float releaseMs) noexcept
    {
        att = std::exp (-1.0f / (attackMs * 0.001f * (float) sr));
        rel = std::exp (-1.0f / (releaseMs * 0.001f * (float) sr));
    }

    float process (float x) noexcept
    {
        x = std::abs (x);
        env = x + (x > env ? att : rel) * (env - x);
        return env;
    }

    void reset() noexcept { env = 0; }

    float env = 0;

private:
    float att = 0, rel = 0;
};

//==============================================================================
// Slow random wander in [-1, 1]: wind, people walking about, etc.
class Drift
{
public:
    void prepare (double sampleRate, float periodSeconds, uint32_t seed) noexcept
    {
        sr = (float) sampleRate;
        period = periodSeconds;
        coef = 1.0f - std::exp (-1.0f / (0.35f * period * sr));
        noise = Noise (seed);
        countdown = 0;
    }

    float process() noexcept
    {
        if (--countdown <= 0)
        {
            target = noise.white();
            countdown = (int) (period * sr * (0.5f + noise.uniform()));
        }
        s1 += (target - s1) * coef;
        s2 += (s1 - s2) * coef;
        return s2;
    }

    // Advance a whole block at once (control-rate use).
    float advance (int numSamples) noexcept
    {
        float v = s2;
        for (int i = 0; i < numSamples; ++i)
            v = process();
        return v;
    }

private:
    Noise noise;
    float sr = 44100, period = 1, coef = 0, target = 0, s1 = 0, s2 = 0;
    int countdown = 0;
};

//==============================================================================
// A swinging door: linear travel, eased so it starts and lands softly.
class Door
{
public:
    void prepare (double sampleRate, float openSeconds, float closeSeconds) noexcept
    {
        openStep  = 1.0f / (openSeconds  * (float) sampleRate);
        closeStep = 1.0f / (closeSeconds * (float) sampleRate);
    }

    // Returns true on the sample the door slams shut.
    bool process (bool wantOpen) noexcept
    {
        if (wantOpen)
        {
            pos = std::min (1.0f, pos + openStep);
            peak = std::max (peak, pos);
            return false;
        }

        if (pos <= 0)
            return false;

        pos = std::max (0.0f, pos - closeStep);
        if (pos > 0)
            return false;

        // Only call it a slam if the door was properly open, not a twitch.
        const bool slam = peak > 0.3f;
        peak = 0;
        return slam;
    }

    float openness() const noexcept { return smoothstep (pos); }

    void reset() noexcept { pos = 0; peak = 0; }

private:
    float pos = 0, peak = 0, openStep = 0.001f, closeStep = 0.001f;
};

//==============================================================================
// People wandering in and out: random open/close timing driven by "vibes".
class Traffic
{
public:
    void prepare (double sampleRate, uint32_t seed) noexcept
    {
        sr = (float) sampleRate;
        noise = Noise (seed);
        open = false;
        countdown = (int) (sr * 2.5f);
    }

    bool process (int numSamples, float vibes, float minOpenSec = 1.2f, float maxOpenSec = 4.0f) noexcept
    {
        if (vibes < 0.02f)
        {
            open = false;
            return false;
        }

        countdown -= numSamples;
        if (countdown <= 0)
        {
            open = ! open;
            const float seconds = open ? lerp (minOpenSec, maxOpenSec, noise.uniform())
                                       : lerp (24.0f, 2.5f, vibes) * (0.5f + noise.uniform());
            countdown = (int) (seconds * sr);
        }
        return open;
    }

    void reset() noexcept { open = false; countdown = (int) (sr * 2.5f); }

private:
    Noise noise;
    float sr = 44100;
    bool open = false;
    int countdown = 0;
};

//==============================================================================
// Percussive one-shot: a damped body tone plus a filtered noise click.
// Used for door slams and knuckles on a wall.
class Impact
{
public:
    void prepare (double sampleRate, float bodyHz, float bodyDecayMs, float clickHz, float clickDecayMs) noexcept
    {
        sr = (float) sampleRate;
        inc = 2.0f * pi * bodyHz / sr;
        bodyDecay = std::exp (-1.0f / (bodyDecayMs * 0.001f * sr));
        clickDecay = std::exp (-1.0f / (clickDecayMs * 0.001f * sr));
        clickFilter.setSampleRate (sampleRate);
        clickFilter.lowpass (clickHz, 0.9f);
    }

    void trigger (float gain, int delaySamples = 0) noexcept
    {
        for (auto& p : pending)
        {
            if (p.delay < 0)
            {
                p.delay = delaySamples;
                p.gain = gain;
                return;
            }
        }
    }

    float process() noexcept
    {
        for (auto& p : pending)
        {
            if (p.delay >= 0 && p.delay-- == 0)
            {
                bodyEnv = clickEnv = p.gain;
                phase = 0;
                p.delay = -1;
            }
        }

        if (bodyEnv < 1.0e-5f && clickEnv < 1.0e-5f)
            return 0;

        const float body = std::sin (phase) * bodyEnv;
        phase += inc * (1.0f + 0.6f * bodyEnv);   // a little pitch drop as it dies away
        const float click = clickFilter.process (0, noise.white()) * clickEnv;
        bodyEnv *= bodyDecay;
        clickEnv *= clickDecay;
        return body + click * 0.8f;
    }

    void reset() noexcept
    {
        bodyEnv = clickEnv = 0;
        for (auto& p : pending)
            p.delay = -1;
        clickFilter.reset();
    }

private:
    struct Pending { int delay = -1; float gain = 0; };
    std::array<Pending, 6> pending;
    Svf clickFilter;
    Noise noise { 77 };
    float sr = 44100, inc = 0, phase = 0, bodyDecay = 0, clickDecay = 0, bodyEnv = 0, clickEnv = 0;
};

//==============================================================================
// Stereo delay line with cubic (Hermite) interpolation for gentle wobbles.
class WobbleDelay
{
public:
    void prepare (int maxSamples)
    {
        size = juce::nextPowerOfTwo (maxSamples + 4);
        for (auto& b : buffer)
            b.assign ((size_t) size, 0.0f);
        write = 0;
    }

    void reset() noexcept
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);
    }

    float process (int ch, float x, float delaySamples) noexcept
    {
        auto& b = buffer[(size_t) ch];
        const int mask = size - 1;
        b[(size_t) write] = x;

        const float readPos = (float) write - delaySamples;
        const int i1 = (int) std::floor (readPos);
        const float t = readPos - (float) i1;
        const float y0 = b[(size_t) ((i1 - 1) & mask)], y1 = b[(size_t) (i1 & mask)];
        const float y2 = b[(size_t) ((i1 + 1) & mask)], y3 = b[(size_t) ((i1 + 2) & mask)];

        if (ch == 1)
            write = (write + 1) & mask;

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

private:
    std::array<std::vector<float>, 2> buffer;
    int size = 0, write = 0;
};

//==============================================================================
// Fixed multi-tap stereo delay for short reflections (windscreens, seat offsets).
class TapDelay
{
public:
    void prepare (int maxSamples)
    {
        size = juce::nextPowerOfTwo (maxSamples + 2);
        for (auto& b : buffer)
            b.assign ((size_t) size, 0.0f);
        write = 0;
    }

    void reset() noexcept
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);
    }

    void push (float l, float r) noexcept
    {
        write = (write + 1) & (size - 1);
        buffer[0][(size_t) write] = l;
        buffer[1][(size_t) write] = r;
    }

    float read (int ch, int delaySamples) const noexcept
    {
        return buffer[(size_t) ch][(size_t) ((write - delaySamples) & (size - 1))];
    }

private:
    std::array<std::vector<float>, 2> buffer;
    int size = 0, write = 0;
};

//==============================================================================
// Synthesised room impulse responses. Each scene describes its space with one
// of these and we render a stereo IR at the host's sample rate.
struct RoomSpec
{
    float lengthSec   = 1.0f;
    float rt60        = 0.8f;
    float predelayMs  = 4.0f;
    int   erCount     = 12;
    float erSpreadMs  = 30.0f;
    float erGain      = 0.4f;
    float brightStart = 8000.0f;   // tail low-pass at the start...
    float brightEnd   = 2000.0f;   // ...and after one RT60
    float highpass    = 80.0f;
    float direct      = 1.0f;
    float wet         = 0.5f;
    float width       = 1.0f;      // 0 = mono tail, 1 = fully decorrelated

    struct Echo { float ms, gain; };
    std::vector<Echo> echoes;      // discrete slap-backs off buildings / hills
    float echoCutoff  = 4000.0f;
    uint32_t seed     = 1;
};

inline juce::AudioBuffer<float> makeRoomIR (const RoomSpec& spec, double sampleRate)
{
    const float sr = (float) sampleRate;
    const int length = std::max (64, (int) (spec.lengthSec * sr));
    juce::AudioBuffer<float> ir (2, length);
    ir.clear();

    Noise common (spec.seed), left (spec.seed + 11), right (spec.seed + 23), er (spec.seed + 37);
    const int pre = (int) (spec.predelayMs * 0.001f * sr);
    const float spread = std::max (0.001f, spec.erSpreadMs * 0.001f);

    // Diffuse tail: noise, exponential decay, darkening as it fades.
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = ir.getWritePointer (ch);
        auto& own = ch == 0 ? left : right;
        common = Noise (spec.seed);
        float lp1 = 0, lp2 = 0, hpState = 0, hpPrev = 0;
        const float hpCoef = std::exp (-2.0f * pi * spec.highpass / sr);
        double energy = 0;

        for (int i = pre; i < length; ++i)
        {
            const float t = (float) (i - pre) / sr;
            const float n = common.white() * (1.0f - spec.width) + own.white() * spec.width;
            const float env = std::exp (-6.9078f * t / spec.rt60)
                            * (1.0f - std::exp (-t / (0.35f * spread + 0.001f)));
            const float fc = expLerp (spec.brightStart, spec.brightEnd, std::min (1.0f, t / spec.rt60));
            const float a = std::exp (-2.0f * pi * fc / sr);
            lp1 = n + a * (lp1 - n);
            lp2 = lp1 + a * (lp2 - lp1);
            hpState = hpCoef * (hpState + lp2 - hpPrev);
            hpPrev = lp2;
            d[i] = hpState * env;
            energy += (double) d[i] * d[i];
        }

        const float norm = energy > 0 ? spec.wet / (float) std::sqrt (energy) : 0.0f;
        for (int i = 0; i < length; ++i)
            d[i] *= norm;
    }

    auto addTap = [&] (int ch, float timeSec, float gain, int smear)
    {
        const int centre = (int) (timeSec * sr);
        auto* d = ir.getWritePointer (ch);
        float total = 0;
        for (int j = -smear; j <= smear; ++j)
            total += 0.5f + 0.5f * std::cos (pi * (float) j / (float) (smear + 1));
        for (int j = -smear; j <= smear; ++j)
        {
            const int idx = centre + j;
            if (idx >= 0 && idx < length)
                d[idx] += gain * (0.5f + 0.5f * std::cos (pi * (float) j / (float) (smear + 1))) / total;
        }
    };

    // Early reflections: sparse taps, slightly different per ear.
    for (int r = 0; r < spec.erCount; ++r)
    {
        const float when = (float) pre / sr + er.uniform() * spread;
        const float fall = 1.0f - 0.6f * (when - (float) pre / sr) / spread;
        const float g = spec.erGain * (0.4f + 0.6f * er.uniform()) * fall * (er.white() > 0 ? 1.0f : -1.0f);
        for (int ch = 0; ch < 2; ++ch)
            addTap (ch, when + spec.width * 0.0007f * er.white(), g * (0.8f + 0.2f * er.uniform()), 1);
    }

    // Discrete echoes, smeared to darken them like real distant surfaces.
    const int echoSmear = std::max (1, (int) (sr / spec.echoCutoff));
    for (auto& e : spec.echoes)
        for (int ch = 0; ch < 2; ++ch)
            addTap (ch, e.ms * 0.001f + (ch == 0 ? 0.0f : spec.width * 0.004f), e.gain, echoSmear);

    for (int ch = 0; ch < 2; ++ch)
        ir.getWritePointer (ch)[0] += spec.direct;

    return ir;
}

class RoomStage
{
public:
    explicit RoomStage (juce::dsp::ConvolutionMessageQueue& queue)
        : conv (juce::dsp::Convolution::NonUniform { 256 }, queue) {}

    void prepare (double sampleRate, int maxBlock, const RoomSpec& spec)
    {
        // Loading before prepare() guarantees the IR is live on the first block.
        conv.loadImpulseResponse (makeRoomIR (spec, sampleRate), sampleRate,
                                  juce::dsp::Convolution::Stereo::yes,
                                  juce::dsp::Convolution::Trim::no,
                                  juce::dsp::Convolution::Normalise::no);
        conv.prepare ({ sampleRate, (juce::uint32) maxBlock, 2 });
    }

    void reset() noexcept { conv.reset(); }

    void process (float* l, float* r, int numSamples) noexcept
    {
        float* chans[] = { l, r };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) numSamples);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

private:
    juce::dsp::Convolution conv;
};

} // namespace elsewhere
