# Replacements for third-party SDKs that are not included in the source release.
# See Compat/README.md.

# D3DX8 (DirectX 8.1 SDK extension library)
add_library(d3dx8compat STATIC d3dx8compat/d3dx8compat.cpp d3dx8compat/d3dx8tex.cpp)
target_include_directories(d3dx8compat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/d3dx8compat)
target_link_libraries(d3dx8compat PUBLIC zh_options d3d8)

# zlib 1.3.1 (vendored at the location the original project expected it)
set(ZLIB_DIR ${ZH_CODE}/Libraries/Source/Compression/ZLib)
add_library(zh_zlib STATIC
    ${ZLIB_DIR}/adler32.c ${ZLIB_DIR}/compress.c ${ZLIB_DIR}/crc32.c ${ZLIB_DIR}/deflate.c
    ${ZLIB_DIR}/gzclose.c ${ZLIB_DIR}/gzlib.c ${ZLIB_DIR}/gzread.c ${ZLIB_DIR}/gzwrite.c
    ${ZLIB_DIR}/infback.c ${ZLIB_DIR}/inffast.c ${ZLIB_DIR}/inflate.c ${ZLIB_DIR}/inftrees.c
    ${ZLIB_DIR}/trees.c ${ZLIB_DIR}/uncompr.c ${ZLIB_DIR}/zutil.c)
target_compile_definitions(zh_zlib PUBLIC Z_PREFIX)
target_include_directories(zh_zlib PUBLIC ${ZLIB_DIR})
target_link_libraries(zh_zlib PRIVATE zh_options)

# LZH-Light (not in the source release) - decoding stub, see lzhl/CompLibHeader/lzhl.h
add_library(zh_lzhl STATIC lzhl/lzhl_stub.cpp)
target_include_directories(zh_lzhl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lzhl)
target_link_libraries(zh_lzhl PRIVATE zh_options)

# GameSpy SDK (not in the source release; service shut down) - offline stand-in
add_library(zh_gamespy_stub STATIC gamespy/gamespy_stub.cpp)
target_include_directories(zh_gamespy_stub PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/gamespy)
target_link_libraries(zh_gamespy_stub PRIVATE zh_options)

# BYTEmark benchmark (not in the source release)
add_library(zh_benchmark STATIC benchmark/benchmark_stub.cpp)
target_include_directories(zh_benchmark PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/benchmark)
target_link_libraries(zh_benchmark PRIVATE zh_options)

# Miles Sound System API implemented on miniaudio (see MSS/MSS.h)
add_library(zh_mss STATIC MSS/mss_miniaudio.cpp)
target_include_directories(zh_mss PRIVATE ${ZH_CODE}/Libraries/Source/miniaudio)
target_link_libraries(zh_mss PRIVATE zh_options)
if(WIN32)
    target_link_libraries(zh_mss PUBLIC ole32 winmm)
endif()
