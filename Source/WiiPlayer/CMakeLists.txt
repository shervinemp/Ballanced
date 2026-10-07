# Wii player: boot.dol for the Homebrew Channel, with every Virtools module linked in.

set(_player_src "${CMAKE_SOURCE_DIR}/Source/Player/src")
set(_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Values consumed by Player's config.h.in (normally set by Player's own CMakeLists).
set(_player_default_rasterizer "gx")
set(PLAYER_SCREEN_WIDTH 640)
set(PLAYER_SCREEN_HEIGHT 480)
set(PLAYER_SCREEN_BPP 32)
configure_file("${_player_src}/config.h.in" "${_generated}/config.h" @ONLY)

# The desktop player's module list, with the Wii managers standing in for the
# SDL3 ones under the same plugin entry points.
include("${CMAKE_SOURCE_DIR}/Source/Player/cmake/PlayerStaticModules.cmake")
set(PLAYER_STATIC_MODULE_SdlInputManager_STATIC_TARGET WiiInputManager)
set(PLAYER_STATIC_MODULE_SdlInputManager_DISPLAY_NAME WiiInputManager)
set(PLAYER_STATIC_MODULE_SdlSoundManager_STATIC_TARGET WiiSoundManager)
set(PLAYER_STATIC_MODULE_SdlSoundManager_DISPLAY_NAME WiiSoundManager)

set(PLAYER_STATIC_PLUGIN_DECLARATIONS "")
set(PLAYER_STATIC_PLUGIN_ENTRIES "")
set(_static_targets "")
set(_static_definitions "")
foreach (_module IN LISTS PLAYER_STATIC_MODULES)
    set(_prefix "PLAYER_STATIC_MODULE_${_module}")
    set(_target "${${_prefix}_STATIC_TARGET}")
    set(_macro "${${_prefix}_COMPILE_DEFINITION}")
    if (NOT TARGET ${_target})
        if (${_prefix}_REQUIRED)
            message(FATAL_ERROR "The Wii player requires target ${_target}")
        endif ()
        continue()
    endif ()
    list(APPEND _static_targets ${_target})
    list(APPEND _static_definitions "${_macro}=1")
    if (${_prefix}_LINK_ONLY)
        continue()
    endif ()

    foreach (_field IN ITEMS GET_INFO_COUNT GET_INFO GET_READER REGISTER_DECLARATIONS)
        set(_${_field} "${${_prefix}_${_field}}")
        if (_${_field})
            set(_${_field}_value "${_${_field}}")
        else ()
            set(_${_field}_value NULL)
        endif ()
    endforeach ()

    string(APPEND PLAYER_STATIC_PLUGIN_DECLARATIONS "#if defined(${_macro})\n")
    if (_GET_INFO_COUNT)
        string(APPEND PLAYER_STATIC_PLUGIN_DECLARATIONS "extern int ${_GET_INFO_COUNT}();\n")
    endif ()
    string(APPEND PLAYER_STATIC_PLUGIN_DECLARATIONS "extern CKPluginInfo *${_GET_INFO}(int);\n")
    if (_GET_READER)
        string(APPEND PLAYER_STATIC_PLUGIN_DECLARATIONS "extern CKDataReader *${_GET_READER}(int);\n")
    endif ()
    if (_REGISTER_DECLARATIONS)
        string(APPEND PLAYER_STATIC_PLUGIN_DECLARATIONS "extern void ${_REGISTER_DECLARATIONS}(XObjectDeclarationArray *);\n")
    endif ()
    string(APPEND PLAYER_STATIC_PLUGIN_DECLARATIONS "#endif\n\n")
    string(APPEND PLAYER_STATIC_PLUGIN_ENTRIES
            "#if defined(${_macro})\n"
            "    {\"${${_prefix}_DISPLAY_NAME}\", ${_GET_INFO_COUNT_value}, ${_GET_INFO_value}, ${_GET_READER_value}, ${_REGISTER_DECLARATIONS_value}},\n"
            "#endif\n")
endforeach ()
configure_file("${_player_src}/StaticPluginRegistry.generated.h.in"
        "${_generated}/StaticPluginRegistry.generated.h" @ONLY)

add_executable(WiiPlayer
        main.cpp
        WiiGamePlayer.cpp
        WiiGamePlayer.h
        "${_player_src}/GameConfig.cpp"
        "${_player_src}/Hotfix.cpp"
        "${_player_src}/IniFile.cpp"
        "${_player_src}/Logger.cpp"
        "${_player_src}/StaticPlugins.cpp"
        "${_player_src}/Utils.cpp"
)
target_compile_features(WiiPlayer PRIVATE cxx_std_17)
target_compile_definitions(WiiPlayer PRIVATE BALLANCE_STATIC_MODULES ${_static_definitions})
target_include_directories(WiiPlayer PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${_player_src}"
        "${_generated}"
)
target_link_libraries(WiiPlayer PRIVATE ${_static_targets} CK2 VxMath WiiSystem)
set_target_properties(WiiPlayer PROPERTIES OUTPUT_NAME boot SUFFIX ".elf")

# Wii storage paths ("sd:/") in the shared player helpers.
ballance_wii_patch(Player
        ROOT "${CMAKE_SOURCE_DIR}/Source/Player"
        PATCH "${BALLANCE_WII_PATCH_DIR}/Player/wii-paths.patch"
        FILES src/Utils.cpp
        TARGETS WiiPlayer
)

ogc_create_dol(WiiPlayer)

# elf2dol leaves the last section's size unaligned at the end of the file;
# loaders read sections in 32-byte units, and Dolphin rejects a DOL whose
# rounded-up last section runs past the end of the file.
find_program(PADBIN_EXE NAMES padbin HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
add_custom_command(TARGET WiiPlayer POST_BUILD
        COMMAND "${PADBIN_EXE}" 32 "$<TARGET_FILE_DIR:WiiPlayer>/boot.dol"
        COMMENT "Padding boot.dol to 32 bytes"
        VERBATIM
)

# On-console tests (tests.dol): state chunk and file byte layouts, media
# readers and GX rasterizer pixels. Run from the SD card or in Dolphin.
add_executable(WiiPlayerTests
        tests/main.cpp
        tests/TestFramework.cpp
        tests/TestFramework.h
        tests/ChunkTests.cpp
        tests/FileTests.cpp
        tests/ReaderTests.cpp
        tests/RasterizerTests.cpp
        tests/SceneTests.cpp
        tests/SoundTests.cpp
        tests/DemoComposition.cpp
        tests/PhysicsTests.cpp
        "${_player_src}/StaticPlugins.cpp"
)
target_compile_features(WiiPlayerTests PRIVATE cxx_std_17)
target_compile_definitions(WiiPlayerTests PRIVATE BALLANCE_STATIC_MODULES ${_static_definitions})
# The physics suite drives physics_RT's manager directly, like its own tests.
target_include_directories(WiiPlayerTests PRIVATE "${_player_src}" "${_generated}"
        "${CMAKE_SOURCE_DIR}/Source/BuildingBlocks/physics_RT")
target_link_libraries(WiiPlayerTests PRIVATE ${_static_targets} CKGXRasterizer CK2 VxMath WiiSystem ivp_controller)
set_target_properties(WiiPlayerTests PROPERTIES OUTPUT_NAME tests SUFFIX ".elf")
ogc_create_dol(WiiPlayerTests)
add_custom_command(TARGET WiiPlayerTests POST_BUILD
        COMMAND "${PADBIN_EXE}" 32 "$<TARGET_FILE_DIR:WiiPlayerTests>/tests.dol"
        VERBATIM
)

# Homebrew Channel layout: apps/ballance/{boot.dol,meta.xml,icon.png}; the
# game files are copied next to them.
install(FILES
        "$<TARGET_FILE_DIR:WiiPlayer>/boot.dol"
        "${CMAKE_CURRENT_SOURCE_DIR}/hbc/meta.xml"
        "${CMAKE_CURRENT_SOURCE_DIR}/hbc/icon.png"
        DESTINATION apps/ballance
        COMPONENT Runtime
)
