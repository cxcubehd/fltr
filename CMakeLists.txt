cmake_minimum_required(VERSION 3.24)
project(fltr LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(FLTR_BUILD_TESTS "Build the fltr test suite" ON)
option(FLTR_BUILD_EXAMPLES "Build the fltr example screens" ON)
option(FLTR_ENABLE_CHECKS "Enable layout-protocol contract checks" ON)

file(GLOB FLTR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(fltr STATIC ${FLTR_SOURCES})
target_include_directories(fltr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fltr PUBLIC cxx_std_23)

if(FLTR_ENABLE_CHECKS)
  target_compile_definitions(fltr PUBLIC FLTR_CHECKS=1)
endif()

if(MSVC)
  # Conformance, not taste: without these MSVC reports C++98 in `__cplusplus`,
  # keeps its non-conforming preprocessor, and reads the sources as the active
  # code page rather than as UTF-8. They are PUBLIC because a consumer compiling
  # fltr's headers needs the same language, not just the same warnings.
  target_compile_options(fltr PUBLIC /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8 /bigobj)
  set(FLTR_WARNINGS /W4)
else()
  set(FLTR_WARNINGS
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
    # Designated initialisers with omitted trailing fields are the authoring
    # style; the omitted fields take their in-class defaults by design.
    -Wno-missing-field-initializers
  )
endif()
target_compile_options(fltr PRIVATE ${FLTR_WARNINGS})

if(FLTR_BUILD_TESTS)
  enable_testing()
  file(GLOB FLTR_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
  add_executable(fltr_tests ${FLTR_TEST_SOURCES})
  target_link_libraries(fltr_tests PRIVATE fltr)
  target_include_directories(fltr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_options(fltr_tests PRIVATE ${FLTR_WARNINGS})
  add_test(NAME fltr_tests COMMAND fltr_tests)
endif()

if(FLTR_BUILD_EXAMPLES)
  file(GLOB FLTR_EXAMPLE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp)
  if(FLTR_EXAMPLE_SOURCES)
    add_executable(fltr_hud ${FLTR_EXAMPLE_SOURCES})
    target_link_libraries(fltr_hud PRIVATE fltr)
    target_compile_options(fltr_hud PRIVATE ${FLTR_WARNINGS})
  endif()
endif()
