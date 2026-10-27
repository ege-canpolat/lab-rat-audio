#pragma once

#include <array>

namespace elsewhere
{
// How the big action button behaves in each scene.
enum class ActionMode { hold, toggle, trigger };

struct SceneInfo
{
    const char* name;
    const char* tagline;
    const char* action;     // button label
    const char* actionOn;   // label while engaged (toggle scenes)
    ActionMode mode;
};

inline constexpr int numScenes = 6;

inline constexpr std::array<SceneInfo, numScenes> sceneInfos {{
    { "NEXT DOOR",
      "Your neighbour is blasting your track. You're on the other side of the wall.",
      "KNOCK!", "KNOCK!", ActionMode::trigger },
    { "CLUB TOILETS",
      "Hiding in the club toilets. Somebody keeps opening the door.",
      "OPEN DOOR", "CLOSE DOOR", ActionMode::toggle },
    { "SMOKING AREA",
      "Outside the club. Every time the door swings open, the music spills out.",
      "OPEN DOOR", "CLOSE DOOR", ActionMode::toggle },
    { "IN THE CAR",
      "Parked downtown, stereo up. Step out to hear what the street hears.",
      "STEP OUTSIDE", "GET BACK IN", ActionMode::toggle },
    { "PHONE SPEAKER",
      "Your mate plays it off their phone on the kitchen table.",
      "IN POCKET", "TAKE IT OUT", ActionMode::toggle },
    { "FESTIVAL",
      "Back at the campsite, half a kilometre from the main stage.",
      "GUST!", "GUST!", ActionMode::trigger },
}};

} // namespace elsewhere
