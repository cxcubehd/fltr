# The interactive demo. Off by default at the top level, because the library and
# fltr_tests must keep configuring and building with no network and no raylib.

include(FetchContent)

set(FLTR_DEMO_RAYLIB_TAG "6.0" CACHE STRING
    "raylib release the demo fetches when no installed raylib is found")

# Find first, fetch second: a distribution's raylib is preferred when it is new
# enough, and the pinned release is the fallback that makes a clean clone build.
find_package(raylib ${FLTR_DEMO_RAYLIB_TAG} QUIET)
if(raylib_FOUND)
  message(STATUS "fltr demo: using installed raylib ${raylib_VERSION}")
else()
  message(STATUS "fltr demo: fetching raylib ${FLTR_DEMO_RAYLIB_TAG}")
  # Static, so the demo is one binary with no runtime search path. raylib reads
  # BUILD_SHARED_LIBS rather than offering a switch of its own.
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG ${FLTR_DEMO_RAYLIB_TAG}
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
  )
  FetchContent_MakeAvailable(raylib)
endif()

if(MSVC)
  # The library's warning set is written for GCC and Clang; /utf-8 is what MSVC
  # needs to read these sources, which are UTF-8.
  set(FLTR_DEMO_WARNINGS /W4 /permissive- /utf-8)
else()
  set(FLTR_DEMO_WARNINGS ${FLTR_WARNINGS})
endif()

# ---------------------------------------------------------------------------
# The demo's own UI, which knows nothing about raylib
# ---------------------------------------------------------------------------
#
# Splitting this out is not packaging for its own sake: it is what makes the
# boundary checkable. If raylib ever leaks into a widget, this target stops
# linking, and it is also what lets the demo's tests drive the real pages
# through the headless harness with no window.

add_library(fltr_demo_ui STATIC
  ui/theme.cpp
  ui/strings.cpp
  ui/scroll.cpp
  ui/drag.cpp
  ui/button.cpp
  ui/controls.cpp
  ui/menu.cpp
  ui/panel.cpp
  app/servers.cpp
  app/page_title.cpp
  app/page_settings.cpp
  app/page_servers.cpp
  app/root.cpp
)
target_link_libraries(fltr_demo_ui PUBLIC fltr)
target_include_directories(fltr_demo_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(fltr_demo_ui PRIVATE ${FLTR_DEMO_WARNINGS})

# ---------------------------------------------------------------------------
# The application
# ---------------------------------------------------------------------------

add_executable(fltr_demo
  platform/raylib_text.cpp
  platform/raylib_renderer.cpp
  platform/input.cpp
  platform/overlay.cpp
  app/main.cpp
)
target_link_libraries(fltr_demo PRIVATE fltr_demo_ui raylib)
target_compile_options(fltr_demo PRIVATE ${FLTR_DEMO_WARNINGS})

# ---------------------------------------------------------------------------
# The demo's tests
# ---------------------------------------------------------------------------
#
# Running the demo is not the evidence; these are. They drive the real pages
# through the same harness the library's own tests use, with a controlled time
# source and no window at all.


