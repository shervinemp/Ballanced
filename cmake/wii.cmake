# Toolchain file for building Ballanced for the Nintendo Wii.
#
# Wraps devkitPro's official Wii toolchain, which provides the Broadway CPU
# flags, the libogc link line and the ogc_create_dol() helper. Configure from
# a devkitPro environment (the devkitPro MSYS2 shell on Windows):
#
#   cmake -S . -B build/wii -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/wii.cmake

if (NOT DEFINED ENV{DEVKITPRO})
    set(ENV{DEVKITPRO} "/opt/devkitpro")
endif ()

if (NOT EXISTS "$ENV{DEVKITPRO}/cmake/Wii.cmake")
    message(FATAL_ERROR
            "devkitPro's Wii toolchain was not found under $ENV{DEVKITPRO}. "
            "Install the wii-dev package group and configure from a devkitPro shell.")
endif ()

include("$ENV{DEVKITPRO}/cmake/Wii.cmake")

set(WII ON CACHE BOOL "Building for the Nintendo Wii" FORCE)
