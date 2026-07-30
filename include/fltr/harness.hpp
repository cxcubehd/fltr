#pragma once

#include <string>

#include "fltr/paint/display_list.hpp"
#include "fltr/render/object.hpp"

namespace fltr {

/// Headless inspection. There is no renderer during most of this work, so
/// correctness has to be observable without pixels; these produce a stable,
/// diffable textual form of what the pipeline actually built.
///
/// Output is deliberately terse and deterministic: no addresses, no timings,
/// floats via a fixed %.6g with -0 normalised.

/// One line per render object, indented by depth.
///   View size=200x100
///     Padding size=200x100 pad=all(8)
///       Row size=184x84
std::string dumpRenderTree(const RenderObject& root);

/// One line per command, indented between push/pop, recursing into embedded
/// sub-lists so a repaint boundary's contents are visible in place.
std::string dumpDisplayList(const DisplayList& list);

/// The scene as the consumer would receive it, including the root list.
std::string dumpScene(const Scene& scene);

}  // namespace fltr
