# xgs_add_test(<name> <sources...>): console Qt Test executable linked with xgscore, registered with ctest.
function(xgs_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test xgscore)
    xgs_target_defaults(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

xgs_add_test(tst_smoke tst_smoke.cpp)
