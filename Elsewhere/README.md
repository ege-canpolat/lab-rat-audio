# Elsewhere

*Hear your mix from somewhere else.* A mix-check plugin (VST3 / AU / Standalone) that
teleports your track to six places real people actually hear music:

| CH | Place | The big button | Vibes knob |
|----|-------|----------------|------------|
| 1 | **Next Door**: through your neighbour's wall | **KNOCK!** (they shout back, then turn it down… for a bit) | on/off: nothing else happens next door |
| 2 | **Club Toilets**: muffled club, tiled room | **OPEN DOOR** | people coming and going: footsteps, flushes, taps, the odd (silent, cartoon) puke |
| 3 | **Smoking Area**: outside, door swinging | **OPEN DOOR** | smokers coming and going, close-up chatter (with subtitles) and laughs, other groups and the city in the background, and now and then someone staggering out to be sick by the bin |
| 4 | **In The Car**: parked downtown, cabin boom, dash tweeters | **STEP OUTSIDE** (hear what the street hears) | city traffic with Doppler, horns, sirens, your engine idling |
| 5 | **Phone Speaker**: phone on the kitchen table | **IN POCKET** | speaker strain + notifications (a little soap opera, in order) |
| 6 | **Festival**: campsite, 500 m from the stage | **GUST!** | wind |

**MIX** blends back to the dry signal, **VOLUME** is output gain, and **BACK TO STUDIO** is an instant
A/B to your untouched mix. The places are loudness-matched, so switching between them doesn't jump in
level. Put it last on your master bus.

## Build (macOS)

Needs Xcode command line tools, CMake and Ninja (`brew install cmake ninja`). JUCE lives in `../JUCE`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Elsewhere_All
```

The build copies the plugin to `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`.
The standalone app ends up in `build/Elsewhere_artefacts/Release/Standalone`.

## Sharing it

For other Macs (Intel or Apple silicon, macOS 11+) build universal, then wrap it in an installer with the
repository's shared script (see `../README.md`):

```bash
cmake -S . -B build-share -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build-share --target Elsewhere_VST3 Elsewhere_AU
../tools/make_pkg.sh Elsewhere 0.1.0 build-share/Elsewhere_artefacts/Release Installer ../docs/elsewhere/download/Elsewhere-0.1.0-mac.pkg
```

It isn't Developer ID signed, so the first time people open the installer macOS sends them to
System Settings > Privacy & Security > Open Anyway. The download page explains this.

## Test tools

- `build/ElsewhereRender_artefacts/Release/ElsewhereRender Renders [your.wav]` renders a test mix (or
  your own file) through every place into `Renders/` as WAVs (plus `life_N.wav`: just the
  surroundings), prints loudness, clipping and latency stats, and saves UI snapshots.
  `ElsewhereRender --sources` prints the level of each surroundings synth on its own and bounces
  `Renders/sfx_*.wav` (knock, voice) for auditioning.
- `build/ElsewhereHostTest_artefacts/Release/ElsewhereHostTest [plugin path]` loads the installed
  VST3/AU like a DAW would, runs a minute of audio, round-trips its state and opens the editor.
- `auval -v aufx Elsw LbRt` runs Apple's Audio Unit validation.

## Code map

- `Source/DSP.h`: filters (Cytomic SVF), synthesised room impulse responses, door and impact models.
- `Source/Life.h`: the surroundings. Knocks, footsteps, Simlish voices, water,
  the people simulation (`Crowd`) and city traffic.
- `Source/Scenes.cpp`: the six places, plus the loudness-matching table (`sceneMakeupDb`).
- `Source/PluginProcessor.*`: parameters, scene crossfading, mix/studio/output.
- `Source/Cartoon.*`: the 2000s cartoon toolkit (palette, outlined shapes, blobs, buttons, knobs).
- `Source/SceneArt.cpp`: the animated TV scenes.
- `Source/PluginEditor.*`: window layout and the parameter wiring.

Fonts: Luckiest Guy (Apache 2.0) and Fredoka (OFL), both from Google Fonts.

## Licence

GNU AGPL v3, like the rest of the repository: see `../LICENSE` and `../README.md`.
