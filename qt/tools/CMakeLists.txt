# xgs-diff: the C++ side of the differential test (differential/run.ps1 compares it with differential/Oracle.java).
add_executable(xgs-diff
    differential/main.cpp
    differential/gen.h differential/gen.cpp
    differential/codec.h)
target_link_libraries(xgs-diff PRIVATE xgscore)
# argument_lists.h: the argument lists of tst_system_process_runner (xgs-diff arglists)
target_include_directories(xgs-diff PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests/helpers)
xgs_target_defaults(xgs-diff)
