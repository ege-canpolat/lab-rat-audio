// Offline test bench for Elsewhere.
//   ElsewhereRender <outputDir> [input.wav]
// Bounces a test mix (or your own file) through every scene, prints loudness
// and sanity stats, and saves a PNG snapshot of the UI for each scene.

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

using namespace juce;

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

// A little four-on-the-floor tune with something in every frequency range.
AudioBuffer<float> makeTestMix (double seconds)
{
    const int n = (int) (seconds * sampleRate);
    AudioBuffer<float> mix (2, n);
    mix.clear();
    auto* L = mix.getWritePointer (0);
    auto* R = mix.getWritePointer (1);

    const double bpm = 122.0, beat = 60.0 / bpm;
    const float bassNotes[] = { 45.0f, 45.0f, 43.65f, 49.0f };       // A1 A1 F1 G1
    const float chordRoots[] = { 220.0f, 220.0f, 174.6f, 196.0f };
    const float melody[] = { 880, 987.8f, 1046.5f, 987.8f, 880, 784, 659.3f, 784 };
    Random rng (42);
    double kickPhase = 0, bassPhase = 0, leadPhase = 0;
    double padPhase[6] {};
    float bassLp = 0, padLp = 0, hatHp = 0, hatPrev = 0, snareLp = 0;

    for (int i = 0; i < n; ++i)
    {
        const double t = i / sampleRate;
        const double beatPos = t / beat;
        const double inBeat = std::fmod (beatPos, 1.0) * beat;   // seconds since the last beat
        const int bar = (int) (beatPos / 4.0) % 4;

        // Kick on every beat.
        const double kf = 45.0 + 90.0 * std::exp (-inBeat * 30.0);
        kickPhase += kf / sampleRate;
        const float kick = (float) (std::sin (2.0 * MathConstants<double>::pi * kickPhase) * std::exp (-inBeat * 7.0)) * 0.9f;

        // Snare on 2 and 4.
        const int beatIndex = (int) beatPos % 4;
        float snare = 0;
        if (beatIndex == 1 || beatIndex == 3)
        {
            const float env = (float) std::exp (-inBeat * 18.0);
            snareLp += 0.5f * (rng.nextFloat() * 2 - 1 - snareLp);
            snare = (snareLp * 0.5f + (float) std::sin (2 * MathConstants<double>::pi * 190.0 * inBeat) * 0.4f) * env * 0.5f;
        }

        // Offbeat hats.
        const double eighth = std::fmod (beatPos * 2.0, 1.0) * beat * 0.5;
        const float hatEnv = (float) std::exp (-eighth * 60.0);
        const float noise = rng.nextFloat() * 2 - 1;
        hatHp = 0.85f * (hatHp + noise - hatPrev);
        hatPrev = noise;
        const float hat = hatHp * hatEnv * 0.12f;

        // Saw bass, filtered.
        bassPhase += bassNotes[bar] * (std::fmod (beatPos, 1.0) > 0.5 ? 2.0 : 1.0) / sampleRate;
        const float saw = (float) (2.0 * std::fmod (bassPhase, 1.0) - 1.0);
        bassLp += 0.04f * (saw - bassLp);
        const float bass = bassLp * 0.45f * (float) (1.0 - std::exp (-inBeat * 40.0));

        // Detuned saw pad.
        float pad = 0;
        const float ratios[] = { 1.0f, 1.2599f, 1.4983f };
        for (int v = 0; v < 6; ++v)
        {
            padPhase[v] += chordRoots[bar] * ratios[v % 3] * (v < 3 ? 1.003 : 0.997) / sampleRate;
            pad += (float) (2.0 * std::fmod (padPhase[v], 1.0) - 1.0);
        }
        padLp += 0.12f * (pad - padLp);
        const float padOut = padLp * 0.05f;

        // A vocal-ish lead in the presence range.
        const int step = (int) (beatPos) % 8;
        leadPhase += melody[step] / sampleRate;
        const double lp = 2.0 * MathConstants<double>::pi * leadPhase;
        const float lead = (float) (std::sin (lp) + 0.3 * std::sin (2 * lp) + 0.15 * std::sin (3 * lp))
                         * 0.09f * (float) std::min (1.0, inBeat * 20.0);

        L[i] = kick + snare + hat * 0.8f + bass + padOut * 1.1f + lead * 0.9f;
        R[i] = kick + snare + hat * 1.2f + bass + padOut * 0.9f + lead * 1.1f;
    }

    mix.applyGain (0.89f / jmax (0.001f, mix.getMagnitude (0, n)));
    return mix;
}

// Rough loudness: RMS after a 100 Hz high-pass (a poor man's K-weighting).
float loudnessDb (const AudioBuffer<float>& b, int start, int end)
{
    elsewhere::Svf hp;
    hp.setSampleRate (sampleRate);
    hp.highpass (100.0f, 0.7f);
    double sum = 0;
    for (int i = 0; i < end; ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const float y = hp.process (ch, b.getSample (ch, i));
            if (i >= start) sum += (double) y * y;
        }
    return (float) (10.0 * std::log10 (sum / (2.0 * (end - start)) + 1e-12));
}

void writeWav (const AudioBuffer<float>& b, const File& f)
{
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> stream (f.createOutputStream());
    auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                         .withNumChannels (2)
                                                                         .withBitsPerSample (24));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct Cue { double from, to; };

AudioBuffer<float> runScene (int scene, const AudioBuffer<float>& input, const std::vector<Cue>& actionCues,
                             float vibes = 50.0f)
{
    ElsewhereProcessor proc;
    proc.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    auto set = [&] (const char* id, float value)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set (ParamIDs::scene, (float) scene);
    set (ParamIDs::vibes, vibes);
    proc.prepareToPlay (sampleRate, blockSize);

    AudioBuffer<float> out (input);
    MidiBuffer midi;
    for (int start = 0; start < out.getNumSamples(); start += blockSize)
    {
        const double t = start / sampleRate;
        bool on = false;
        for (auto& c : actionCues)
            on |= (t >= c.from && t < c.to);
        set (ParamIDs::action, on ? 1.0f : 0.0f);

        const int len = std::min (blockSize, out.getNumSamples() - start);
        AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, len);
        proc.processBlock (view, midi);
    }
    return out;
}

// Snapshot the UI. With `simulate`, real audio runs first so the people and
// traffic are where the DSP has put them; otherwise the telemetry is faked.
void snapshot (int scene, bool action, bool studio, const File& file,
               const AudioBuffer<float>* simulate = nullptr, double runSeconds = 0, int fakePukeSpot = -1)
{
    ElsewhereProcessor proc;
    auto set = [&] (const char* id, float value)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set (ParamIDs::scene, (float) scene);
    set (ParamIDs::studio, studio ? 1.0f : 0.0f);

    std::unique_ptr<AudioProcessorEditor> editor (proc.createEditorAndMakeActive());
    auto* ed = dynamic_cast<ElsewhereEditor*> (editor.get());
    if (ed == nullptr)
        return;

    if (action)
        set (ParamIDs::action, 1.0f);

    MidiBuffer midi;
    int cursor = 0;
    auto run = [&] (int samples)
    {
        if (simulate == nullptr)
            return;
        AudioBuffer<float> block (2, blockSize);
        for (int done = 0; done < samples; done += blockSize)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    block.setSample (ch, i, simulate->getSample (ch, (cursor + i) % simulate->getNumSamples()));
            cursor += blockSize;
            proc.processBlock (block, midi);
        }
    };

    if (simulate != nullptr)
    {
        set (ParamIDs::vibes, 100.0f);
        proc.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);
        run ((int) (runSeconds * sampleRate));
    }
    else
    {
        // Fake a busy moment so the cartoon is mid-dance.
        proc.bassLevel.store (0.3f);
        proc.outputLevel.store (0.5f);
        proc.telemetry.openness.store (action ? 1.0f : 0.0f);
        proc.telemetry.wind.store (action ? 0.8f : 0.3f);
        proc.telemetry.duck.store (0.0f);
    }

    for (int i = 0; i < 70; ++i)
    {
        if (simulate == nullptr && action && i == 60 && scene != 3 && scene != 4)
            proc.telemetry.events.fetch_add (1);   // knock / slam / gust mid-air
        if (simulate == nullptr && ! action && i == 50 && scene == 4)
            proc.telemetry.events.fetch_add (1);   // a message comes in
        if (fakePukeSpot >= 0 && i == 10)
        {
            proc.telemetry.pukeSpot.store (fakePukeSpot);
            proc.telemetry.pukes.fetch_add (1);
        }
        run (800);
        if (fakePukeSpot == 4)
        {
            // The terrace's sick visitor, at the bin.
            auto& v = proc.telemetry.people[4];
            v.state.store (2); v.spot.store (4); v.colour.store (1);
            v.sick.store (true); v.puked.store (i >= 10);
            v.talking.store (false); v.laughing.store (false);
        }
        ed->getView().tick (1.0f / 60.0f);
    }

    auto image = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    file.deleteFile();
    FileOutputStream os (file);
    PNGImageFormat().writeImageToStream (image, os);
}

// Level of each "life" synth on its own, so the scenes can set sane gains.
void measureSources()
{
    using namespace elsewhere;
    auto report = [] (const char* name, double seconds, const std::function<float()>& gen)
    {
        const int len = (int) (seconds * sampleRate);
        double sum = 0;
        float peak = 0;
        for (int i = 0; i < len; ++i)
        {
            const float y = gen();
            sum += (double) y * y;
            peak = std::max (peak, std::abs (y));
        }
        std::printf ("  %-22s peak %7.1f dBFS   rms %7.1f dBFS\n", name,
                     Decibels::gainToDecibels (peak, -120.0f), Decibels::gainToDecibels ((float) std::sqrt (sum / len), -120.0f));
    };

    Knocker knock; knock.prepare (sampleRate); knock.knockThreeTimes();
    report ("knock x3", 0.6, [&] { return knock.process(); });

    Footsteps steps;
    steps.prepare (sampleRate, {{ { 190.0f, 0.03f, 1.0f }, { 1100.0f, 0.015f, 0.45f }, { 2700.0f, 0.008f, 0.3f } }});
    steps.step (1.0f);
    report ("footstep (tiles)", 0.15, [&] { return steps.process(); });

    Babbler voice; voice.prepare (sampleRate, 120.0f, 7); voice.setTalking (true);
    report ("babbler talking", 6.0, [&] { return voice.process(); });


    // How often someone comes out to be sick on the terrace.
    for (float vibes : { 0.25f, 0.5f, 1.0f })
    {
        CrowdSettings cs;
        cs.stayMin = 40; cs.stayMax = 120; cs.arriveSlow = 35; cs.arriveFast = 7; cs.startPeople = 3; cs.sickVisitors = true;
        Crowd crowd; crowd.prepare (sampleRate, cs, 40); crowd.reset (vibes);
        std::printf ("  sick visitors, vibes %3.0f%%, first 10 min:", vibes * 100.0f);
        for (int chunk = 0; chunk < (int) (600.0 * sampleRate / 32); ++chunk)
        {
            Crowd::Events ev;
            crowd.advance (32, vibes, ev);
            if (ev.puked)
                std::printf (" %.0fs", chunk * 32 / sampleRate);
        }
        std::printf ("\n");
    }

    // Sound effects on their own, for auditioning.
    auto bounce = [] (const char* file, double seconds, const std::function<float()>& gen)
    {
        AudioBuffer<float> b (2, (int) (seconds * sampleRate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, gen() * 0.5f), b.setSample (1, i, b.getSample (0, i));
        writeWav (b, File::getCurrentWorkingDirectory().getChildFile ("Renders").getChildFile (file));
    };
    Knocker k2; k2.prepare (sampleRate); k2.knockThreeTimes();
    bounce ("sfx_knock.wav", 1.0, [&] { return k2.process(); });
    Babbler b2; b2.prepare (sampleRate, 135.0f, 11); b2.setTalking (true);
    bounce ("sfx_voice.wav", 6.0, [&] { return b2.process(); });

    Water water; water.prepare (sampleRate); water.flush();
    report ("flush", 3.0, [&] { return water.process(); });
    water.tap();
    report ("tap", 2.5, [&] { return water.process(); });



    Impact slam; slam.prepare (sampleRate, 68.0f, 130.0f, 900.0f, 22.0f); slam.trigger (1.0f);
    report ("slam (gain 1)", 0.5, [&] { return slam.process(); });

    CityTraffic city; city.prepare (sampleRate, 81);
    std::vector<float> cl (512), cr (512);
    int idx = 512;
    report ("city 60s vibes 1", 60.0, [&]
    {
        if (idx == 512)
        {
            std::fill (cl.begin(), cl.end(), 0.0f); std::fill (cr.begin(), cr.end(), 0.0f);
            city.process (cl.data(), cr.data(), 512, 1.0f);
            idx = 0;
        }
        const float y = 0.5f * (cl[(size_t) idx] + cr[(size_t) idx]);
        ++idx;
        return y;
    });
}

} // namespace

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI gui;

    if (argc > 1 && String (argv[1]) == "--sources")
    {
        measureSources();
        return 0;
    }

    const File outDir = argc > 1 ? File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                 : File::getCurrentWorkingDirectory().getChildFile ("Renders");
    outDir.createDirectory();

    AudioBuffer<float> input;
    if (argc > 2)
    {
        AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (File::getCurrentWorkingDirectory().getChildFile (argv[2])));
        if (reader == nullptr) { std::cerr << "Can't read input file\n"; return 1; }
        input.setSize (2, (int) reader->lengthInSamples);
        reader->read (&input, 0, (int) reader->lengthInSamples, 0, true, true);
    }
    else
        input = makeTestMix (40.0);

    writeWav (input, outDir.getChildFile ("00_studio_dry.wav"));
    const int n = input.getNumSamples();
    const int settle = (int) (1.0 * sampleRate);
    const float inLoud = loudnessDb (input, settle, n);
    std::cout << "input loudness " << inLoud << " dB\n\n";

    // Demo moves for each scene (seconds).
    const std::vector<std::vector<Cue>> demoCues {
        { { 4.0, 4.2 }, { 22.0, 22.2 } }, // knock
        { { 30.0, 34.0 } },             // door propped open (people come and go before that)
        { { 30.0, 34.0 } },             // door propped open
        { { 20.0, 99.0 } },             // step outside
        { { 25.0, 99.0 } },             // into the pocket
        { { 6.0, 6.2 } },               // gust
    };

    std::cout << "scene               loud(dB) vsIn   actionLoud  peak   clip%  life(dB vs music) lifePeak  nan\n";
    AudioBuffer<float> silence (2, n);
    silence.clear();
    for (int s = 0; s < elsewhere::numScenes; ++s)
    {
        const auto calm = runScene (s, input, {}, 0.0f);   // calibration: no actions, no vibes
        bool bad = false;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                bad |= ! std::isfinite (calm.getSample (ch, i));

        const auto demo = runScene (s, input, demoCues[(size_t) s]);
        const auto& cue = demoCues[(size_t) s][0];
        const int a0 = (int) ((cue.from + 1.0) * sampleRate), a1 = std::min (n, (int) (cue.to * sampleRate));
        const float actionLoud = a1 - a0 > (int) sampleRate ? loudnessDb (demo, a0, a1) - inLoud : 0.0f;

        const float loud = loudnessDb (calm, settle, n);
        int hot = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                hot += std::abs (demo.getSample (ch, i)) > 0.85f ? 1 : 0;
        // Just the surroundings: silent input, vibes up full.
        const auto life = runScene (s, silence, demoCues[(size_t) s], 100.0f);
        std::printf ("%-18s %8.2f %6.2f %10.2f %6.2f %6.3f %12.2f %10.2f   %s\n", elsewhere::sceneInfos[(size_t) s].name,
                     loud, loud - inLoud, actionLoud, demo.getMagnitude (0, n), 100.0 * hot / (2.0 * n),
                     loudnessDb (life, settle, n) - loud, life.getMagnitude (0, n), bad ? "NaN!" : "ok");
        writeWav (life, outDir.getChildFile ("life_" + String (s + 1) + ".wav"));

        auto name = String (s + 1).paddedLeft ('0', 2) + "_" + String (elsewhere::sceneInfos[(size_t) s].name).replace (" ", "_").toLowerCase();
        writeWav (demo, outDir.getChildFile (name + ".wav"));
    }

    // Latency check: an impulse through every scene should come out at sample 0.
    std::cout << "\nlatency (first sample above -40 dB):";
    for (int s = 0; s < elsewhere::numScenes; ++s)
    {
        AudioBuffer<float> imp (2, 4096);
        imp.clear();
        imp.setSample (0, 0, 1.0f);
        imp.setSample (1, 0, 1.0f);
        auto out = runScene (s, imp, {}, 0.0f);
        int first = -1;
        for (int i = 0; i < 4096 && first < 0; ++i)
            if (std::abs (out.getSample (0, i)) > 0.01f) first = i;
        std::cout << " " << first;
    }
    std::cout << "\n";

    for (int s = 0; s < elsewhere::numScenes; ++s)
    {
        snapshot (s, false, false, outDir.getChildFile ("ui_" + String (s + 1) + ".png"));
        snapshot (s, true, false, outDir.getChildFile ("ui_" + String (s + 1) + "_action.png"));
    }
    snapshot (2, false, true, outDir.getChildFile ("ui_studio.png"));

    // The logo on its own, transparent, for the website.
    {
        const float scale = 3.0f;
        const auto area = MainView::logoBounds();
        Image logo (Image::ARGB, roundToInt (area.getWidth() * scale), roundToInt (area.getHeight() * scale), true);
        {
            Graphics g (logo);
            g.addTransform (AffineTransform::translation (-area.getX(), -area.getY()).scaled (scale));
            MainView::paintLogo (g);
        }
        const auto file = outDir.getChildFile ("logo.png");
        file.deleteFile();
        FileOutputStream os (file);
        PNGImageFormat().writeImageToStream (logo, os);
    }

    // Living scenes, simulated for real.
    snapshot (1, false, false, outDir.getChildFile ("ui_2_people.png"), &input, 26.0);
    snapshot (1, false, false, outDir.getChildFile ("ui_2_puke.png"), &input, 12.0, 1);
    snapshot (2, false, false, outDir.getChildFile ("ui_3_people.png"), &input, 21.0);
    snapshot (2, false, false, outDir.getChildFile ("ui_3_puke.png"), &input, 21.0, 4);
    snapshot (3, false, false, outDir.getChildFile ("ui_4_traffic.png"), &input, 14.0);
    snapshot (0, true, false, outDir.getChildFile ("ui_1_sorry.png"), &input, 2.0);
    std::cout << "wrote renders to " << outDir.getFullPathName() << "\n";
    return 0;
}
