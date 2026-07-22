CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -Iinc
SRC = src/main.cpp src/Linker.cpp
TARGET = mllinker

.PHONY: all test test-integration test-all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

test-integration: $(TARGET)
	python3 test/run_integration_tests.py

# The current linker suite is self-contained: it generates objects and verifies linking.
test: test-integration

test-all: test

clean:
	rm -f $(TARGET)
