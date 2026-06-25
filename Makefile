CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O0 -g -Itests -Icomponents/balboa_spa/protocol

PROTO_SRC := $(wildcard components/balboa_spa/protocol/*.cpp)
TEST_SRC  := $(wildcard tests/test_*.cpp)

build/run_tests: $(PROTO_SRC) $(TEST_SRC) | build
	$(CXX) $(CXXFLAGS) $(PROTO_SRC) $(TEST_SRC) -o $@

build:
	mkdir -p build

test: build/run_tests
	./build/run_tests

clean:
	rm -rf build

.PHONY: test clean
