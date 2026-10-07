# Host build: the 800x480 todo simulator and the model unit tests.
#
#   cmake -S sim -B build/sim -G Ninja
#   cmake --build build/sim
#   ctest --test-dir build/sim --output-on-failure
#
# Mirrors the SDK simulator (third_party/muse-gadget-sdk/esp32/simulator): the
# same pinned LVGL 9.5.0 and SDL2, sized for the 5" board instead of 412x412.

cmake_minimum_required(VERSION 3.24)

project(todo_display_host VERSION 0.1.0 LANGUAGES C)

if(WIN32 OR NOT UNIX)
    message(FATAL_ERROR "The todo simulator supports Linux and macOS")
endif()

include(FetchContent)
find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TODO_SIM_BUILD_SIMULATOR "Build the SDL simulator (needs SDL2 and LVGL)" ON)
option(TODO_SIM_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SDK_ESP32_DIR "${REPO_DIR}/third_party/muse-gadget-sdk/esp32")
set(TODO_COMPONENT_DIR "${REPO_DIR}/components/todo_display")

if(NOT EXISTS "${SDK_ESP32_DIR}/avatar/muse_pixel.c")
    message(FATAL_ERROR "The SDK submodule is missing: run `git submodule update --init`")
endif()

if(TODO_SIM_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# cJSON ships with ESP-IDF on the device; the host fetches the same release.
FetchContent_Declare(
    cjson
    URL "https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz"
    URL_HASH "SHA256=3aa806844a03442c00769b83e99970be70fbef03735ff898f4811dd03b9f5ee5"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake # fetch only; cJSON.c is compiled below without its test suite
)
FetchContent_MakeAvailable(cjson)
add_library(cjson STATIC "${cjson_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson PUBLIC "${cjson_SOURCE_DIR}")

# The model is plain C: no LVGL, no ESP-IDF.
add_library(todo_model STATIC "${TODO_COMPONENT_DIR}/todo_model.c")
target_include_directories(todo_model PUBLIC "${TODO_COMPONENT_DIR}/include")
target_link_libraries(todo_model PUBLIC cjson)
target_compile_options(todo_model PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(todo_model PRIVATE _POSIX_C_SOURCE=200809L)

enable_testing()

add_executable(test_todo_model "${REPO_DIR}/tests/test_todo_model.c")
target_link_libraries(test_todo_model PRIVATE todo_model)
target_compile_options(test_todo_model PRIVATE -Wall -Wextra)
target_compile_definitions(test_todo_model PRIVATE _POSIX_C_SOURCE=200809L)
add_test(NAME todo_model COMMAND test_todo_model)

if(NOT TODO_SIM_BUILD_SIMULATOR)
    return()
endif()

# ---- SDL2 -----------------------------------------------------------------
find_package(SDL2 2.0.18 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    set(TODO_SDL_TARGET SDL2::SDL2)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(TODO_SYSTEM_SDL QUIET IMPORTED_TARGET GLOBAL "sdl2>=2.0.18")
    endif()
    if(TODO_SYSTEM_SDL_FOUND)
        set(TODO_SDL_TARGET PkgConfig::TODO_SYSTEM_SDL)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
        set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
        set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
        set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
        set(SDL_POWER OFF CACHE BOOL "" FORCE)
        set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            SDL2
            URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.32.10.tar.gz"
            URL_HASH "SHA256=03f9d7c191a837525c9cda6406af2f2e48be02b5e7eb03d949cc9f1e9ca41c8b"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(SDL2)
        set(TODO_SDL_TARGET SDL2::SDL2-static)
    endif()
endif()

# ---- LVGL 9.5.0 (same pin as the SDK simulator) ------------------------------
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE FILEPATH "" FORCE)
set(LV_BUILD_SET_CONFIG_OPTS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_PRIVATE_API ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    lvgl
    URL "https://github.com/lvgl/lvgl/archive/refs/tags/v9.5.0.tar.gz"
    URL_HASH "SHA256=34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lvgl)
target_link_libraries(lvgl PRIVATE ${TODO_SDL_TARGET})
target_compile_definitions(lvgl PRIVATE _POSIX_C_SOURCE=200809L)

add_executable(todo_sim
    src/main.c
    src/png_write.c
    "${TODO_COMPONENT_DIR}/todo_ui.c"
    "${TODO_COMPONENT_DIR}/todo_mascot.c"
    "${SDK_ESP32_DIR}/avatar/muse_pixel.c"
)
target_include_directories(todo_sim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${TODO_COMPONENT_DIR}"
    "${SDK_ESP32_DIR}/components/muse"
)
target_link_libraries(todo_sim PRIVATE todo_model lvgl::lvgl ${TODO_SDL_TARGET} Threads::Threads m)
target_compile_definitions(todo_sim PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(todo_sim PRIVATE -Wall -Wextra)
if(APPLE)
    find_library(TODO_IOKIT IOKit REQUIRED)
    target_link_libraries(todo_sim PRIVATE ${TODO_IOKIT})
endif()

find_package(Python3 3.9 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(
        NAME todo_sim_scenarios
        COMMAND "${Python3_EXECUTABLE}" "${REPO_DIR}/tests/test_sim.py"
                --binary "$<TARGET_FILE:todo_sim>"
                --scenarios "${CMAKE_CURRENT_SOURCE_DIR}/scenarios"
    )
    set_tests_properties(todo_sim_scenarios PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        TIMEOUT 300
    )
endif()
