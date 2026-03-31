set(CKGX_RASTERIZER_SOURCES
        CKGXRasterizer.cpp
        CKGXRasterizerContext.cpp
        CKGXRasterizerDriver.cpp
)

set(CKGX_RASTERIZER_HEADERS
        CKGXRasterizer.h
)

if (WII)
    add_library(CKGXRasterizer STATIC ${CKGX_RASTERIZER_SOURCES} ${CKGX_RASTERIZER_HEADERS})

    target_include_directories(CKGXRasterizer
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Source/RenderEngine/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(CKGXRasterizer
            PUBLIC
            CKRasterizerLib
            PRIVATE
            CK2
            VxMath
            wiiuse
            ogc
    )

    install(TARGETS CKGXRasterizer
            EXPORT CKRenderEngineTargets
            ARCHIVE DESTINATION lib
    )
endif()
