CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -Iinc
SRC = src/main.cpp src/Linker.cpp
TARGET = mllinker

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

test-e2e: $(TARGET)
	python3 test/run_integration_tests.py

test-integration: test-e2e

test-component:

test-all: test-component test-e2e

clean:
	rm -f $(TARGET)
