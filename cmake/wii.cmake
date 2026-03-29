set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)
set(WII ON CACHE BOOL "Building for Nintendo Wii" FORCE)

# Setup devkitPro paths
if(NOT DEFINED ENV{DEVKITPRO})
    set(ENV{DEVKITPRO} "/opt/devkitpro")
endif()
if(NOT DEFINED ENV{DEVKITPPC})
    set(ENV{DEVKITPPC} "/opt/devkitpro/devkitPPC")
endif()

set(DEVKITPRO $ENV{DEVKITPRO})
set(DEVKITPPC $ENV{DEVKITPPC})

if (EXISTS "${DEVKITPPC}/bin/powerpc-eabi-gcc")
    set(CMAKE_C_COMPILER "${DEVKITPPC}/bin/powerpc-eabi-gcc")
    set(CMAKE_CXX_COMPILER "${DEVKITPPC}/bin/powerpc-eabi-g++")
    set(CMAKE_ASM_COMPILER "${DEVKITPPC}/bin/powerpc-eabi-gcc")

    set(MACH_DEP "-mrvl -mcpu=750 -meabi -mhard-float")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MACH_DEP} -DGEKKO -DHW_RVL -DWII=1" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${MACH_DEP} -DGEKKO -DHW_RVL -DWII=1" CACHE STRING "" FORCE)
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${MACH_DEP} -DGEKKO -DHW_RVL -DWII=1" CACHE STRING "" FORCE)
endif()

# LibOGC
set(LIBOGC_INC "${DEVKITPRO}/libogc/include")
set(LIBOGC_LIB "${DEVKITPRO}/libogc/lib/wii")

include_directories(${LIBOGC_INC})
link_directories(${LIBOGC_LIB})
