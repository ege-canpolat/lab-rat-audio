// Loads the installed plugin the way a DAW does and gives it a workout.
//   ElsewhereHostTest [path/to/Elsewhere.vst3 | .component]

#include <juce_audio_utils/juce_audio_utils.h>

using namespace juce;

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI gui;
    AudioPluginFormatManager formats;
    addDefaultFormatsToManager (formats);

    const String path = argc > 1 ? String (argv[1])
                                 : File::getSpecialLocation (File::userHomeDirectory)
                                       .getChildFile ("Library/Audio/Plug-Ins/VST3/Elsewhere.vst3").getFullPathName();

    OwnedArray<PluginDescription> found;
    for (auto* format : formats.getFormats())
        if (format->fileMightContainThisPluginType (path))
            format->findAllTypesForFile (found, path);
    if (found.isEmpty()) { std::cerr << "no plugin found in " << path << "\n"; return 1; }
    std::cout << "found: " << found[0]->name << " (" << found[0]->pluginFormatName << ")\n";

    String error;
    auto plugin = formats.createPluginInstance (*found[0], 48000.0, 256, error);
    if (plugin == nullptr) { std::cerr << "load failed: " << error << "\n"; return 1; }

    plugin->setPlayConfigDetails (2, 2, 48000.0, 256);
    plugin->prepareToPlay (48000.0, 256);

    auto params = plugin->getParameters();
    std::cout << "parameters:";
    for (auto* p : params)
        std::cout << " [" << p->getName (32) << " = " << p->getCurrentValueAsText() << "]";
    std::cout << "\n";

    AudioBuffer<float> buffer (2, 256);
    MidiBuffer midi;
    Random rng (1);
    bool finite = true;
    double phase = 0;
    const int blocksPerScene = (int) (10.0 * 48000.0 / 256);
    const auto start = Time::getMillisecondCounterHiRes();

    for (int scene = 0; scene < 6; ++scene)
    {
        params[0]->setValueNotifyingHost ((float) scene / 5.0f);
        for (int b = 0; b < blocksPerScene; ++b)
        {
            params[3]->setValueNotifyingHost ((b / 200) % 2 == 1 ? 1.0f : 0.0f);   // poke the action button
            for (int i = 0; i < 256; ++i)
            {
                phase += 55.0 / 48000.0;
                const float x = 0.4f * (float) std::sin (6.2831853 * phase) + 0.1f * (rng.nextFloat() * 2 - 1);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }
            plugin->processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    finite &= std::isfinite (buffer.getSample (ch, i));
        }
    }
    const double seconds = (Time::getMillisecondCounterHiRes() - start) * 0.001;
    std::cout << "processed 60 s of audio in " << seconds << " s  ->  ~" << (100.0 * seconds / 60.0)
              << "% of one core;  output finite: " << (finite ? "yes" : "NO") << "\n";

    MemoryBlock state;
    params[0]->setValueNotifyingHost (0.8f);   // phone speaker
    plugin->getStateInformation (state);
    params[0]->setValueNotifyingHost (0.0f);
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    std::cout << "state round-trip: " << params[0]->getCurrentValueAsText() << " (expect PHONE SPEAKER)\n";

    std::unique_ptr<AudioProcessorEditor> editor (plugin->createEditorAndMakeActive());
    std::cout << "editor: " << (editor != nullptr ? String (editor->getWidth()) + " x " + String (editor->getHeight()) : String ("none")) << "\n";
    editor.reset();

    plugin->releaseResources();
    return finite ? 0 : 2;
}
