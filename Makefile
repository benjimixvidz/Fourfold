# Settings of one machine (RACK_DIR, RACK_APP for make gui-test, CHECK_RT), never committed.
-include local.mk
RACK_DIR ?= ../..
export RACK_DIR RACK_APP

SOURCES += $(wildcard src/*.cpp) $(wildcard core/*.cpp)
DISTRIBUTABLES += res LICENSE

EXTRA_CXXFLAGS += -std=c++20
EXTRA_FLAGS += -isystem $(RACK_DIR)/include -isystem $(RACK_DIR)/dep/include
# The sequencer timing maths must not be reassociated (the SDK enables unsafe maths).
EXTRA_FLAGS += -fno-unsafe-math-optimizations

# Where jq is missing (this machine), the slug and version come from plugin.json through python3;
# elsewhere (the VCV Library's build machines) the SDK reads them with jq, as it always does.
ifeq ($(shell command -v jq 2>/dev/null),)
override SLUG := $(shell python3 -c 'import json; print(json.load(open("plugin.json"))["slug"])')
override VERSION := $(shell python3 -c 'import json; print(json.load(open("plugin.json"))["version"])')
endif

include $(RACK_DIR)/plugin.mk

# Core unit tests, built with the host compiler: make test
test: build/core_tests
	build/core_tests

build/core_tests: tests/core_tests.cpp $(wildcard core/*.cpp core/*.h)
	@mkdir -p build
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Icore -o $@ tests/core_tests.cpp $(wildcard core/*.cpp)

# The same tests under AddressSanitizer and UBSan, with strict warnings: make test-sanitize
SANITIZE_FLAGS := -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
	-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Werror
test-sanitize:
	@mkdir -p build
	$(CXX) $(SANITIZE_FLAGS) -Icore -o build/core_tests_san tests/core_tests.cpp $(wildcard core/*.cpp)
	build/core_tests_san

# The gate before a commit: the real-time rule (when CHECK_RT names a checker), the plugin layer
# compiled with strict warnings as errors, then the core tests plain and under the sanitizers:
# make check
check:
	@mkdir -p build/check
	@if [ -n "$(CHECK_RT)" ]; then $(CHECK_RT) || exit 1; fi
	for f in src/*.cpp; do $(CXX) -std=c++20 -Wall -Wextra -Werror -Wno-unused-parameter -O1 \
		-isystem $(RACK_DIR)/include -isystem $(RACK_DIR)/dep/include -Icore -c $$f -o build/check/plugin.o || exit 1; done
	$(MAKE) test
	$(MAKE) test-sanitize
	@echo "check: all clean"

# Real-time budget of the core (regeneration, bank load, render per sample): make bench
bench:
	@mkdir -p build
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Icore -o build/bench tests/bench.cpp $(wildcard core/*.cpp)
	build/bench

# Endurance: one hour of playback, the clock stopping now and then, settings moving: make soak
soak:
	@mkdir -p build
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Icore -o build/soak tests/soak.cpp $(wildcard core/*.cpp)
	build/soak

# Coverage of the lines changed since 2.7.0 (gcov, in a throwaway copy): make coverage
coverage:
	tests/coverage.sh

# The module layer: Tetrad and Tetrad Pipe as built for the plugin (src/ and core/), linked against
# the SDK's libRack and driven sample by sample without an engine or a window: make test-module
test-module: $(OBJECTS) tests/module_tests.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o build/module_tests tests/module_tests.cpp $(OBJECTS) -L$(RACK_DIR) -lRack \
		-Wl,-rpath,$(abspath $(RACK_DIR)) -lpthread
	build/module_tests

# One scenario, every output hashed per phase (tests/compare_2.7.0.sh runs it on 2.7.0 too)
render-dump: $(OBJECTS) tests/render_dump.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o build/render_dump tests/render_dump.cpp $(OBJECTS) -L$(RACK_DIR) -lRack \
		-Wl,-rpath,$(abspath $(RACK_DIR)) -lpthread
	build/render_dump

# Sound against the published 2.7.0 (tag v2.7.0): every phase identical but the two documented ones,
# and the patches saved by earlier versions (tests/fixtures/) playing the same
compare-2.7.0: render-dump
	tests/compare_2.7.0.sh

# The RUN input menu in a throwaway Rack (its own user folder), driven by synthetic events, its
# saved state checked; in a nested X server when no monitor is on: make gui-test
gui-test: plugin.so
	tests/gui/run.sh

# Fixtures: a module saved by 2.6.2, 2.6.7 and 2.7.0 (built from their tags), committed; run once
fixtures:
	tests/fixtures/make_fixtures.sh

.PHONY: test test-sanitize check test-module render-dump compare-2.7.0 gui-test fixtures bench soak coverage
