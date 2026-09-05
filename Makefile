CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -Iinc
SRC = src/main.cpp src/Linker.cpp
TARGET = mllinker

.PHONY: all test test-component test-integration test-all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# The current linker suite is self-contained: it generates objects and verifies linking.
test-component: $(TARGET)
	python3 test/run_integration_tests.py

# Backward-compatible aliases for the historical target names. test-component is
# the one qa/tests/test-all.py invokes; on the profiler-map branch it was an
# empty rule, so `make qa ARGS=linker` ran nothing and reported success.
test-integration: test-component
test-e2e: test-component
test: test-component
test-all: test-component

clean:
	rm -f $(TARGET)
