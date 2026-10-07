# Console services shared by the Wii player, rasterizer and managers.
add_library(WiiSystem STATIC
        System/LibcCompat.c
        System/WiiSystem.cpp
        System/WiiSystem.h
)
target_include_directories(WiiSystem PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/System")
# libogc's standard libraries (ogc, wiiuse, bte) come from the toolchain.
target_link_libraries(WiiSystem PUBLIC fat wiikeyboard)
