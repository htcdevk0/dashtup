CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -pedantic -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
LDFLAGS ?=
BUILD_DIR := build
TARGET := dashtup
SOURCES := src/main.c src/ui.c src/fs.c src/tar_reader.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
PAYLOAD_TAR := $(BUILD_DIR)/payload.tar
PAYLOAD_OBJ := $(BUILD_DIR)/payload.o

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJECTS) $(PAYLOAD_OBJ)
	$(CC) $(OBJECTS) $(PAYLOAD_OBJ) -o $@ $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(PAYLOAD_TAR): | $(BUILD_DIR)
	tar -cf $@ -C resources bin .dash

$(PAYLOAD_OBJ): $(PAYLOAD_TAR)
	ld -r -b binary -o $@ $<

run: $(TARGET)
	./$(TARGET) help

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
