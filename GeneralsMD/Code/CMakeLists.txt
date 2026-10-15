# Modern build for Command & Conquer Generals Zero Hour (modified version).
#
# Replaces the Visual C++ 6 workspace (RTS.dsw) for building the game executable.  Source lists
# are generated from the original .dsp files by cmake/dsp_to_cmake.py.
#
#   Windows / MSVC:   cmake -S . -B build -A Win32        && cmake --build build --config Release
#   Linux / MinGW:    cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake
#                     cmake --build build
cmake_minimum_required(VERSION 3.20)
project(GeneralsZH LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(ZH_WARNINGS "Show compiler warnings (very noisy on this code base)" OFF)

set(ZH_CODE ${CMAKE_CURRENT_SOURCE_DIR})
set(ZH_COMPAT ${ZH_CODE}/Compat)	# replacements for unavailable SDKs and pre-standard headers

foreach(list wwdebug wwlib wwmath wwsaveload wwutil ww3d2 wwshade compression gameengine gameenginedevice rts)
    include(cmake/sources/${list}.cmake)
endforeach()

# ------------------------------------------------------------------------------------------------
# Compiler settings shared by every target
# ------------------------------------------------------------------------------------------------
add_library(zh_options INTERFACE)
target_compile_definitions(zh_options INTERFACE
    WIN32 _WINDOWS NDEBUG _RELEASE
    WINVER=0x0601 _WIN32_WINNT=0x0601
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
target_include_directories(zh_options INTERFACE ${ZH_COMPAT})

if(MSVC)
    # /fp:precise is required for deterministic multiplayer - never use /fp:fast.
    target_compile_options(zh_options INTERFACE /fp:precise /MP /permissive)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_compile_options(zh_options INTERFACE /arch:SSE2)
    endif()
    if(NOT ZH_WARNINGS)
        target_compile_options(zh_options INTERFACE /W0)
    endif()
else()
    # Deterministic floating point: SSE2 everywhere (no x87 excess precision), no FMA contraction.
    target_compile_options(zh_options INTERFACE
        -fno-strict-aliasing -fwrapv -ffp-contract=off -fms-extensions
        $<$<COMPILE_LANGUAGE:CXX>:-fno-operator-names>)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_compile_options(zh_options INTERFACE -msse2 -mfpmath=sse)
    endif()
    if(NOT ZH_WARNINGS)
        target_compile_options(zh_options INTERFACE -w)
    endif()
    target_link_options(zh_options INTERFACE -static -static-libgcc -static-libstdc++)
endif()

set(WWV ${ZH_CODE}/Libraries/Source/WWVegas)

function(zh_library name)
    cmake_parse_arguments(A "" "" "SOURCES;INCLUDES;DEFINES;LINK" ${ARGN})
    add_library(${name} STATIC ${A_SOURCES})
    target_link_libraries(${name} PUBLIC zh_options ${A_LINK})
    target_include_directories(${name} PUBLIC ${A_INCLUDES})
    target_compile_definitions(${name} PRIVATE ${A_DEFINES})
endfunction()

# ------------------------------------------------------------------------------------------------
# Westwood libraries
# ------------------------------------------------------------------------------------------------
set(WW_DEFINES WIN32_LEAN_AND_MEAN IG_DEBUG_STACKTRACE)
zh_library(wwdebug SOURCES ${WWDEBUG_SOURCES} INCLUDES ${WWV}/WWDebug ${WWV}/WWLib DEFINES ${WW_DEFINES})
zh_library(wwlib SOURCES ${WWLIB_SOURCES} INCLUDES ${WWV}/WWLib ${WWV}/WWDebug
    DEFINES ${WW_DEFINES} REGEX_MALLOC STDC_HEADERS LINK wwdebug)
zh_library(wwmath SOURCES ${WWMATH_SOURCES} INCLUDES ${WWV}/WWMath ${WWV}/WWSaveLoad
    DEFINES ${WW_DEFINES} LINK wwlib d3dx8compat)
zh_library(wwsaveload SOURCES ${WWSAVELOAD_SOURCES} INCLUDES ${WWV}/WWSaveLoad
    DEFINES ${WW_DEFINES} LINK wwmath)
zh_library(wwutil SOURCES ${WWUTIL_SOURCES} INCLUDES ${WWV}/Wwutil DEFINES ${WW_DEFINES} LINK wwmath)
zh_library(ww3d2 SOURCES ${WW3D2_SOURCES} INCLUDES ${WWV}/WW3D2 ${WWV}
    DEFINES ${WW_DEFINES} LINK wwsaveload wwmath wwlib d3dx8compat)
zh_library(wwshade SOURCES ${WWSHADE_SOURCES} INCLUDES ${WWV}/wwshade DEFINES ${WW_DEFINES} LINK ww3d2)

# ------------------------------------------------------------------------------------------------
# Replacements for SDKs that are not part of the source release (see Compat/README.md)
# ------------------------------------------------------------------------------------------------
add_subdirectory(Compat)

# ------------------------------------------------------------------------------------------------
# Compression (EA RefPack/Huffman/BTree, LZH, zlib)
# ------------------------------------------------------------------------------------------------
zh_library(compression SOURCES ${COMPRESSION_SOURCES}
    INCLUDES ${ZH_CODE}/Libraries/Source/Compression ${ZH_CODE}/Libraries/Include
    DEFINES Z_PREFIX LINK zh_zlib zh_lzhl)

# ------------------------------------------------------------------------------------------------
# Game engine
# ------------------------------------------------------------------------------------------------
zh_library(gameengine SOURCES ${GAMEENGINE_SOURCES}
    INCLUDES ${ZH_CODE}/GameEngine/Include ${ZH_CODE}/GameEngine/Include/Precompiled
             ${ZH_CODE}/Libraries/Include ${WWV} ${ZH_CODE}/Libraries/Source/Compression
    DEFINES Z_PREFIX IG_DEBUG_STACKTRACE
    LINK compression wwlib wwmath zh_gamespy_stub zh_platform_stubs)

zh_library(gameenginedevice SOURCES ${GAMEENGINEDEVICE_SOURCES}
    INCLUDES ${ZH_CODE}/GameEngineDevice/Include ${ZH_CODE}/Main
    DEFINES IG_DEBUG_STACKTRACE
    LINK gameengine ww3d2 wwshade wwsaveload wwutil zh_miles_stub zh_bink_stub)

add_executable(RTS WIN32 ${RTS_SOURCES})
target_link_libraries(RTS PRIVATE gameenginedevice gameengine
    d3d8 dinput8 dxguid dsound winmm vfw32 wsock32 ws2_32 imm32 wininet
    ole32 oleaut32 uuid comdlg32 shell32 advapi32 gdi32 user32 imagehlp)
set_target_properties(RTS PROPERTIES OUTPUT_NAME "generalszh")
