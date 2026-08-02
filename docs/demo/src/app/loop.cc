#include "app/loop.hh"

#include <cmath>

#include "raylib.h"
#include "app/app.hh"
#include "game/session.hh"

namespace demo {

using fltr::Offset;
using fltr::Scene;

namespace {

/// The game is drawn by raylib directly; only the UI goes through fltr. Keeping
/// them separate is the point -- fltr is a UI library embedded in a renderer,
/// not a renderer.
void drawField(const Session& session, fltr::Size surface) {
  const Field& field = session.field();

  for (const Asteroid& rock : field.asteroids()) {
    const int sides = 5 + rock.tier;
    DrawPolyLinesEx(Vector2{rock.position.dx, rock.position.dy}, sides, rock.radius,
                    rock.angle * 57.2957795f, 1.5f, Color{0x8b, 0x8b, 0x99, 0xff});
  }

  for (const Bullet& bullet : field.bullets()) {
    DrawCircleV(Vector2{bullet.position.dx, bullet.position.dy}, 2.0f,
                Color{0x6e, 0x9b, 0xff, 0xff});
  }

  const Ship& ship = session.ship();
  if (ship.alive()) {
    const float heading = ship.heading() * 57.2957795f;
    DrawPolyLinesEx(Vector2{ship.position().dx, ship.position().dy}, 3, Ship::kRadius, heading,
                    2.0f, Color{0xec, 0xec, 0xef, 0xff});
    if (ship.thrusting()) {
      const Offset back = ship.position() -
                          Offset{std::cos(ship.heading()), std::sin(ship.heading())} *
                              (Ship::kRadius + 6.0f);
      DrawCircleV(Vector2{back.dx, back.dy}, 3.0f, Color{0xff, 0x6b, 0x6b, 0xff});
    }
  }

  (void)surface;
}

}  // namespace

float advanceClock(App& app) { return app.clock().advance(); }

void pumpWindowEvents(App& app) {
  // Pushing the same size again is a no-op, so this is unconditional.
  const fltr::Size surface = app.window().surface();
  app.binding().setSurface(surface);
  app.session().setBounds(surface);
}

void pumpInput(App& app) {
  // A menu takes the keyboard away from the ship without either side knowing
  // about the other: the UI answers "did I consume this", and what is left is
  // the game's.
  app.input().setGameHasFocus(app.gameHasFocus());
  app.input().pump(app.binding());
  if (app.input().backRequested()) app.back();
}

void stepSimulation(App& app, float dt) {
  app.session().tick(app.shipInput(), dt);
  // One push per frame of everything the UI might read. Values that did not
  // change notify nobody, so this is cheap even though it is unconditional.
  app.session().publish();
}

Scene buildAndLayout(App& app, float dt) {
  // The only call into the framework's frame. Animations advance, then what is
  // dirty is built, then what that made dirty is laid out and painted.
  return app.binding().drawFrame(dt);
}

void applyCursor(App& app) { app.window().applyCursor(app.binding().cursor()); }

void present(App& app, const Scene& scene) {
  BeginDrawing();
  ClearBackground(Color{0x0a, 0x0a, 0x0c, 0xff});
  if (app.screen().value() == Screen::Playing) drawField(app.session(), scene.surface);
  app.backend().submit(scene);
  EndDrawing();
}

Scene runFrame(App& app) {
  const float dt = advanceClock(app);
  pumpWindowEvents(app);
  pumpInput(app);
  stepSimulation(app, dt);
  const Scene scene = buildAndLayout(app, dt);
  applyCursor(app);
  app.refreshDebugText();
  present(app, scene);
  return scene;
}

}  // namespace demo
