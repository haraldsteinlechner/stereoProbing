cmake_minimum_required(VERSION 3.16)
project(stereo_probe LANGUAGES C)

# Static CRT: the shipped exes run without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(Vulkan REQUIRED)

add_library(stereo_common STATIC verdict.c window.c redblue_args.c)
target_compile_features(stereo_common PUBLIC c_std_11)

add_executable(stereo_probe main.c)
target_link_libraries(stereo_probe PRIVATE stereo_common Vulkan::Vulkan d3d11 dxgi dxguid user32 gdi32 opengl32 advapi32)

add_executable(redblue redblue.c)
target_link_libraries(redblue PRIVATE stereo_common Vulkan::Vulkan d3d11 dxgi dxguid user32 gdi32 opengl32)

add_executable(stereo_verdict_test verdict_test.c)
target_link_libraries(stereo_verdict_test PRIVATE stereo_common)

add_executable(redblue_args_test redblue_args_test.c)
target_link_libraries(redblue_args_test PRIVATE stereo_common)

enable_testing()
add_test(NAME stereo_verdict_test COMMAND stereo_verdict_test)
add_test(NAME redblue_args_test COMMAND redblue_args_test)

if(MSVC)
    foreach(t stereo_common stereo_probe redblue stereo_verdict_test redblue_args_test)
        target_compile_options(${t} PRIVATE /W4 /WX /D_CRT_SECURE_NO_WARNINGS)
    endforeach()
endif()
