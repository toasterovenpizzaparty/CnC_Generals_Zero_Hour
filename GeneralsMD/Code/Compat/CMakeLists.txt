# Replacements for third-party SDKs that are not included in the source release.
# See Compat/README.md.

# D3DX8 (DirectX 8.1 SDK extension library)
add_library(d3dx8compat STATIC d3dx8compat/d3dx8compat.cpp d3dx8compat/d3dx8tex.cpp)
target_include_directories(d3dx8compat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/d3dx8compat)
target_link_libraries(d3dx8compat PUBLIC zh_options d3d8)

# Placeholder targets, filled in incrementally.
foreach(t zh_zlib zh_lzhl zh_gamespy_stub zh_platform_stubs zh_miles_stub zh_bink_stub)
    add_library(${t} INTERFACE)
endforeach()
