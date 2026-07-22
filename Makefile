CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -Iinc
SRC = src/main.cpp src/Linker.cpp
TARGET = mllinker

.PHONY: all test-integration test-all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

test-integration: $(TARGET)
	python3 test/run_integration_tests.py

test-all: test-integration

clean:
	rm -f $(TARGET)
