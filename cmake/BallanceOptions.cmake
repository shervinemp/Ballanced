# Root-only options and child-project defaults for the superproject.

ballance_set_cache_default(BALLANCE_AUTO_DETECT_ASSETS OFF BOOL
        "Use the repository-local assets directory for staging when it exists")
ballance_set_cache_default(BALLANCE_ASSETS_ROOT "" PATH
        "Path to Ballance game root for asset staging (base.cmo, Textures/, etc.)")

set(_ballance_local_assets_root "${PROJECT_SOURCE_DIR}/assets")
set(BALLANCE_EFFECTIVE_ASSETS_ROOT "${BALLANCE_ASSETS_ROOT}")
if (BALLANCE_AUTO_DETECT_ASSETS AND NOT BALLANCE_ASSETS_ROOT AND EXISTS "${_ballance_local_assets_root}")
    set(BALLANCE_EFFECTIVE_ASSETS_ROOT "${_ballance_local_assets_root}")
endif ()
if (BALLANCE_EFFECTIVE_ASSETS_ROOT)
    file(TO_CMAKE_PATH "${BALLANCE_EFFECTIVE_ASSETS_ROOT}"
            BALLANCE_EFFECTIVE_ASSETS_ROOT)
endif ()

ballance_set_cache_default(BALLANCE_DIR "" PATH
        "Optional: Ballance game directory for manual deployment")

ballance_set_cache_default(BALLANCE_TEST_CONFIG "Release" STRING
        "CTest configuration for multi-config generators")

if (WII)
    # The Wii has no dynamic loader, so every Virtools module is linked into the executable.
    set(BALLANCE_BUILD_STATIC ON CACHE BOOL
            "Build Player with Virtools modules linked statically" FORCE)
    # Only boot.dol and the Homebrew Channel files are installed.
    foreach (_component IN ITEMS VXMATH CK2 CKRE CKBB CKPLUGINS SDLINPUT SDLSOUND CKPARAMOP)
        ballance_set_cache_default(${_component}_INSTALL OFF BOOL "")
    endforeach ()
endif ()
ballance_set_cache_default(BALLANCE_BUILD_STATIC OFF BOOL
        "Build Player with Virtools modules linked statically")

ballance_set_cache_default(BALLANCE_TARGET_ARCH "" STRING
        "Optional target architecture label used by platform presets")

if (WII)
    # libogc replaces SDL3 on the Wii. Components that link SDL3::SDL3 for their
    # desktop platform layer get an empty target; Source/WiiPlatform supplies the
    # native implementations (see cmake/WiiPort.cmake).
    add_library(SDL3::SDL3 INTERFACE IMPORTED GLOBAL)
    # No dynamic loader either: the dlfcn.h stubs in Source/WiiPlatform/include
    # stand in for libdl, so components must not link it.
    set(CMAKE_DL_LIBS "")
    # devkitPPC compiles thread_local to r2-relative TLS, but libogc sets no
    # thread pointer and its linker script has no TLS segment, so such variables
    # read garbage. The engine only uses them on the game thread (blit caches,
    # animation merging, stb flags): make them ordinary variables.
    add_compile_definitions("$<$<COMPILE_LANGUAGE:CXX>:thread_local=>")
else ()
    find_package(SDL3 3.4.8 CONFIG REQUIRED)
endif ()

foreach (_component IN ITEMS VXMATH CK2 CKRE)
    if (BALLANCE_BUILD_STATIC)
        ballance_set_cache_default(${_component}_BUILD_SHARED OFF BOOL "")
        ballance_set_cache_default(${_component}_BUILD_STATIC ON BOOL "")
    else ()
        ballance_set_cache_default(${_component}_BUILD_SHARED ON BOOL "")
        ballance_set_cache_default(${_component}_BUILD_STATIC OFF BOOL "")
    endif ()
    ballance_set_cache_default(${_component}_INSTALL ON BOOL "")
    ballance_set_cache_default(${_component}_BUILD_TESTS OFF BOOL "")
endforeach ()

if (BALLANCE_BUILD_STATIC)
    ballance_set_cache_default(CKBB_BUILD_SHARED OFF BOOL "")
    ballance_set_cache_default(CKBB_BUILD_STATIC ON BOOL "")
    ballance_set_cache_default(CKPLUGINS_BUILD_SHARED OFF BOOL "")
    ballance_set_cache_default(CKPLUGINS_BUILD_STATIC ON BOOL "")
    foreach (_mgr IN ITEMS SDLINPUT SDLSOUND CKPARAMOP)
        ballance_set_cache_default(${_mgr}_BUILD_SHARED OFF BOOL "")
        ballance_set_cache_default(${_mgr}_BUILD_STATIC ON BOOL "")
    endforeach ()
else ()
    ballance_set_cache_default(CKBB_BUILD_SHARED ON BOOL "")
    ballance_set_cache_default(CKBB_BUILD_STATIC OFF BOOL "")
    ballance_set_cache_default(CKPLUGINS_BUILD_SHARED ON BOOL "")
    foreach (_mgr IN ITEMS SDLINPUT SDLSOUND CKPARAMOP)
        ballance_set_cache_default(${_mgr}_BUILD_SHARED ON BOOL "")
    endforeach ()
endif ()
ballance_set_cache_default(CKBB_INSTALL ON BOOL "")
ballance_set_cache_default(CKBB_BUILD_TESTS OFF BOOL "")

ballance_set_cache_default(CKPLUGINS_INSTALL ON BOOL "")
ballance_set_cache_default(CKPLUGINS_BUILD_TESTS OFF BOOL "")

foreach (_mgr IN ITEMS SDLINPUT SDLSOUND CKPARAMOP)
    ballance_set_cache_default(${_mgr}_INSTALL ON BOOL "")
endforeach ()

if (NOT WIN32)
    ballance_set_cache_default(CKBB_BUILD_MidiManager OFF BOOL "")
endif ()

if (WII)
    # SDL GPU has no Wii backend; the GX rasterizer is built from Source/WiiRasterizer.
    ballance_set_cache_default(CKRE_BUILD_SDL_GPU_RASTERIZER OFF BOOL "")
    # Broadway has no SSE; SIMDe would only emulate it in scalar code.
    ballance_set_cache_default(VXMATH_ENABLE_SIMD OFF BOOL "")
endif ()
