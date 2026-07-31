#pragma once

#include <memory>
#include <utility>

#include "fltr/widgets/binding.hpp"

namespace fltrtest {

/// Drives a WidgetBinding from a builder the test can change between frames,
/// which is how "the game pushed different state this frame" is expressed.
class Harness {
public:
  explicit Harness(fltr::Size surface = {200, 100}) : binding_(surface, text_) {}

  template <class BuildRoot>
  void attach(BuildRoot&& buildRoot) {
    root_ = std::make_unique<RootHolder<std::decay_t<BuildRoot>>>(
        std::forward<BuildRoot>(buildRoot));
    binding_.attachRoot([this] { return root_->build(); });
  }

  /// A frame, optionally with time having passed since the last one -- which is
  /// how animation is driven here: explicitly, not from a real clock.
  fltr::Scene frame(float seconds = 0.0f) { return binding_.drawFrame(seconds); }

  fltr::WidgetBinding& binding() noexcept { return binding_; }
  fltr::BuildOwner& buildOwner() noexcept { return binding_.buildOwner(); }
  fltr::PipelineOwner& pipeline() noexcept { return binding_.pipeline(); }
  fltr::PointerBinding& pointers() noexcept { return binding_.pointers(); }
  fltr::Element& rootElement() const noexcept { return *binding_.rootElement(); }
  fltr::RenderView& view() const noexcept { return *binding_.renderView(); }
  fltr::MonospaceTextService& textService() noexcept { return text_; }

private:
  struct RootBuilder {
    virtual ~RootBuilder() = default;
    virtual fltr::WidgetRef build() = 0;
  };
  template <class F>
  struct RootHolder final : RootBuilder {
    explicit RootHolder(F f) : fn(std::move(f)) {}
    fltr::WidgetRef build() override { return fn(); }
    F fn;
  };

  fltr::MonospaceTextService text_;
  fltr::WidgetBinding binding_;
  std::unique_ptr<RootBuilder> root_;
};

/// A stateful root whose build is driven by a test-owned function, so the tree
/// can change shape between frames without re-attaching the binding.
class Scripted;

class ScriptedState final : public fltr::State<Scripted> {
public:
  fltr::WidgetRef build(fltr::BuildContext& context) override;
};

class Scripted final : public fltr::Configure<Scripted, fltr::StatefulWidget> {
public:
  using Script = fltr::WidgetRef (*)(void*);

  struct Args {
    fltr::Key key;
    Script script = nullptr;
    void* context = nullptr;
  };

  explicit Scripted(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Scripted"; }
  fltr::WidgetRef run() const { return args_.script(args_.context); }

  std::unique_ptr<fltr::State<Scripted>> createState() const {
    return std::make_unique<ScriptedState>();
  }

private:
  Args args_;
};

inline fltr::WidgetRef ScriptedState::build(fltr::BuildContext&) { return widget().run(); }

inline fltr::Element& elementFor(fltr::Element& root, fltr::WidgetType type,
                                 fltr::Key key = fltr::Key::none()) {
  fltr::Element* found = nullptr;
  const auto walk = [&](auto&& self, fltr::Element& element) -> void {
    if (found) return;
    if (element.widget()->type() == type && element.widget()->key() == key) {
      found = &element;
      return;
    }
    element.visitChildren([&](fltr::Element& child) { self(self, child); });
  };
  walk(walk, root);
  FLTR_EXPECTS(found != nullptr, "no such element in this tree");
  return *found;
}

/// A root that re-runs `script` on demand. `rebuild()` is the widget-layer
/// equivalent of a game pushing a whole new frame of state.
template <class Script>
class ScriptedRoot {
public:
  explicit ScriptedRoot(Harness& harness, Script script) : script_(std::move(script)) {
    harness.attach([this] {
      return Scripted::make({
          .script = [](void* self) { return static_cast<ScriptedRoot*>(self)->script_(); },
          .context = this,
      });
    });
    element_ = &harness.rootElement();
  }

  void rebuild() { elementFor(*element_, fltr::widgetTypeOf<Scripted>()).markNeedsBuild(); }

private:
  Script script_;
  fltr::Element* element_ = nullptr;
};

}  // namespace fltrtest
