set(TARGET_NAME WiiSoundManager)

add_library(${TARGET_NAME} STATIC
        WiiSoundManager.cpp
        WiiSoundManager.h
)

target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${TARGET_NAME} PRIVATE CK2 VxMath PUBLIC asnd)
