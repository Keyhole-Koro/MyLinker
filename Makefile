CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -Iinc
SRC = src/main.cpp src/Linker.cpp
TARGET = mllinker

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)