# Lab Rat Audio

Hobby audio plugins by Lab Rat Audio, all in one place. Each plugin lives in its own folder and has its own look;
the website in `docs/` stays neutral and links to each one's page.

```
VST_Playground/           (the local folder; the repository is lab-rat-audio)
├── JUCE/                 the JUCE framework (shared by every plugin)
├── tools/make_pkg.sh     builds a macOS installer (.pkg) for any plugin here
├── Elsewhere/            a plugin: source, CMake project, installer screens (Installer/)
├── docs/                 the website (GitHub Pages serves this folder)
│   ├── index.html        the hub: one card per plugin
│   ├── assets/           the hub's neutral style, shared top bar + footer
│   └── elsewhere/        Elsewhere's own page, style, screenshots and download
└── <NextPlugin>/         ...
```

## Releasing a plugin for Mac

```bash
cd Elsewhere
cmake -S . -B build-share -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build-share --target Elsewhere_VST3 Elsewhere_AU
cd ..
tools/make_pkg.sh Elsewhere 0.1.0 Elsewhere/build-share/Elsewhere_artefacts/Release Elsewhere/Installer docs/elsewhere/download/Elsewhere-0.1.0-mac.pkg
```

The installer is unsigned, so on first open people go to System Settings > Privacy & Security >
Open Anyway (the plugin page explains this). With an Apple Developer ID, run the script with
`SIGN_IDENTITY="Developer ID Installer: ..."` and notarise the result instead.

## Adding the next plugin

1. Make a folder next to `Elsewhere/` with its own `CMakeLists.txt` (`add_subdirectory(../JUCE ...)`).
2. Give it an `Installer/` folder with `welcome.html` and `conclusion.html`.
3. Make `docs/<plugin>/` with its own `index.html` and stylesheet. Keep the shared top bar and footer
   from `docs/assets/playground.css` so people can always get back to the hub; everything else can look
   however that plugin wants.
4. Copy the card block in `docs/index.html`.

## Publishing the site

The repository is published from this folder. In GitHub, in the repository's Settings > Pages choose
"Deploy from a branch", branch `main`, folder `/docs`. The site appears at
`https://<username>.github.io/<repository>/`.

JUCE is best added as a git submodule rather than committed:
`git submodule add https://github.com/juce-framework/JUCE.git JUCE`.

## Licence

Copyright (C) 2026 Ege Canpolat (Lab Rat Audio).

The code in this repository is free software under the GNU Affero General Public License v3
(see [LICENSE](LICENSE)): you can use, study, change and share it, as long as anything you share
built from it stays under the same licence, with its source. That's also what JUCE's open-source
licence asks of plugins built with it.

Not covered by that licence: JUCE (its own licence, see `JUCE/LICENSE.md`) and the bundled fonts
(Luckiest Guy under Apache 2.0 and Fredoka under the SIL Open Font License; their licence files are
in `Elsewhere/Assets/`).
