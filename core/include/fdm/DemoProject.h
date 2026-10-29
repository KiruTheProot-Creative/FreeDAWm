#pragma once

#include "fdm/Engine.h"

namespace fdm {

/** Fills an empty engine with a small starter session: 4 channels
    (Drums, Bass, Keys, Lead) and two scenes. Used for the first launch
    of the app, for the offline renderer, and in tests. */
void buildDemoProject(Engine& engine);

}  // namespace fdm
