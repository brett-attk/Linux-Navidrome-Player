cmake_minimum_required(VERSION 3.20)
project(radio-player VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(GST_AUDIO REQUIRED IMPORTED_TARGET gstreamer-audio-1.0)
pkg_check_modules(LIBSOUP REQUIRED IMPORTED_TARGET libsoup-3.0)
pkg_check_modules(GDKPIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)

find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json package not found; expecting system include <nlohmann/json.hpp>")
endif()

# Shared non-UI library used by the app and unit tests.
add_library(radio-core STATIC
  src/config.cpp
  src/session.cpp
  src/cache/cover_cache.cpp
  src/subsonic/client.cpp
  src/subsonic/parse.cpp
  src/audio/player.cpp
  src/queue/play_queue.cpp
  src/discovery/lan_discovery.cpp
)

target_include_directories(radio-core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(radio-core PUBLIC
  PkgConfig::GTK4
  PkgConfig::GSTREAMER
  PkgConfig::GST_AUDIO
  PkgConfig::LIBSOUP
  PkgConfig::GDKPIXBUF
)

if(nlohmann_json_FOUND)
  target_link_libraries(radio-core PUBLIC nlohmann_json::nlohmann_json)
endif()

target_compile_options(radio-core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(radio-player
  src/main.cpp
  src/ui/connect_dialog.cpp
  src/ui/cover_loader.cpp
  src/ui/eq_dialog.cpp
  src/ui/beat_stage.cpp
  src/ui/main_window.cpp
)

target_link_libraries(radio-player PRIVATE
  radio-core
  PkgConfig::ADWAITA
)

target_compile_options(radio-player PRIVATE -Wall -Wextra -Wpedantic)

# Resolve icons from the source tree while developing / running from build/.
target_compile_definitions(radio-player PRIVATE
  RADIO_ICON_DIR="${CMAKE_SOURCE_DIR}/data/icons"
)

enable_testing()
add_executable(radio-tests
  tests/test_main.cpp
  tests/test_play_queue.cpp
  tests/test_util_safe.cpp
  tests/test_eq_settings.cpp
  tests/test_config_session.cpp
  tests/test_cover_cache.cpp
  tests/test_subsonic_parse.cpp
  tests/test_client_urls.cpp
  tests/test_beat_math.cpp
  tests/test_scrobble.cpp
)

target_include_directories(radio-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(radio-tests PRIVATE radio-core)
target_compile_options(radio-tests PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME radio-tests COMMAND radio-tests)

include(GNUInstallDirs)

configure_file(
  ${CMAKE_SOURCE_DIR}/data/ai.workspace.radioplayer.desktop.in
  ${CMAKE_BINARY_DIR}/ai.workspace.radioplayer.desktop
  @ONLY
)

install(TARGETS radio-player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/icons/hicolor
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
install(FILES ${CMAKE_BINARY_DIR}/ai.workspace.radioplayer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)