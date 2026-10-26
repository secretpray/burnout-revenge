# Synthetic fixtures only: no generated PPC, game input or fetched dependency.
set(RT ${PROJECT_SOURCE_DIR}/runtime)
find_package(Threads REQUIRED)
function(portable_test name)
    add_executable(${name} ../${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${RT})
    target_compile_options(${name} PRIVATE -O2 -UNDEBUG)
    target_link_libraries(${name} PRIVATE Threads::Threads)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
foreach(name sha1 socket_compat block_ranges texture_volume_layout rectangle_list temporal_history
        burnout_motion composite_controls xma_mmio rumble_focus rewind_shortcuts
        rewind_gate crash_launch_model crash_launch_texture bit_copy xmp_playlist
        notification_queue memory_plan zpd_report edge_bars pipeline_cache_header
        gpu_choice present_schedule audio_cadence whole_vblanks light_trails
        hud_anchor suspend_count vehicle_detail pad_slots online_identity key_map
        wheel_map version_check front_end_matrix)
    portable_test(${name}_test)
endforeach()
portable_test(voice_mix_test)
target_compile_options(voice_mix_test PRIVATE -ffp-contract=off)
portable_test(picture_fit_test ${RT}/video/picture_fit.cpp)
portable_test(refresh_fit_test ${RT}/video/refresh_fit.cpp)
portable_test(guest_time_test ${RT}/cpu/guest_time.cpp)
portable_test(frame_rate_policy_test ${RT}/video/frame_rate_policy.cpp)
portable_test(swapchain_policy_test ${RT}/video/swapchain_policy.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    portable_test(shared_memory_test)
    portable_test(fault_policy_test)
endif()
