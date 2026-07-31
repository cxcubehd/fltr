#include "ui/button.hpp"

#include "fltr/widgets/animated.hpp"
#include "fltr/widgets/basic.hpp"
#include "ui/layout.hpp"
#include "ui/theme.hpp"

namespace fltrdemo {

using namespace fltr;

namespace {

/// Fast enough that the button feels attached to the cursor, slow enough that
/// the interpolation is visible. Hover is slower than press on purpose: a press
/// has to feel instant.
constexpr AnimationDriver::Config kHover{.duration = 0.14f, .curve = Curves::easeOut};
constexpr AnimationDriver::Config kPress{.duration = 0.08f, .curve = Curves::easeOut};

class ButtonState final : public State<Button> {
public:
  WidgetRef build(BuildContext& context) override {
    const Theme& theme = themeOf(context);
    const bool enabled = widget().enabled();
    const bool lit = enabled && (hovered_ || widget().selected());
    const bool tab = widget().kind() == ButtonKind::Tab;

    BoxDecoration decoration{
        .color = pressed_          ? theme.accentDim
                 : widget().selected() ? theme.surfaceRaised
                 : lit             ? theme.surfaceRaised
                 : tab             ? Color::transparent()
                                   : theme.surface,
        .radius = BorderRadius::all(tab ? 0.0f : theme.radius()),
        .borderColor = tab ? Color::transparent() : (lit ? theme.accent : theme.border),
        .borderWidth = tab ? 0.0f : theme.hairline(),
    };

    TextStyle style = theme.body();
    style.color = !enabled            ? theme.textDim
                  : (lit || pressed_) ? theme.textBright
                                      : theme.text;

    const float padX = theme.unit() * (widget().kind() == ButtonKind::Menu   ? 2.0f
                                       : widget().kind() == ButtonKind::Tab ? 1.0f
                                                                            : 1.5f);
    WidgetRef body = AnimatedDecoration::make({
        .decoration = decoration,
        .animation = kHover,
        .child = Padding::make({
            .padding = EdgeInsets::symmetric(padX, theme.unit()),
            .child = Align::make({
                // Height wraps the label; width fills whatever the button was
                // given, which is what centres the label in a fixed-width menu
                // button and shrink-wraps a compact one.
                .heightFactor = 1.0f,
                .child = text(widget().label(), style),
            }),
        }),
    });

    if (widget().width() > 0.0f) {
      body = ConstrainedBox::make({
          .constraints = BoxConstraints::tightWidth(widget().width()),
          .child = body,
      });
    }

    // The press scales about the button's own centre, and the transform never
    // touches layout: neighbours do not move when this one is pressed.
    body = AnimatedTransform::make({
        .transform = pressed_ ? Transform2D::scaling(0.97f, 0.97f) : Transform2D::identity(),
        .animation = kPress,
        .child = body,
    });

    body = AnimatedOpacity::make({
        .opacity = enabled ? 1.0f : 0.45f,
        .animation = kHover,
        .child = body,
    });

    // A disabled button takes no callbacks at all, so its recognizer never
    // enters the arena and its region never asks to be hovered -- and it still
    // swallows the press, which is what Opaque is for.
    return RepaintBoundary::make({
        .child = Pointer::make({
            .behavior = HitTestBehavior::Opaque,
            .onEnter = enabled ? Callback<void()>([this] { setHovered(true); }) : Callback<void()>{},
            .onExit = enabled ? Callback<void()>([this] { setHovered(false); }) : Callback<void()>{},
            .onTapDown = enabled ? Callback<void()>([this] { setPressed(true); }) : Callback<void()>{},
            .onTap = enabled ? Callback<void()>([this] {
                       setPressed(false);
                       widget().onPressed()();
                     })
                             : Callback<void()>{},
            .onTapCancel =
                enabled ? Callback<void()>([this] { setPressed(false); }) : Callback<void()>{},
            .child = body,
        }),
    });
  }

private:
  void setHovered(bool hovered) {
    if (hovered_ == hovered) return;
    setState([&] { hovered_ = hovered; });
  }
  void setPressed(bool pressed) {
    if (pressed_ == pressed) return;
    setState([&] { pressed_ = pressed; });
  }

  bool hovered_ = false;
  bool pressed_ = false;
};

}  // namespace

std::unique_ptr<State<Button>> Button::createState() const {
  return std::make_unique<ButtonState>();
}

}  // namespace fltrdemo
