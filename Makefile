# ToyC Compiler Makefile

# Compiler and flags
CXX = g++
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
OPT_SRCS = $(wildcard src/optimize/*.cpp)
BACKEND_SRCS = $(wildcard src/backend/*.cpp)
MAIN_SRC = main.cpp

# Object files
FRONTEND_OBJS = $(patsubst $(FRONTEND_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(FRONTEND_SRCS))
FRONTEND_GEN_OBJS = $(patsubst $(FRONTEND_DIR)/%.c,$(BUILD_DIR)/%.o,$(FRONTEND_GEN_SRCS))
IR_OBJS = $(patsubst src/ir/%.cpp,$(BUILD_DIR)/%.o,$(IR_SRCS))
OPT_OBJS = $(patsubst src/optimize/%.cpp,$(BUILD_DIR)/%.o,$(OPT_SRCS))
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

$(OPT_OBJS): $(BUILD_DIR)/%.o: src/optimize/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BACKEND_OBJS): $(BUILD_DIR)/%.o: src/backend/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile main
$(MAIN_OBJ): $(MAIN_SRC) $(FRONTEND_DIR)/parser.tab.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(MAIN_SRC) -o $(MAIN_OBJ)

# Link executable
$(TARGET): $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS) $(IR_OBJS) $(OPT_OBJS) $(BACKEND_OBJS) $(MAIN_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $(TARGET) $(LDFLAGS)

# IR unit tests (M1, M2)
IR_M1_TEST_SRC = tests/ir_m1_tests.cpp
IR_M1_TEST_OBJ = $(BUILD_DIR)/ir_m1_tests.o
IR_M1_TEST_BIN = ir_m1_tests

IR_M2_TEST_SRC = tests/ir_m2_tests.cpp
IR_M2_TEST_OBJ = $(BUILD_DIR)/ir_m2_tests.o
IR_M2_TEST_BIN = ir_m2_tests

IR_M3_TEST_SRC = tests/ir_m3_tests.cpp
IR_M3_TEST_OBJ = $(BUILD_DIR)/ir_m3_tests.o
IR_M3_TEST_BIN = ir_m3_tests

$(IR_M1_TEST_OBJ): $(IR_M1_TEST_SRC) include/ir.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(IR_M1_TEST_SRC) -o $(IR_M1_TEST_OBJ)

$(IR_M1_TEST_BIN): $(IR_M1_TEST_OBJ) $(IR_OBJS) $(OPT_OBJS) $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $(IR_M1_TEST_BIN)

$(IR_M2_TEST_OBJ): $(IR_M2_TEST_SRC) include/ir.h include/ir_generator.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(IR_M2_TEST_SRC) -o $(IR_M2_TEST_OBJ)

$(IR_M2_TEST_BIN): $(IR_M2_TEST_OBJ) $(IR_OBJS) $(OPT_OBJS) $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $(IR_M2_TEST_BIN)

$(IR_M3_TEST_OBJ): $(IR_M3_TEST_SRC) include/ir.h include/cfg.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(IR_M3_TEST_SRC) -o $(IR_M3_TEST_OBJ)

$(IR_M3_TEST_BIN): $(IR_M3_TEST_OBJ) $(IR_OBJS) $(OPT_OBJS) $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $(IR_M3_TEST_BIN)

IR_M4_TEST_SRC = tests/ir_m4_tests.cpp
IR_M4_TEST_OBJ = $(BUILD_DIR)/ir_m4_tests.o
IR_M4_TEST_BIN = ir_m4_tests

MIDIR_TEST_SRC = tests/midir_tests.cpp
MIDIR_TEST_OBJ = $(BUILD_DIR)/midir_tests.o
MIDIR_TEST_BIN = midir_tests

BACKEND_B1_TEST_SRC = tests/backend_b1_tests.cpp
BACKEND_B1_TEST_OBJ = $(BUILD_DIR)/backend_b1_tests.o
BACKEND_B1_TEST_BIN = backend_b1_tests

BACKEND_B2_TEST_SRC = tests/backend_b2_tests.cpp
BACKEND_B2_TEST_OBJ = $(BUILD_DIR)/backend_b2_tests.o
BACKEND_B2_TEST_BIN = backend_b2_tests

BACKEND_B3_TEST_SRC = tests/backend_b3_tests.cpp
BACKEND_B3_TEST_OBJ = $(BUILD_DIR)/backend_b3_tests.o
BACKEND_B3_TEST_BIN = backend_b3_tests

BACKEND_B4_TEST_SRC = tests/backend_b4_tests.cpp
BACKEND_B4_TEST_OBJ = $(BUILD_DIR)/backend_b4_tests.o
BACKEND_B4_TEST_BIN = backend_b4_tests

$(IR_M4_TEST_OBJ): $(IR_M4_TEST_SRC) include/ir.h include/optimizer.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(IR_M4_TEST_SRC) -o $(IR_M4_TEST_OBJ)

$(IR_M4_TEST_BIN): $(IR_M4_TEST_OBJ) $(IR_OBJS) $(OPT_OBJS) $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $(IR_M4_TEST_BIN)

$(MIDIR_TEST_OBJ): $(MIDIR_TEST_SRC) include/ir.h include/midir.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(MIDIR_TEST_SRC) -o $(MIDIR_TEST_OBJ)

$(MIDIR_TEST_BIN): $(MIDIR_TEST_OBJ) $(IR_OBJS) $(OPT_OBJS) $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $(MIDIR_TEST_BIN)

$(BACKEND_B1_TEST_OBJ): $(BACKEND_B1_TEST_SRC) include/ir.h include/backend_codegen.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(BACKEND_B1_TEST_SRC) -o $(BACKEND_B1_TEST_OBJ)


$(BACKEND_B1_TEST_BIN): $(BACKEND_B1_TEST_OBJ) $(IR_OBJS) $(BACKEND_OBJS) $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $(BACKEND_B1_TEST_BIN)

$(BACKEND_B2_TEST_OBJ): $(BACKEND_B2_TEST_SRC) include/ir.h include/backend_codegen.h include/optimizer.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(BACKEND_B2_TEST_SRC) -o $(BACKEND_B2_TEST_OBJ)

$(BACKEND_B2_TEST_BIN): $(BACKEND_B2_TEST_OBJ) $(IR_OBJS) $(OPT_OBJS) $(BACKEND_OBJS) $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $(BACKEND_B2_TEST_BIN)

$(BACKEND_B3_TEST_OBJ): $(BACKEND_B3_TEST_SRC) include/ir.h include/backend_liveness.h include/backend_regalloc.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(BACKEND_B3_TEST_SRC) -o $(BACKEND_B3_TEST_OBJ)

$(BACKEND_B3_TEST_BIN): $(BACKEND_B3_TEST_OBJ) $(IR_OBJS) $(BACKEND_OBJS) $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $(BACKEND_B3_TEST_BIN)

$(BACKEND_B4_TEST_OBJ): $(BACKEND_B4_TEST_SRC) include/ir.h include/backend_codegen.h include/optimizer.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(BACKEND_B4_TEST_SRC) -o $(BACKEND_B4_TEST_OBJ)

$(BACKEND_B4_TEST_BIN): $(BACKEND_B4_TEST_OBJ) $(IR_OBJS) $(OPT_OBJS) $(BACKEND_OBJS) $(FRONTEND_OBJS) $(FRONTEND_GEN_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $(BACKEND_B4_TEST_BIN)

ir-test: $(IR_M1_TEST_BIN)
	./$(IR_M1_TEST_BIN)

ir-m2-test: $(IR_M2_TEST_BIN)
	./$(IR_M2_TEST_BIN)

ir-m3-test: $(IR_M3_TEST_BIN)
	./$(IR_M3_TEST_BIN)

ir-m4-test: $(IR_M4_TEST_BIN)
	./$(IR_M4_TEST_BIN)

midir-test: $(MIDIR_TEST_BIN)
	./$(MIDIR_TEST_BIN)

backend-b1-test: $(BACKEND_B1_TEST_BIN)
	./$(BACKEND_B1_TEST_BIN)

backend-b2-test: $(BACKEND_B2_TEST_BIN)
	./$(BACKEND_B2_TEST_BIN)

backend-b3-test: $(BACKEND_B3_TEST_BIN)
	./$(BACKEND_B3_TEST_BIN)

backend-b4-test: $(BACKEND_B4_TEST_BIN)
	./$(BACKEND_B4_TEST_BIN)

ir-tests: ir-test ir-m2-test ir-m3-test ir-m4-test midir-test

backend-tests: backend-b1-test backend-b2-test backend-b3-test backend-b4-test

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
	rm -rf $(BUILD_DIR) $(TARGET) $(IR_M1_TEST_BIN) $(IR_M2_TEST_BIN) $(IR_M3_TEST_BIN) $(IR_M4_TEST_BIN) $(MIDIR_TEST_BIN) $(BACKEND_B1_TEST_BIN) $(BACKEND_B2_TEST_BIN) $(BACKEND_B3_TEST_BIN) $(BACKEND_B4_TEST_BIN)

.PHONY: all test test-verbose clean distclean
