RACK_DIR ?= ../..

SOURCES += $(wildcard src/*.cpp) $(wildcard core/*.cpp)
DISTRIBUTABLES += res LICENSE

EXTRA_CXXFLAGS += -std=c++17
# The sequencer timing maths must not be reassociated (the SDK enables unsafe maths).
EXTRA_FLAGS += -fno-unsafe-math-optimizations

include $(RACK_DIR)/plugin.mk

# Core unit tests, built with the host compiler: make test
test: build/core_tests
	build/core_tests

build/core_tests: tests/core_tests.cpp $(wildcard core/*.cpp core/*.h)
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Icore -o $@ tests/core_tests.cpp $(wildcard core/*.cpp)

.PHONY: test
