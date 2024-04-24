CFLAGS := -Wall -std=c11 -pthread -D_GNU_SOURCE

SRC_DIR := .
BUILD_DIR := build

SOURCES := $(wildcard $(SRC_DIR)/*.c)

EXECUTABLES := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%,$(SOURCES))

all: build $(EXECUTABLES)

build:
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<
	
clean:
	@rm -rf $(BUILD_DIR)



