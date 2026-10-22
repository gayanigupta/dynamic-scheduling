cmake_minimum_required(VERSION 3.16)
project(dynamic_scheduling CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -O2)

add_library(dscore
  src/task_graph.cpp
  src/infra_graph.cpp
  src/scheduler.cpp
  src/sim.cpp
  src/recovery.cpp
  src/optimal.cpp
  src/generators.cpp
  src/milp_export.cpp
)
target_include_directories(dscore PUBLIC src)

add_executable(experiment src/main.cpp)
target_link_libraries(experiment dscore)

add_executable(unit_tests tests/test_main.cpp)
target_link_libraries(unit_tests dscore)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
