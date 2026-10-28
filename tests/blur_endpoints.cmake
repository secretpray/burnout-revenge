# Compile the generated GLSL helper's scalar/vector arithmetic as C++.
# This checks endpoint identity without Vulkan, a game install or a copied formula.
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    message(STATUS "Blur endpoint test needs a native build or CMAKE_CROSSCOMPILING_EMULATOR")
    return()
endif()
set(blur_helper ${CMAKE_CURRENT_BINARY_DIR}/blur_tap_generated.h)
add_custom_command(OUTPUT ${blur_helper}
    COMMAND composite_controls_test --emit-blur-helper ${blur_helper}
    DEPENDS composite_controls_test
    VERBATIM)
add_executable(blur_endpoints_test ${CMAKE_CURRENT_LIST_DIR}/blur_endpoints_test.cpp ${blur_helper})
target_include_directories(blur_endpoints_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${RT})
target_compile_options(blur_endpoints_test PRIVATE -O2 -UNDEBUG -ffp-contract=off)
add_test(NAME blur_endpoints_test COMMAND blur_endpoints_test)
