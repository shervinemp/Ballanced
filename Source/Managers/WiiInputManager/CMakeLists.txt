set(TARGET_NAME WiiInputManager)

add_library(${TARGET_NAME} STATIC
        WiiInputManager.cpp
        WiiInputManager.h
        # Parameter types and operations are shared with the desktop manager.
        ${CMAKE_CURRENT_SOURCE_DIR}/../SdlInputManager/Parameters.cpp
)

target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${TARGET_NAME} PRIVATE CK2 VxMath WiiSystem)
