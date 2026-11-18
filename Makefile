CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?=

BUILD := build
SRC := src/quadtree.c
TEST := $(BUILD)/test_quadtree

.PHONY: all test clean demo

all: $(TEST)

$(BUILD):
	mkdir -p $(BUILD)

$(TEST): $(SRC) tests/test_quadtree.c src/quadtree.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRC) tests/test_quadtree.c $(LDFLAGS)

test: $(TEST)
	./$(TEST)

demo: | $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/cull_demo src/quadtree.c tools/cull_demo.c $(LDFLAGS)
	./$(BUILD)/cull_demo

clean:
	rm -rf $(BUILD)
