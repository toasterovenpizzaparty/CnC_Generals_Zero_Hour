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

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT MSVC)
    # The original shipped with MSVC /O2.  GCC's default -O3 is avoided on this old code base.
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(ZH_WARNINGS "Show compiler warnings (very noisy on this code base)" OFF)
# Proprietary SDKs that are not part of the source release.  Turn on only if you have them.
option(ZH_USE_MILES "Link the real Miles Sound System 6 SDK instead of the miniaudio implementation of its API" OFF)
option(ZH_USE_BINK "Build the Bink video player (needs the Bink SDK)" OFF)

set(ZH_CODE ${CMAKE_CURRENT_SOURCE_DIR})
set(ZH_COMPAT ${ZH_CODE}/Compat)	# replacements for unavailable SDKs and pre-standard headers

foreach(list wwdebug wwlib wwmath wwsaveload wwutil ww3d2 compression gameengine gameenginedevice rts)
    include(cmake/sources/${list}.cmake)
endforeach()

# ------------------------------------------------------------------------------------------------
# Compiler settings shared by every target
# ------------------------------------------------------------------------------------------------
add_library(zh_options INTERFACE)
target_compile_definitions(zh_options INTERFACE
    WIN32 _WINDOWS NDEBUG _RELEASE
    WINVER=0x0400 _WIN32_WINNT=0x0400 NOMINMAX	# as in the original .dsp files
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE
    ZH_USE_MILES=$<BOOL:${ZH_USE_MILES}> ZH_USE_BINK=$<BOOL:${ZH_USE_BINK}>)
target_include_directories(zh_options INTERFACE ${ZH_COMPAT})
if(WIN32)
    target_link_libraries(zh_options INTERFACE
        d3d8 dinput8 dxguid dsound winmm vfw32 wsock32 ws2_32 imm32 wininet
        ole32 oleaut32 uuid comdlg32 shell32 advapi32 gdi32 user32 imagehlp)
endif()
if(MSVC)
    target_compile_options(zh_options INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/FI${ZH_COMPAT}/zh_compat_prefix.h>)
else()
    target_compile_options(zh_options INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-include ${ZH_COMPAT}/zh_compat_prefix.h>)
endif()

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
        # sources are Windows-1252 encoded (MSVC reads them in the system code page)
        -finput-charset=CP1252 -fexec-charset=CP1252
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
    # The libraries call into each other in cycles (e.g. GameEngine <-> GameEngineDevice), which
    # MSVC resolves on its own; GNU ld needs each cycle repeated on the link line.
    set_target_properties(${name} PROPERTIES LINK_INTERFACE_MULTIPLICITY 3)
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
target_link_libraries(wwdebug INTERFACE "$<LINK_ONLY:wwlib>")	# wwmemlog uses the WWLib allocator
zh_library(wwmath SOURCES ${WWMATH_SOURCES} INCLUDES ${WWV}/WWMath ${WWV}/WWSaveLoad
    DEFINES ${WW_DEFINES} LINK wwlib d3dx8compat)
zh_library(wwsaveload SOURCES ${WWSAVELOAD_SOURCES} INCLUDES ${WWV}/WWSaveLoad
    DEFINES ${WW_DEFINES} LINK wwmath)
zh_library(wwutil SOURCES ${WWUTIL_SOURCES} INCLUDES ${WWV}/Wwutil DEFINES ${WW_DEFINES} LINK wwmath)
zh_library(ww3d2 SOURCES ${WW3D2_SOURCES} INCLUDES ${WWV}/WW3D2 ${WWV}
    DEFINES ${WW_DEFINES} LINK wwsaveload wwmath wwlib d3dx8compat)
zh_library(wwdownload SOURCES
        ${WWV}/WWDownload/Download.cpp ${WWV}/WWDownload/Ftp.cpp
        ${WWV}/WWDownload/registry.cpp ${WWV}/WWDownload/urlBuilder.cpp
    INCLUDES ${WWV}/WWDownload ${WWV} DEFINES ${WW_DEFINES})
# wwshade is only used when USE_WWSHADE is defined, which the game never does (and its vertex
# shaders need the proprietary NVASM assembler), so it is not built.

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
# LAN network: ENet and the lockstep session (engine-independent, see GameNetwork/Lockstep)
# ------------------------------------------------------------------------------------------------
file(GLOB ENET_SOURCES ${ZH_CODE}/Libraries/Source/ENet/*.c)
add_library(zh_enet STATIC ${ENET_SOURCES})
target_include_directories(zh_enet PUBLIC ${ZH_CODE}/Libraries/Source/ENet/include)
target_link_libraries(zh_enet PRIVATE zh_options)
if(WIN32)
    target_link_libraries(zh_enet PUBLIC ws2_32 winmm)
endif()

add_library(zh_lockstep STATIC
    ${ZH_CODE}/GameEngine/Source/GameNetwork/Lockstep/Session.cpp
    ${ZH_CODE}/GameEngine/Source/GameNetwork/Lockstep/EnetTransport.cpp)
target_include_directories(zh_lockstep PUBLIC ${ZH_CODE}/GameEngine/Include)
target_link_libraries(zh_lockstep PUBLIC zh_enet PRIVATE zh_options)

# ------------------------------------------------------------------------------------------------
# Game engine
# ------------------------------------------------------------------------------------------------
# The original network implementation (Network.cpp: frame-info protocol, ConnectionManager) is
# replaced by LockstepNetwork.cpp on the lockstep session (GameNetwork/Lockstep).
list(REMOVE_ITEM GAMEENGINE_SOURCES "GameEngine/Source/GameNetwork/Network.cpp")
list(APPEND GAMEENGINE_SOURCES "GameEngine/Source/GameNetwork/LockstepNetwork.cpp")
zh_library(gameengine SOURCES ${GAMEENGINE_SOURCES}
    INCLUDES ${ZH_CODE}/GameEngine/Include ${ZH_CODE}/GameEngine/Include/Precompiled
             ${ZH_CODE}/Libraries/Include ${WWV} ${ZH_CODE}/Libraries/Source/Compression
    DEFINES Z_PREFIX IG_DEBUG_STACKTRACE
    LINK compression wwlib wwmath wwdownload zh_gamespy_stub zh_lockstep)
# The original project used PreRTS.h as precompiled header (/Yu"PreRTS.h"); every .cpp includes it first.
target_precompile_headers(gameengine PRIVATE ${ZH_CODE}/GameEngine/Include/Precompiled/PreRTS.h)

if(NOT ZH_USE_BINK)
    list(FILTER GAMEENGINEDEVICE_SOURCES EXCLUDE REGEX "VideoDevice/Bink/")
endif()
zh_library(gameenginedevice SOURCES ${GAMEENGINEDEVICE_SOURCES}
    INCLUDES ${ZH_CODE}/GameEngineDevice/Include ${ZH_CODE}/Main
    DEFINES IG_DEBUG_STACKTRACE
    LINK gameengine ww3d2 wwsaveload wwutil zh_benchmark $<IF:$<BOOL:${ZH_USE_MILES}>,mss32,zh_mss>)
# GameEngine calls the device layer through factory functions (CreateCDManager, MOTDSystem, ...):
# a link-only dependency that closes the cycle, so the linker repeats both libraries.
target_link_libraries(gameengine INTERFACE "$<LINK_ONLY:gameenginedevice>")

# Version headers (generated by Tools/versionUpdate and Tools/buildVersionUpdate in the original
# build).  The version takes part in the exe CRC that LAN games compare, so every player must
# use the same values.
set(ZH_VERSION_MAJOR 1 CACHE STRING "Game version, major")
set(ZH_VERSION_MINOR 4 CACHE STRING "Game version, minor (Zero Hour retail patch 1.04)")
set(ZH_VERSION_BUILDNUM 0 CACHE STRING "Build number")
set(ZH_VERSION_LOCALBUILDNUM 0 CACHE STRING "Local build number")
set(ZH_VERSION_BUILDUSER "cmake" CACHE STRING "Build user name")
set(ZH_VERSION_BUILDLOC "modern-build" CACHE STRING "Build location")
configure_file(cmake/templates/BuildVersion.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/BuildVersion.h)
configure_file(cmake/templates/GeneratedVersion.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/GeneratedVersion.h)

add_executable(RTS WIN32 ${RTS_SOURCES} ${ZH_CODE}/Main/RTS.RC)
target_include_directories(RTS PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated ${ZH_CODE}/Main)
target_link_libraries(RTS PRIVATE gameenginedevice gameengine)
set_target_properties(RTS PROPERTIES OUTPUT_NAME "generalszh")
