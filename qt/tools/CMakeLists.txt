# xgs-diff: the C++ side of the differential test (differential/run.ps1 compares it with differential/Oracle.java).
add_executable(xgs-diff
    differential/main.cpp
    differential/gen.h differential/gen.cpp
    differential/codec.h)
target_link_libraries(xgs-diff PRIVATE xgscore)
xgs_target_defaults(xgs-diff)
