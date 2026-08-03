cmake_minimum_required(VERSION 3.24)
project(fltr_demo LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

include(FetchContent)

# fltr defaults FLTR_BUILD_TESTS and FLTR_BUILD_EXAMPLES to ON, and they are
# plain option()s rather than prefixed, dependent ones -- so a consumer that
# does not turn them off here builds fltr's whole test suite as a side effect of
# depending on it. See the "Gaps and rework" page.
set(FLTR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLTR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Configure with -DFETCHCONTENT_SOURCE_DIR_FLTR=/path/to/fltr to build against a
# checkout instead of the tag -- which is what this repository's own CI does,
# since the demo lives inside it.
FetchContent_Declare(
  fltr
  GIT_REPOSITORY https://github.com/cxcubehd/fltr.git
  GIT_TAG main
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE
)

# A UI is only as legible as its font, and the fallback -- raylib's built-in
# bitmap face -- is not legible at these sizes. Fetching the real one keeps a
# 300KB binary out of the repository without leaving the demo's appearance up to
# whichever monospace face the machine happens to have installed.
FetchContent_Declare(
  jetbrains_mono
  URL https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip
  URL_HASH SHA256=6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(jetbrains_mono)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)

# raylib picks its backend from this, and only this: `emcmake cmake` sets
# EMSCRIPTEN, and the rest of the web build follows from the same branch below.
if(EMSCRIPTEN)
  set(PLATFORM Web CACHE STRING "" FORCE)
endif()

FetchContent_MakeAvailable(fltr raylib)

add_executable(demo
  src/main.cc

  src/app/app.cc
  src/app/clock.cc
  src/app/loop.cc

  src/platform/backend.cc
  src/platform/input.cc
  src/platform/text.cc
  src/platform/window.cc

  src/game/field.cc
  src/game/levels.cc
  src/game/session.cc
  src/game/ship.cc

  src/ui/root.cc
  src/ui/router.cc
  src/ui/theme.cc
  src/ui/screens/hud.cc
  src/ui/screens/level_select.cc
  src/ui/screens/main_menu.cc
  src/ui/screens/pause.cc
  src/ui/screens/settings.cc
  src/ui/widgets/menu_button.cc
  src/ui/widgets/panel.cc
  src/ui/widgets/readout.cc
  src/ui/widgets/slider_row.cc
  src/ui/widgets/styled_surface.cc
  src/ui/widgets/toggle_row.cc
)

target_include_directories(demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# fltr exports no namespaced alias, so this is the bare target name.
target_link_libraries(demo PRIVATE fltr raylib)

set(DEMO_FONT "${jetbrains_mono_SOURCE_DIR}/fonts/ttf/JetBrainsMono-Regular.ttf")

if(EMSCRIPTEN)
  # There is no host filesystem to fall back to, so the font is packaged into
  # the module's virtual one and the app opens it at the path it was mounted at.
  target_compile_definitions(demo PRIVATE
    DEMO_ASSET_DIR="/assets"
    DEMO_FONT_FILE="/JetBrainsMono-Regular.ttf"
  )
  # Named for where it is served from rather than for the target: the page is
  # published as a directory, not as a file with a name in it.
  set_target_properties(demo PROPERTIES OUTPUT_NAME index SUFFIX ".html")
  target_link_options(demo PRIVATE
    -sUSE_GLFW=3
    -sALLOW_MEMORY_GROWTH=1
    -sGL_ENABLE_GET_PROC_ADDRESS=1
    -sSTACK_SIZE=1mb
    --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    --preload-file "${DEMO_FONT}@/JetBrainsMono-Regular.ttf"
  )
else()
  target_compile_definitions(demo PRIVATE
    DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    DEMO_FONT_FILE="${DEMO_FONT}"
  )
endif()

if(MSVC)
  target_compile_options(demo PRIVATE /W4 /MP)
else()
  target_compile_options(demo PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()
