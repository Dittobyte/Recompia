# astc2bc7: ASTC -> BC7 transcoder library + CLI. Windows x64 (clang-cl + Ninja) or Linux x86-64 (clang or gcc + Ninja).
#   cmake -S tools/textures -B tools/textures/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
# (run from a vcvars64 environment; scripts/build.ps1 does all of it). Linux: scripts/build.sh.
cmake_minimum_required(VERSION 3.20)
# Static MSVC runtime (/MT): players need no Visual C++ Redistributable. Set before project() so every target below, third-party code
# included, uses it; the libraries this one links must agree (a mismatch is a link error).
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
project(astc2bc7 C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(WIN32)
    set(A2B_ISPC_DEFAULT "${TP}/ispc/ispc-v1.32.0-windows/bin/ispc.exe")
    set(A2B_ISPC_OS windows)
    set(A2B_OBJ .obj)
    set(A2B_ISPC_PIC)
else()
    set(A2B_ISPC_DEFAULT "${TP}/ispc/ispc-v1.32.0-linux/bin/ispc")
    set(A2B_ISPC_OS linux)
    set(A2B_OBJ .o)
    set(A2B_ISPC_PIC --pic)
endif()
set(A2B_ISPC "${A2B_ISPC_DEFAULT}" CACHE FILEPATH "Intel ISPC compiler (BSD-3)")

# ---- ARM astcenc 5.7.0 (Apache-2.0): decoder-only DLLs, one per ISA, picked at run time by CPUID
set(ASTCENC_CLI OFF CACHE BOOL "" FORCE)
set(ASTCENC_DECOMPRESSOR ON CACHE BOOL "" FORCE)
set(ASTCENC_SHAREDLIB ON CACHE BOOL "" FORCE)
set(ASTCENC_ISA_AVX2 ON CACHE BOOL "" FORCE)
set(ASTCENC_ISA_SSE41 ON CACHE BOOL "" FORCE)
set(ASTCENC_ISA_SSE2 ON CACHE BOOL "" FORCE)
set(ASTCENC_WERROR OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP}/astc-encoder ${CMAKE_BINARY_DIR}/astcenc EXCLUDE_FROM_ALL)

# ---- bc7e.ispc (Apache-2.0, Binomial LLC), multi-target
set(BC7E_SRC ${TP}/bc7enc_rdo/bc7e.ispc)
set(BC7E_OUT ${CMAKE_BINARY_DIR}/bc7e)
file(MAKE_DIRECTORY ${BC7E_OUT})
set(BC7E_OBJS ${BC7E_OUT}/bc7e${A2B_OBJ} ${BC7E_OUT}/bc7e_sse4${A2B_OBJ} ${BC7E_OUT}/bc7e_avx2${A2B_OBJ} ${BC7E_OUT}/bc7e_avx512skx${A2B_OBJ})
add_custom_command(
    OUTPUT ${BC7E_OBJS} ${BC7E_OUT}/bc7e_ispc.h
    COMMAND ${A2B_ISPC} -O2 ${BC7E_SRC} -o ${BC7E_OUT}/bc7e${A2B_OBJ} -h ${BC7E_OUT}/bc7e_ispc.h
            --target=sse4-i32x4,avx2-i32x8,avx512skx-x16 --arch=x86-64 --target-os=${A2B_ISPC_OS} ${A2B_ISPC_PIC}
            --opt=fast-math --opt=disable-assertions --woff
    DEPENDS ${BC7E_SRC}
    COMMENT "ispc bc7e.ispc")
add_library(bc7e_ispc STATIC ${BC7E_OBJS})
set_target_properties(bc7e_ispc PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(bc7e_ispc INTERFACE ${BC7E_OUT})

# ---- library + CLI
add_library(astc2bc7 STATIC src/a2b.cpp ${BC7E_OUT}/bc7e_ispc.h)
target_include_directories(astc2bc7 PUBLIC include PRIVATE ${TP}/astc-encoder/Source)
target_link_libraries(astc2bc7 PUBLIC bc7e_ispc)
target_compile_definitions(astc2bc7 PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(astc2bc7 PRIVATE /W3 -Wno-unused-parameter)
else()
    target_compile_options(astc2bc7 PRIVATE -Wall -Wno-unused-parameter)
    target_link_libraries(astc2bc7 PUBLIC ${CMAKE_DL_LIBS} pthread)
endif()

add_executable(astc2bc7_cli cli/main.cpp)
set_target_properties(astc2bc7_cli PROPERTIES OUTPUT_NAME astc2bc7)
target_include_directories(astc2bc7_cli PRIVATE cli)
target_link_libraries(astc2bc7_cli PRIVATE astc2bc7)
target_compile_definitions(astc2bc7_cli PRIVATE _CRT_SECURE_NO_WARNINGS)
add_dependencies(astc2bc7_cli astcdec-avx2-shared astcdec-sse4.1-shared astcdec-sse2-shared)

# put the three decoder DLLs next to the executable
foreach(isa avx2 sse4.1 sse2)
    add_custom_command(TARGET astc2bc7_cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:astcdec-${isa}-shared> $<TARGET_FILE_DIR:astc2bc7_cli>)
endforeach()
