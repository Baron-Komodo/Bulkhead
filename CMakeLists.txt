cmake_minimum_required(VERSION 3.16)
project(SeaBuilderDesktop LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# On MinGW, link the C++ runtime statically. Otherwise a libstdc++-6.dll from another
# toolchain earlier on PATH gets picked up at runtime and the process crashes on startup.
if(MINGW)
  add_link_options(-static-libstdc++ -static-libgcc)
endif()

# ---------------------------------------------------------------------------
# Pure game logic, no windowing dependency. Shared by the game and the tests.
# ---------------------------------------------------------------------------
add_library(sea_logic STATIC
  src/geo.cpp
  src/sea_route.cpp
  src/data.cpp
  src/cargo.cpp
  src/research.cpp
  src/voyage.cpp
  src/combat.cpp
  src/management.cpp
  src/game.cpp
  src/save.cpp
)
target_include_directories(sea_logic PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# ---------------------------------------------------------------------------
# raylib: use a system/package copy when one exists, otherwise fetch and build.
# ---------------------------------------------------------------------------
find_package(raylib 5.0 QUIET)

if(NOT raylib_FOUND)
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(PLATFORM Desktop CACHE STRING "" FORCE)
  FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(raylib)
endif()

add_executable(sea_builder
  src/main.cpp
  src/map_view.cpp
  src/ui.cpp
  src/fonts.cpp
  src/naval_view.cpp
  src/combat_view.cpp
)
target_include_directories(sea_builder PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(sea_builder PRIVATE sea_logic raylib)

# Copy the data assets next to the executable so it can be launched from the build dir.
add_custom_command(TARGET sea_builder POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/data
          $<TARGET_FILE_DIR:sea_builder>/data
)

# ---------------------------------------------------------------------------
# Screenshot checker: summarises a frame captured with SEA_BUILDER_SCREENSHOT,
# so the rendering can be verified without a display.
# ---------------------------------------------------------------------------
add_executable(frame_report tools/frame_report.cpp)
target_link_libraries(frame_report PRIVATE raylib)

# ---------------------------------------------------------------------------
# Logic test runner (headless).
# ---------------------------------------------------------------------------
add_executable(logic_tests tests/logic_tests.cpp)
target_include_directories(logic_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(logic_tests PRIVATE sea_logic)

enable_testing()
add_test(NAME logic_tests COMMAND logic_tests)
set_tests_properties(logic_tests PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  ENVIRONMENT "SEA_BUILDER_DATA=${CMAKE_CURRENT_SOURCE_DIR}/data"
)
