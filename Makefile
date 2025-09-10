CC = gcc
CFLAGS = -Wall -Wextra -O3 -march=native -Iinclude
LDFLAGS = 

BUILD = build

CORE = src/core/feather.c
PLATFORM = src/platform/linux/impl.c src/platform/linux/coro.c
EXAMPLES = examples/main.c

OBJ = ${BUILD}/feather.o $(BUILD)/impl.o $(BUILD)/coro.o $(BUILD)/main.o

TARGET = $(BUILD)/server

all: $(TARGET)

$(BUILD)/feather.o: $(CORE) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/impl.o: src/platform/linux/impl.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/coro.o: src/platform/linux/coro.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/main.o: $(EXAMPLES) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET) $(LDFLAGS)

build:
	mkdir -p build

clean:
	rm -rf build
