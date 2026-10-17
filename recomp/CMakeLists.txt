# Compiles XenonRecomp's output (../ppc, generated from the user's own XEX and
# never committed) into a static library. Regenerate ppc/ with:
#   tools/XenonRecomp/build/XenonRecomp/XenonRecomp config/burnout.toml tools/XenonRecomp/XenonUtils/ppc_context.h
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Built from the top-level CMakeLists.txt.
file(GLOB PPC_SOURCES ${BR_PPC_DIR}/*.cpp)
if(NOT PPC_SOURCES)
    message(FATAL_ERROR "No generated sources in ${BR_PPC_DIR}. Run XenonRecomp first.")
endif()

set(BR_PREBUILT_RECOMP "" CACHE FILEPATH "Optional locally built Burnout library; never distributed")
if(BR_PREBUILT_RECOMP)
    add_library(${BR_RECOMP_TARGET} STATIC IMPORTED GLOBAL)
    set_target_properties(${BR_RECOMP_TARGET} PROPERTIES IMPORTED_LOCATION "${BR_PREBUILT_RECOMP}"
        INTERFACE_INCLUDE_DIRECTORIES "${BR_PPC_DIR};${ROOT}/tools/XenonRecomp/XenonUtils;${ROOT}/tools/XenonRecomp/thirdparty/simde")
    return()
endif()

add_library(${BR_RECOMP_TARGET} STATIC ${PPC_SOURCES})
target_compile_features(${BR_RECOMP_TARGET} PUBLIC cxx_std_20)
target_include_directories(${BR_RECOMP_TARGET} PUBLIC
    ${BR_PPC_DIR}
    ${ROOT}/tools/XenonRecomp/XenonUtils
    ${ROOT}/tools/XenonRecomp/thirdparty/simde)
target_compile_options(${BR_RECOMP_TARGET} PRIVATE
    -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable
    -fno-strict-aliasing
    # Checked indirect calls (runtime/cpu/indirect.cpp). SHELL: keeps each
    # -include with its file (CMake drops a repeated plain -include).
    "SHELL:-include \"${ROOT}/runtime/cpu/indirect.h\""
    # mftb reads the game's clock, which stands still while it's suspended.
    "SHELL:-include \"${ROOT}/runtime/cpu/guest_time.h\"")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(${BR_RECOMP_TARGET} PRIVATE -march=x86-64-v3)
endif()
if(BR_GAME STREQUAL "burnout")
    target_compile_options(${BR_RECOMP_TARGET} PRIVATE "SHELL:-include \"${ROOT}/runtime/cpu/burnout_mmio.h\"")
endif()
if(IOS)
    target_compile_options(${BR_RECOMP_TARGET} PRIVATE -mcpu=apple-a15)
endif()
