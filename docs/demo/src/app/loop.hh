#pragma once

#include "fltr/paint/display_list.hpp"

namespace demo {

class App;

/// The frame, as named steps. Each one is a separate function on purpose: this
/// is the shape of a game loop that drives fltr, and the ordering constraints
/// between the steps are the whole subject of the "driving from a game loop"
/// guide.
///
/// Input goes in as it arrives and is unrelated to the frame; the frame is one
/// call to `drawFrame`, and what comes out of it is a `Scene` to submit.

float advanceClock(App& app);
void pumpWindowEvents(App& app);
void pumpInput(App& app);
void stepSimulation(App& app, float dt);
fltr::Scene buildAndLayout(App& app, float dt);
void applyCursor(App& app);
void present(App& app, const fltr::Scene& scene);

/// Runs all of the above, in order, and hands back what was submitted.
fltr::Scene runFrame(App& app);

}  // namespace demo
