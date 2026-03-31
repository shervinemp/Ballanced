if (WII)
    set(_player_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    configure_file(
            "${CMAKE_SOURCE_DIR}/Source/Player/src/config.h.in"
            "${_player_generated_dir}/config.h"
            @ONLY
    )
    configure_file(
            "${CMAKE_SOURCE_DIR}/Source/Player/src/Player.rc.in"
            "${_player_generated_dir}/Player.rc"
            @ONLY
    )

    add_executable(WiiPlayer
        main.cpp
        ${CMAKE_SOURCE_DIR}/Source/Player/src/GamePlayer.cpp
        ${CMAKE_SOURCE_DIR}/Source/Player/src/GameConfig.cpp
        ${CMAKE_SOURCE_DIR}/Source/Player/src/Hotfix.cpp
        ${CMAKE_SOURCE_DIR}/Source/Player/src/Splash.cpp
        ${CMAKE_SOURCE_DIR}/Source/Player/src/CmdlineParser.cpp
        ${CMAKE_SOURCE_DIR}/Source/Player/src/Logger.cpp
        ${CMAKE_SOURCE_DIR}/Source/Player/src/Utils.cpp
        WiiPlatform.cpp
    )

    target_include_directories(WiiPlayer PRIVATE
            ${CMAKE_SOURCE_DIR}/Source/Player/include
            ${CMAKE_SOURCE_DIR}/Source/Player/src
            ${_player_generated_dir}
    )

    target_link_libraries(WiiPlayer PRIVATE CK2 VxMath CKGXRasterizer asnd wiiuse fat)

    # Convert the .elf to a .dol file automatically after linking
    add_custom_command(TARGET WiiPlayer POST_BUILD
        COMMAND elf2dol $<TARGET_FILE:WiiPlayer> $<TARGET_FILE_DIR:WiiPlayer>/boot.dol
        COMMENT "Generating boot.dol for Homebrew Channel..."
    )

    install(FILES $<TARGET_FILE_DIR:WiiPlayer>/boot.dol DESTINATION .)
endif()
