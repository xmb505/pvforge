add_executable(pvf_solver_tests
    SolverTests.cpp
)

target_link_libraries(pvf_solver_tests PRIVATE
    pvf_solver
)

add_test(NAME pvf_solver_tests COMMAND pvf_solver_tests)
