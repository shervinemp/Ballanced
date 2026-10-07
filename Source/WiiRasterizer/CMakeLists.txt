# Native GX rasterizer, registered statically by CK2_3D on the Wii.
add_library(CKGXRasterizer STATIC
        CKGXRasterizer.cpp
        CKGXRasterizer.h
        CKGXRasterizerContext.cpp
        CKGXState.cpp
        CKGXDraw.cpp
        CKGXTextures.cpp
)
target_compile_features(CKGXRasterizer PRIVATE cxx_std_17)
target_include_directories(CKGXRasterizer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(CKGXRasterizer PUBLIC CKRasterizerLib CK2 VxMath WiiSystem)
