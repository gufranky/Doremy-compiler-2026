# ToyC Compiler Makefile

# Compiler and flags
ifeq ($(shell uname -s),Linux)
CXX ?= g++
else
CXX ?= C:/mingw64/bin/g++.exe
endif
CXXFLAGS = -std=c++17 -Wall -g -Iinclude -Isrc/frontend
LDFLAGS =

# Directories
SRC_DIR = src
FRONTEND_DIR = $(SRC_DIR)/frontend
BUILD_DIR = build

# Source files
FRONTEND_SRCS = $(FRONTEND_DIR)/ast.cpp $(FRONTEND_DIR)/semantic_analyzer.cpp $(FRONTEND_DIR)/preprocessor.cpp $(FRONTEND_DIR)/parse_driver.cpp
FRONTEND_GEN_SRCS = $(FRONTEND_DIR)/lex.yy.c $(FRONTEND_DIR)/parser.tab.c
IR_SRCS = $(wildcard src/ir/*.cpp)
MIDIR_SRCS = $(wildcard src/midir/*.cpp)
BACKEND_SRCS = $(wildcard src/backend/*.cpp)
MAIN_SRC = main.cpp

# Object files
FRONTEND_OBJS = $(patsubst $(FRONTEND_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(FRONTEND_SRCS))
FRONTEND_GEN_OBJS = $(patsubst $(FRONTEND_DIR)/%.c,$(BUILD_DIR)/%.o,$(FRONTEND_GEN_SRCS))
IR_OBJS = $(patsubst src/ir/%.cpp,$(BUILD_DIR)/%.o,$(IR_SRCS))
MIDIR_OBJS = $(patsubst src/midir/%.cpp,$(BUILD_DIR)/%.o,$(MIDIR_SRCS))
BACKEND_OBJS = $(patsubst src/backend/%.cpp,$(BUILD_DIR)/%.o,$(BACKEND_SRCS))
MAIN_OBJ = $(BUILD_DIR)/main.o
# Target executable
TARGET = compiler

# Default target
all: $(TARGET)

# Create directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile frontend sources
$(FRONTEND_OBJS): $(BUILD_DIR)/%.o: $(FRONTEND_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(FRONTEND_GEN_OBJS): $(BUILD_DIR)/%.o: $(FRONTEND_DIR)/%.c | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(IR_OBJS): $(BUILD_DIR)/%.o: src/ir/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(MIDIR_OBJS): $(BUILD_DIR)/%.o: src/midir/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BACKEND_OBJS): $(BUILD_DIR)/%.o: src/backend/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile main
$(MAIN_OBJ): $(MAIN_SRC) $(FRONTEND_DIR)/parser.tab.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(MAIN_SRC) -o $(MAIN_OBJ)

# Link executable
$(TARGET): $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS) $(IR_OBJS) $(MIDIR_OBJS) $(BACKEND_OBJS) $(MAIN_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $(TARGET) $(LDFLAGS)

# Test with functional test cases
test: $(TARGET)
	@echo "========================================="
	@echo "Running Functional Tests"
	@echo "========================================="
	@passed=0; failed=0; \
	for test_file in testcases/functional/*.c; do \
		echo -n "Testing $$test_file... "; \
		if ./$(TARGET) < $$test_file > /dev/null 2>&1; then \
			echo "✓ PASSED"; \
			passed=$$((passed + 1)); \
		else \
			echo "✗ FAILED"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "========================================="; \
	echo "Results: $$passed passed, $$failed failed"; \
	echo "========================================="

# Test with verbose output
test-verbose: $(TARGET)
	@echo "========================================="
	@echo "Running Functional Tests (Verbose)"
	@echo "========================================="
	@for test_file in testcases/functional/*.c; do \
		echo ""; \
		echo "Testing $$test_file:"; \
		./$(TARGET) < $$test_file 2>&1 || true; \
		echo ""; \
	done

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all test test-verbose clean distclean
