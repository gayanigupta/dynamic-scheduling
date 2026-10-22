CXX = /opt/homebrew/bin/g++-14
# The -D_Alignof flag works around Homebrew GCC rejecting C11 _Alignof in
# newer macOS Command Line Tools SDK headers; harmless on other platforms.
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Isrc -D'_Alignof(x)=alignof(x)'

SRC = src/task_graph.cpp src/infra_graph.cpp src/scheduler.cpp \
      src/sim.cpp src/recovery.cpp src/optimal.cpp src/generators.cpp src/milp_export.cpp
HDR = src/task_graph.h src/infra_graph.h src/scheduler.h \
      src/sim.h src/recovery.h src/generators.h

all: experiment unit_tests

experiment: $(SRC) $(HDR) src/main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) src/main.cpp

unit_tests: $(SRC) $(HDR) tests/test_main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) tests/test_main.cpp

test: unit_tests
	./unit_tests

clean:
	rm -f experiment unit_tests

.PHONY: all test clean
