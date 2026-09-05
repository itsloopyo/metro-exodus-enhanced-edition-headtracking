cmake_minimum_required(VERSION 3.20)
project(MetroExodusHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "MetroExodusHeadTracking must be built as x64. Configure with -A x64.")
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Pull in the shared CameraUnlock C++ library (no tests).
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# Vendored MinHook (built into the mod, not the core).
set(MINHOOK_SOURCES
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)

# The core's hook manager, compiled straight into the .asi rather than through
# the core's own CAMERAUNLOCK_BUILD_HOOKS option. That option builds its own
# static library against a `minhook` CMake target, and the MinHook above is a
# plain source list rather than a target, so switching it on would mean either
# a second copy of MinHook in the same binary or a target declared here purely
# to satisfy it. One source entry is the smaller thing.
set(CAMERAUNLOCK_HOOK_SOURCES
    cameraunlock-core/cpp/src/hooks/hook_manager.cpp
)

# The render path, listed apart from the rest because it belongs to the .asi
# alone. Two of these four are also compiled directly into the projection test,
# and the other two pull in Direct3D 12, so folding them into the object library
# below would either duplicate a symbol or make every test binary link a
# renderer it has no use for.
set(MOD_RENDER_SOURCES
    src/head_transform.cpp
    src/camera_hook.cpp
    src/aim_projection.cpp
    src/reticle.cpp
)

set(MOD_SOURCES
    src/dllmain.cpp
    src/path_utils.cpp
    src/logging.cpp
    src/config.cpp
    src/build_profile.cpp
    src/tracking_runtime.cpp
    src/hotkeys.cpp
    src/game_state.cpp
    src/fov.cpp
    src/torch.cpp
    src/torch_watch.cpp
    src/ads_state.cpp
    src/steam_offsets.cpp
    src/window_centering.cpp
)

# Everything the mod is made of that does not touch the render path, compiled and
# warning-checked in one place so the same /W4 /WX applies whether it reaches the
# .asi or a test binary.
add_library(metroex_mod_objects OBJECT ${MOD_SOURCES})
target_include_directories(metroex_mod_objects PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
)
target_link_libraries(metroex_mod_objects PRIVATE cameraunlock)
# The version the log banner reports. Taken from the project version so it
# cannot drift: release.ps1 bumps CMakeLists.txt and nothing under src/, and a
# constant kept in a header would go stale the first release nobody remembered
# to edit it.
target_compile_definitions(metroex_mod_objects PRIVATE
    METROEX_VERSION="${PROJECT_VERSION}")
if(MSVC)
    target_compile_options(metroex_mod_objects PRIVATE /W4 /WX /permissive- /wd4324)
endif()

add_library(MetroExodusHeadTracking SHARED
    ${MINHOOK_SOURCES}
    ${CAMERAUNLOCK_HOOK_SOURCES}
    ${MOD_RENDER_SOURCES}
    $<TARGET_OBJECTS:metroex_mod_objects>
)

set_target_properties(MetroExodusHeadTracking PROPERTIES
    OUTPUT_NAME "MetroExodusHeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_link_libraries(MetroExodusHeadTracking PRIVATE cameraunlock)

target_include_directories(MetroExodusHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
)

if(WIN32)
    target_link_libraries(MetroExodusHeadTracking PRIVATE ws2_32 d3d11 d3d12 dxgi dxguid d3dcompiler)
endif()

if(MSVC)
    target_compile_options(MetroExodusHeadTracking PRIVATE /W4 /WX /permissive- /wd4324)
    set_source_files_properties(${MINHOOK_SOURCES} PROPERTIES COMPILE_FLAGS "/W0")
endif()

# Every suite is a plain console executable: the mod links against the game's own
# runtime, so a test framework would be a second one. One declaration keeps every
# suite's settings from drifting apart - the warning level in particular, which is
# what catches a narrowing conversion in a check.
enable_testing()

function(add_metroex_test name)
    set(sources ${ARGN})
    # FATAL_ERROR, not WARNING. A warning does not fail configure, so a stale
    # path here - the shape a rename refactor leaves behind - used to drop the
    # whole suite from ctest while `pixi run test` still reported 100% passed and
    # exited 0. A missing test source is a broken build.
    foreach(source IN LISTS sources)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
            message(FATAL_ERROR "Test ${name} lists ${source}, which does not exist.")
        endif()
    endforeach()

    add_executable(${name} ${sources})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(${name} PRIVATE cameraunlock)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# The INI contract: what the file the player edits means, and what an absent or
# malformed key falls back to.
add_metroex_test(metroex_config_tests tests/config_tests.cpp src/config.cpp)

# The INI that ships in the release ZIP, and the base64 copy of it inside
# launcher-manifest.json, against the one the mod writes for itself. Three copies
# of the same file kept in step by hand, and nothing compared them, which is how
# the shipped [Camera] comment came to omit that a non-zero FieldOfView writes
# six bytes into the running game's code - and how the manifest kept omitting it
# after the shipped copy was fixed.
add_metroex_test(metroex_shipped_ini_tests tests/shipped_ini_tests.cpp src/config.cpp)
target_compile_definitions(metroex_shipped_ini_tests PRIVATE
    METROEX_SHIPPED_INI="${CMAKE_CURRENT_SOURCE_DIR}/MetroExodusHeadTracking.ini"
    METROEX_LAUNCHER_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/launcher-manifest.json")

# The ADS cycle: the pose the camera is fed while the sights are up, and the
# verdict walk that decides whether it reaches the view at all. Both are pure
# functions over the shared module in cameraunlock-core, so neither needs the
# game or a hook.
add_metroex_test(metroex_ads_pose_tests tests/ads_pose_tests.cpp)
add_metroex_test(metroex_ads_gate_tests tests/ads_gate_tests.cpp)

# The projection the reticle is drawn with, over the camera composition the
# camera hook applies.
add_metroex_test(metroex_projection_tests
    tests/projection_tests.cpp
    src/head_transform.cpp
    src/aim_projection.cpp
)

# The camera composition and the boundary conversion are the two halves of what
# reaches the view, and the second is four negations that no build can catch.
# head_transform.cpp carries both, so the suite above covers both.

# The image-bounds check every pinned address goes through, shared by the
# game-state, sights, camera and field-of-view readers, and the profile registry
# itself: the suite reads the newest profile back field by field, which is what
# catches an address landing on the wrong member of a positional initialiser.
add_metroex_test(metroex_build_profile_tests
    tests/build_profile_tests.cpp
    src/steam_offsets.cpp
    src/build_profile.cpp
)

# The console-variable name check, which is what stands between a build profile's
# RVA and a write into the engine's own state. Header-only, and exercised against
# an object laid out by hand rather than against the game.
add_metroex_test(metroex_console_var_tests tests/console_var_tests.cpp)

# The half-field tangents the reticle divides by. Pure arithmetic over the two
# numbers the engine publishes per frame, so it needs neither the game nor a
# build profile.
add_metroex_test(metroex_fov_tests tests/fov_tests.cpp)

# The tracker port being held by a previously-launched game and then freed. Real
# sockets, a real TrackingRuntime and a wall clock: the suite times the recovery
# rather than reading the retry constant back, because the constant proves
# nothing about the loop that consumes it. It takes a few seconds to run for the
# same reason.
add_metroex_test(metroex_udp_recovery_tests
    tests/udp_recovery_tests.cpp
    src/tracking_runtime.cpp
    src/config.cpp
)
