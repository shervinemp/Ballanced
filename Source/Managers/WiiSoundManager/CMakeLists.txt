set(TARGET_NAME WiiSoundManager)

set(SRCS
        WiiSoundManager.cpp
)

set(HDRS
        WiiSoundManager.h
)

if (WII)
    add_library(${TARGET_NAME} SHARED ${SRCS} ${HDRS})

    target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${TARGET_NAME} PRIVATE CK2 VxMath asnd)

    set_target_properties(${TARGET_NAME} PROPERTIES
            PREFIX ""
            SUFFIX ".dll"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Managers"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Managers"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Managers"
    )

    install(TARGETS ${TARGET_NAME}
            RUNTIME DESTINATION Managers
            LIBRARY DESTINATION Managers
    )
endif()
