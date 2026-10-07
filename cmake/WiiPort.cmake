# Wii port overlay.
#
# The engine submodules stay untouched. For Wii builds the superproject
#  - swaps desktop platform sources for the libogc ones in Source/WiiPlatform, and
#  - applies the patches in Source/WiiPatches to copies of upstream files in the
#    build tree. Patches must apply exactly, so an upstream change that moves the
#    patched code stops the configure step instead of being silently reverted.

include_guard(GLOBAL)

set(BALLANCE_WII_PLATFORM_DIR "${PROJECT_SOURCE_DIR}/Source/WiiPlatform")
set(BALLANCE_WII_PATCH_DIR "${PROJECT_SOURCE_DIR}/Source/WiiPatches")
set(BALLANCE_WII_PATCHED_DIR "${PROJECT_BINARY_DIR}/WiiPatched")

find_package(Git REQUIRED)

# Static builds expose most components as ALIAS targets of <Name>Static.
function(_ballance_wii_resolve_target out target)
    if (NOT TARGET ${target})
        message(FATAL_ERROR "[Wii] Target '${target}' does not exist")
    endif ()
    get_target_property(_aliased ${target} ALIASED_TARGET)
    if (_aliased)
        set(${out} ${_aliased} PARENT_SCOPE)
    else ()
        set(${out} ${target} PARENT_SCOPE)
    endif ()
endfunction()

# Absolute path of every source of a target.
function(_ballance_wii_absolute_sources out target)
    get_target_property(_sources ${target} SOURCES)
    get_target_property(_source_dir ${target} SOURCE_DIR)
    set(_result "")
    foreach (_source IN LISTS _sources)
        if (_source MATCHES "^\\$<")
            list(APPEND _result "${_source}")
        else ()
            get_filename_component(_absolute "${_source}" ABSOLUTE BASE_DIR "${_source_dir}")
            list(APPEND _result "${_absolute}")
        endif ()
    endforeach ()
    set(${out} "${_result}" PARENT_SCOPE)
endfunction()

# ballance_wii_replace_sources(<target> REMOVE <file>... ADD <file>...)
#
# Replaces upstream sources (absolute paths) with Wii implementations.
function(ballance_wii_replace_sources target)
    cmake_parse_arguments(PARSE_ARGV 1 _arg "" "" "REMOVE;ADD")
    _ballance_wii_resolve_target(_real ${target})
    _ballance_wii_absolute_sources(_sources ${_real})

    foreach (_file IN LISTS _arg_REMOVE)
        get_filename_component(_file "${_file}" ABSOLUTE)
        list(FIND _sources "${_file}" _index)
        if (_index EQUAL -1)
            message(FATAL_ERROR "[Wii] ${_real} does not compile ${_file}; update cmake/WiiPort.cmake")
        endif ()
        list(REMOVE_AT _sources ${_index})
    endforeach ()

    set_target_properties(${_real} PROPERTIES SOURCES "${_sources}")
    if (_arg_ADD)
        target_sources(${_real} PRIVATE ${_arg_ADD})
    endif ()
endfunction()

# ballance_wii_patch(<name>
#     ROOT <upstream directory>
#     PATCH <patch file>
#     FILES <paths relative to ROOT>...
#     TARGETS <targets>...
#     [PUBLIC_HEADERS])
#
# Copies FILES out of ROOT, applies PATCH to the copies and makes TARGETS build
# the patched copies. List a header's same-directory includers in FILES too so
# they pick up the patched header. PUBLIC_HEADERS also gives every consumer of
# TARGETS the patched headers, for headers that change a public type.
function(ballance_wii_patch name)
    cmake_parse_arguments(PARSE_ARGV 1 _arg "PUBLIC_HEADERS" "ROOT;PATCH" "FILES;TARGETS")

    set(_staging "${BALLANCE_WII_PATCHED_DIR}/${name}.staging")
    set(_output "${BALLANCE_WII_PATCHED_DIR}/${name}")
    file(REMOVE_RECURSE "${_staging}")
    file(MAKE_DIRECTORY "${_staging}")

    # Normalize line endings so CRLF checkouts and LF patches agree.
    foreach (_file IN LISTS _arg_FILES)
        file(READ "${_arg_ROOT}/${_file}" _content)
        string(REPLACE "\r\n" "\n" _content "${_content}")
        file(WRITE "${_staging}/${_file}" "${_content}")
    endforeach ()
    file(READ "${_arg_PATCH}" _patch)
    string(REPLACE "\r\n" "\n" _patch "${_patch}")
    file(WRITE "${_staging}.patch" "${_patch}")

    # A private repository keeps git from resolving paths against an enclosing checkout.
    execute_process(
            COMMAND "${GIT_EXECUTABLE}" init -q
            WORKING_DIRECTORY "${_staging}"
            RESULT_VARIABLE _result
    )
    execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${_staging}.patch"
            WORKING_DIRECTORY "${_staging}"
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _out
            ERROR_VARIABLE _out
    )
    if (NOT _result EQUAL 0)
        message(FATAL_ERROR
                "[Wii] ${_arg_PATCH} no longer applies to ${_arg_ROOT}.\n"
                "Re-merge the patch against the current submodule.\n${_out}")
    endif ()

    set(_patched_sources "")
    set(_patched_header_dirs "")
    foreach (_file IN LISTS _arg_FILES)
        get_filename_component(_output_dir "${_output}/${_file}" DIRECTORY)
        file(MAKE_DIRECTORY "${_output_dir}")
        file(COPY_FILE "${_staging}/${_file}" "${_output}/${_file}" ONLY_IF_DIFFERENT)
        get_filename_component(_original "${_arg_ROOT}/${_file}" ABSOLUTE)
        if (_file MATCHES "\\.(c|cc|cpp|cxx)$")
            list(APPEND _patched_sources "${_original}")
        else ()
            get_filename_component(_dir "${_output}/${_file}" DIRECTORY)
            list(APPEND _patched_header_dirs "${_dir}")
        endif ()
    endforeach ()
    list(REMOVE_DUPLICATES _patched_header_dirs)
    file(REMOVE_RECURSE "${_staging}")
    file(REMOVE "${_staging}.patch")

    foreach (_target IN LISTS _arg_TARGETS)
        _ballance_wii_resolve_target(_real ${_target})
        _ballance_wii_absolute_sources(_sources ${_real})
        set(_add "")
        foreach (_original IN LISTS _patched_sources)
            list(FIND _sources "${_original}" _index)
            if (_index EQUAL -1)
                continue()
            endif ()
            list(REMOVE_AT _sources ${_index})
            file(RELATIVE_PATH _relative "${_arg_ROOT}" "${_original}")
            list(APPEND _add "${_output}/${_relative}")
        endforeach ()
        set_target_properties(${_real} PROPERTIES SOURCES "${_sources}")
        if (_add)
            target_sources(${_real} PRIVATE ${_add})
            # Patched copies still include their neighbours from the original tree.
            foreach (_source IN LISTS _add)
                file(RELATIVE_PATH _relative "${_output}" "${_source}")
                get_filename_component(_relative_dir "${_relative}" DIRECTORY)
                target_include_directories(${_real} PRIVATE "${_arg_ROOT}/${_relative_dir}")
            endforeach ()
        endif ()
        if (_patched_header_dirs)
            if (_arg_PUBLIC_HEADERS)
                foreach (_dir IN LISTS _patched_header_dirs)
                    target_include_directories(${_real} BEFORE PUBLIC "$<BUILD_INTERFACE:${_dir}>")
                endforeach ()
            else ()
                target_include_directories(${_real} BEFORE PRIVATE ${_patched_header_dirs})
            endif ()
        endif ()
    endforeach ()

    set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_arg_PATCH}")
    foreach (_file IN LISTS _arg_FILES)
        set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_arg_ROOT}/${_file}")
    endforeach ()
endfunction()

# --- Component hooks, called from Source/CMakeLists.txt after each add_subdirectory ---

# VxMath: the libogc platform layer replaces the SDL3 one.
function(ballance_wii_port_vxmath)
    set(_src "${PROJECT_SOURCE_DIR}/Source/VxMath/src")
    set(_wii "${BALLANCE_WII_PLATFORM_DIR}/VxMath")
    ballance_wii_replace_sources(VxMath
            REMOVE
            "${_src}/VxSharedLibrarySDL3.cpp"
            "${_src}/VxMemoryMappedFileSDL3.cpp"
            "${_src}/VxWindowFunctionsSDL3.cpp"
            "${_src}/VxTimeProfilerSDL3.cpp"
            "${_src}/VxMutexSDL3.cpp"
            "${_src}/VxThreadSDL3.cpp"
            ADD
            "${_wii}/VxSharedLibraryOgc.cpp"
            "${_wii}/VxMemoryMappedFileOgc.cpp"
            "${_wii}/VxWindowFunctionsOgc.cpp"
            "${_wii}/VxTimeProfilerOgc.cpp"
            "${_wii}/VxMutexOgc.cpp"
            "${_wii}/VxThreadOgc.cpp"
    )
    _ballance_wii_resolve_target(_vxmath VxMath)
    target_include_directories(${_vxmath} PUBLIC "$<BUILD_INTERFACE:${BALLANCE_WII_PLATFORM_DIR}/include>")
    # 32-bit pixels are host-order words: kernels that addressed their bytes
    # (24-bit conversions, colour quantizers, bump maps) read and write words.
    ballance_wii_patch(VxMathEndian
            ROOT "${PROJECT_SOURCE_DIR}/Source/VxMath"
            PATCH "${BALLANCE_WII_PATCH_DIR}/VxMath/big-endian.patch"
            FILES src/VxBlitKernels.cpp src/VxBlitEngineClass.cpp src/VxImageKernels.cpp
            TARGETS VxMath
    )
endfunction()

# CK2: big-endian state chunks and files, and Wii storage paths ("sd:/").
# Chunks keep the little-endian file layout in memory (CKChunkWord in
# CKStateChunk.h), so the patched header is public; CKAll.h and CKFile.h include
# it from the same directory and come along unchanged.
function(ballance_wii_port_ck2)
    ballance_wii_patch(CK2
            ROOT "${PROJECT_SOURCE_DIR}/Source/CK2"
            PATCH "${BALLANCE_WII_PATCH_DIR}/CK2/big-endian.patch"
            FILES include/CKStateChunk.h include/CKAll.h include/CKFile.h
                  src/CKStateChunk.cpp src/CKFile.cpp src/CKParameter.cpp src/CKWaveSound.cpp
            TARGETS CK2
            PUBLIC_HEADERS
    )
    ballance_wii_patch(CK2Paths
            ROOT "${PROJECT_SOURCE_DIR}/Source/CK2"
            PATCH "${BALLANCE_WII_PATCH_DIR}/CK2/device-paths.patch"
            FILES src/CKPathManager.cpp
            TARGETS CK2
    )
endfunction()

# RenderEngine: static GX rasterizer registration and Wii display discovery.
function(ballance_wii_port_render_engine)
    set(_root "${PROJECT_SOURCE_DIR}/Source/RenderEngine")
    ballance_wii_patch(RenderEngine
            ROOT "${_root}"
            PATCH "${BALLANCE_WII_PATCH_DIR}/RenderEngine/static-gx-rasterizer.patch"
            FILES src/CK2_3D.cpp
            TARGETS CK2_3D
    )
    # Animation controller keys and bitmap save properties are raw chunk words.
    ballance_wii_patch(RenderEngineEndian
            ROOT "${_root}"
            PATCH "${BALLANCE_WII_PATCH_DIR}/RenderEngine/big-endian.patch"
            FILES src/CKObjectAnimation.cpp src/CKSprite.cpp src/CKTexture.cpp
            TARGETS CK2_3D
    )
    _ballance_wii_resolve_target(_ck2_3d CK2_3D)
    target_compile_definitions(${_ck2_3d} PRIVATE CKRE_STATIC_GX_RASTERIZER)
    target_link_libraries(${_ck2_3d} PUBLIC CKGXRasterizer)

    ballance_wii_replace_sources(CKRasterizerLib
            REMOVE "${_root}/src/CKRasterizer/CKRasterizerLib/CKRasterizerDriverCaps.cpp"
            ADD "${BALLANCE_WII_PLATFORM_DIR}/RenderEngine/CKRasterizerDriverCapsOgc.cpp"
    )

    # POSIX module probing compiles against the "no dynamic loader" dlfcn.h.
    foreach (_target IN ITEMS ${_ck2_3d} CKRasterizerLib)
        target_include_directories(${_target} PRIVATE "${BALLANCE_WII_PLATFORM_DIR}/include")
    endforeach ()
endfunction()

# Plugins: CK2's CKJpegDecoder.cpp already compiles stb_image (JPEG only, no
# stdio); a second copy in AVIReader collides once both are linked statically.
# The readers parse little-endian files and hand out host-order pixels and
# samples. The reader headers include ImageReader.h from their own directory.
function(ballance_wii_port_plugins)
    set(_root "${PROJECT_SOURCE_DIR}/Source/Plugins")
    ballance_wii_replace_sources(AVIReaderStatic
            REMOVE "${_root}/AVIReader/StbImageImpl.cpp"
    )
    ballance_wii_patch(Plugins
            ROOT "${_root}"
            PATCH "${BALLANCE_WII_PATCH_DIR}/Plugins/big-endian.patch"
            FILES ImageReader/ImageReader.h ImageReader/BmpReader.h ImageReader/TgaReader.h
                  ImageReader/PcxReader.h ImageReader/ImageReader.cpp ImageReader/BmpReader.cpp
                  ImageReader/TgaReader.cpp ImageReader/PcxReader.cpp
                  WavReader/WavReader.cpp AVIReader/FrameDecoder.cpp
            TARGETS ImageReaderStatic WavReaderStatic AVIReaderStatic
    )
endfunction()

# BuildingBlocks: big-endian and non-x86 fixes.
function(ballance_wii_port_building_blocks)
    set(_root "${PROJECT_SOURCE_DIR}/Source/BuildingBlocks")
    ballance_wii_patch(BuildingBlocksAddons1
            ROOT "${_root}"
            PATCH "${BALLANCE_WII_PATCH_DIR}/BuildingBlocks/texture-processing-any-arch.patch"
            FILES BuildingBlocksAddons1/TextureProcessing.h BuildingBlocksAddons1/TextureProcessing.cpp
            TARGETS BuildingBlocksAddons1Static
    )
    ballance_wii_patch(Materials
            ROOT "${_root}"
            PATCH "${BALLANCE_WII_PATCH_DIR}/BuildingBlocks/texture-sinus-big-endian.patch"
            FILES Materials-Textures/Behaviors/TextureSinus.cpp
            TARGETS MaterialsStatic
    )
    # The little-endian Database.tdb file, pixel bytes and raw buffers that
    # blocks save in files.
    ballance_wii_patch(BuildingBlocksEndian
            ROOT "${_root}"
            PATCH "${BALLANCE_WII_PATCH_DIR}/BuildingBlocks/big-endian.patch"
            FILES TT_DatabaseManager_RT/DatabaseManager.cpp TT_Toolbox_RT/Behaviors/HighMapMeshform.cpp
                  BuildingBlocksAddons1/Behaviors/CombineTexture.cpp TT_Gravity_RT/Behaviors/TextureSine.cpp
            TARGETS TT_DatabaseManager_RTStatic TT_Toolbox_RTStatic BuildingBlocksAddons1Static TT_Gravity_RTStatic
    )
    # Unlimited Controller keeps -1 in plain chars (unsigned on PowerPC), and
    # its load-time swap must swap a float/int union once.
    ballance_wii_patch(Characters
            ROOT "${_root}"
            PATCH "${BALLANCE_WII_PATCH_DIR}/BuildingBlocks/unlimited-controller.patch"
            FILES Characters/Behaviors/UnlimitedParams.h Characters/Behaviors/UnlimitedController.cpp
            TARGETS CharactersStatic
    )
    # Blocks that read typed text ask for the on-screen keyboard.
    ballance_wii_patch(TextInput
            ROOT "${_root}"
            PATCH "${BALLANCE_WII_PATCH_DIR}/BuildingBlocks/text-input.patch"
            FILES TT_Toolbox_RT/Behaviors/InputString.cpp Controllers/Behaviors/EditString.cpp
            TARGETS TT_Toolbox_RTStatic ControllersStatic
    )
    # Fonts: big-endian font manager chunks and atlas texels; with no system
    # fonts, TrueType fonts come from a Fonts folder next to the game, and font
    # indices from the PC's font list fall back to Arial or Roboto.
    ballance_wii_patch(InterfaceFonts
            ROOT "${_root}"
            PATCH "${BALLANCE_WII_PATCH_DIR}/BuildingBlocks/interface-fonts.patch"
            FILES Interface/CKFontManager.cpp Interface/CKStbFontBackend.cpp
            TARGETS InterfaceStatic
    )
endfunction()

# Code generation settings for every target in the tree:
#  - everything is linked into one executable, so position-independent code
#    only costs registers and GOT loads on Broadway;
#  - devkitPPC's GCC crashes in the LTO link step on the render engine, so the
#    targets that request link-time optimization are compiled normally.
function(ballance_wii_tune_targets directory)
    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach (_target IN LISTS _targets)
        get_target_property(_type ${_target} TYPE)
        if (_type MATCHES "^(STATIC_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            set_target_properties(${_target} PROPERTIES
                    POSITION_INDEPENDENT_CODE OFF
                    INTERPROCEDURAL_OPTIMIZATION OFF
                    INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
                    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF
                    INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL OFF
            )
        endif ()
    endforeach ()
    get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach (_subdirectory IN LISTS _subdirectories)
        ballance_wii_tune_targets("${_subdirectory}")
    endforeach ()
endfunction()
