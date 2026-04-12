# monolith-engine — convenience wrapper around CMake presets
# Usage: make [target]
#
# On Windows (MSVC):  uses debug-msvc / release-msvc presets automatically
# On macOS / Linux:   uses debug / release presets (Ninja)

CMAKE_E := cmake -E
MKDIR_P := $(CMAKE_E) make_directory
RM_RF   := $(CMAKE_E) rm -rf

ifeq ($(OS),Windows_NT)
    WIN_CURDIR  := $(subst /,\,$(CURDIR))
    PRESET_DBG  ?= debug-msvc
    PRESET_REL  ?= release-msvc
    TESTS_BIN   := $(CURDIR)/build/$(PRESET_DBG)/bin/tests
    SENTINEL_DBG := build/$(PRESET_DBG)/MonolithEngine.sln
    SENTINEL_REL := build/$(PRESET_REL)/MonolithEngine.sln
    BUILD_DBG   = cmake --build build/$(PRESET_DBG) --config Debug --parallel 1
    BUILD_REL   = cmake --build build/$(PRESET_REL) --config Release --parallel 1
    TEST_CMD    = ctest --test-dir build/$(PRESET_DBG) -C Debug --output-on-failure
    EDITOR_EXE  := build/$(PRESET_DBG)/bin/Debug/HoroEditor.exe

    # Coverage — Windows: OpenCppCoverage (install via: winget install OpenCppCoverage.OpenCppCoverage)
    OPENCOV     := "C:/Program Files/OpenCppCoverage/OpenCppCoverage.exe"
    COV_DIR     := $(CURDIR)/build/coverage
    COV_FLAGS   := --sources "$(WIN_CURDIR)"
    COV_REPORT  := $(COV_DIR)/html/index.html
else
    PRESET_DBG  ?= debug
    PRESET_REL  ?= release
    SENTINEL_DBG := build/$(PRESET_DBG)/build.ninja
    SENTINEL_REL := build/$(PRESET_REL)/build.ninja
    BUILD_DBG   = cmake --build --preset $(PRESET_DBG)
    BUILD_REL   = cmake --build --preset $(PRESET_REL)
    TEST_CMD    = ctest --preset debug
    EDITOR_EXE  := build/$(PRESET_DBG)/bin/HoroEditor

    # Coverage — Linux/macOS: gcov + lcov  (apt: lcov  |  brew: lcov)
    PRESET_COV  ?= coverage
    SENTINEL_COV := build/$(PRESET_COV)/build.ninja
    COV_DIR     := build/coverage
    COV_REPORT  := $(COV_DIR)/html/index.html
endif

# Source files to format: all tracked .cpp/.h (excluding vendor/)
ifeq ($(OS),Windows_NT)
    FORMAT_SOURCES := $(shell git ls-files "*.cpp" "*.h" | findstr /V /R "^vendor/")
else
    FORMAT_SOURCES := $(shell git ls-files '*.cpp' '*.h' | grep -v '^vendor/')
endif

# Prefer VS-bundled clang-format on Windows; fall back to PATH
CLANG_FORMAT_VS := C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe
ifeq ($(OS),Windows_NT)
    CLANG_FORMAT ?= $(CLANG_FORMAT_VS)
else
    CLANG_FORMAT ?= clang-format
endif

.PHONY: all configure build test test-ui test\:ui test-ui-headless test\:ui-headless release coverage clean clean-all format format-check help

# Default: build debug
all: build

## Configure debug (run once, or after touching CMakeLists.txt)
configure:
	cmake --preset $(PRESET_DBG)

## Build debug (configures automatically if build dir is missing)
build: $(SENTINEL_DBG)
	$(BUILD_DBG)

$(SENTINEL_DBG):
	cmake --preset $(PRESET_DBG)

## Build + run all 23 engine unit tests (debug)
test: build
	$(TEST_CMD)

## Build + run standalone ImGui UI smoke tests from terminal (debug)
test-ui-headless: build
	ctest --test-dir build/$(PRESET_DBG) -C Debug --output-on-failure -R test_standalone_ui

## Alias for test-ui-headless (npm-style naming)
test\:ui-headless: test-ui-headless

## Build + launch standalone editor UI window (manual UI verification mode)
test-ui: build
	"$(EDITOR_EXE)"

## Alias for test-ui (npm-style naming)
test\:ui: test-ui

## Build release library
release: $(SENTINEL_REL)
	$(BUILD_REL)

$(SENTINEL_REL):
	cmake --preset $(PRESET_REL)

## Format all sources in-place (Google style via .clang-format)
format:
	"$(CLANG_FORMAT)" -i $(FORMAT_SOURCES)

## Check formatting without modifying files (exits non-zero if changes needed)
format-check:
	"$(CLANG_FORMAT)" --dry-run --Werror $(FORMAT_SOURCES)

# ----------------------------------------------------------------
# Coverage
# ----------------------------------------------------------------

ifeq ($(OS),Windows_NT)

## Generate HTML coverage report using OpenCppCoverage (Windows/MSVC)
## Requires: winget install OpenCppCoverage.OpenCppCoverage
coverage: build
	@$(MKDIR_P) "$(COV_DIR)"
	@echo "[coverage] collecting test_math ..."
	@$(OPENCOV) $(COV_FLAGS) \
	    --export_type binary:"$(COV_DIR)/cov0.cov" \
	    -- "$(TESTS_BIN)/test_math.exe"
	@echo "[coverage] collecting test_math_extended ..."
	@$(OPENCOV) $(COV_FLAGS) \
	    --input_coverage "$(COV_DIR)/cov0.cov" \
	    --export_type binary:"$(COV_DIR)/cov1.cov" \
	    -- "$(TESTS_BIN)/test_math_extended.exe"
	@echo "[coverage] collecting test_physics ..."
	@$(OPENCOV) $(COV_FLAGS) \
	    --input_coverage "$(COV_DIR)/cov1.cov" \
	    --export_type binary:"$(COV_DIR)/cov2.cov" \
	    -- "$(TESTS_BIN)/test_physics.exe"
	@echo "[coverage] collecting test_physics_extended ..."
	@$(OPENCOV) $(COV_FLAGS) \
	    --input_coverage "$(COV_DIR)/cov2.cov" \
	    --export_type binary:"$(COV_DIR)/cov3.cov" \
	    -- "$(TESTS_BIN)/test_physics_extended.exe"
	@echo "[coverage] collecting test_ecs + generating HTML report ..."
	@$(OPENCOV) $(COV_FLAGS) \
	    --input_coverage "$(COV_DIR)/cov3.cov" \
	    --export_type html:"$(COV_DIR)/html" \
	    -- "$(TESTS_BIN)/test_ecs.exe"
	@echo ""
	@echo "Coverage report: $(COV_REPORT)"

else

## Generate HTML coverage report using gcov + lcov (Linux / macOS)
## Requires: apt install lcov  |  brew install lcov
$(SENTINEL_COV):
	cmake --preset $(PRESET_COV)

coverage: $(SENTINEL_COV)
	cmake --build build/$(PRESET_COV)
	@$(MKDIR_P) "$(COV_DIR)"
	ctest --test-dir build/$(PRESET_COV) --output-on-failure
	lcov --capture \
	     --directory build/$(PRESET_COV) \
	     --output-file "$(COV_DIR)/raw.info" \
	     --rc lcov_branch_coverage=1
	lcov --remove "$(COV_DIR)/raw.info" \
	     '*/vendor/*' '*/_deps/*' '*/tests/*' \
	     --output-file "$(COV_DIR)/filtered.info" \
	     --rc lcov_branch_coverage=1
	genhtml "$(COV_DIR)/filtered.info" \
	        --output-directory "$(COV_DIR)/html" \
	        --branch-coverage \
	        --title "MonolithEngine Coverage"
	@echo ""
	@echo "Coverage report: $(COV_REPORT)"

endif

## Wipe debug build directory
clean:
	$(RM_RF) "build/$(PRESET_DBG)"

## Wipe all build directories (including coverage)
clean-all:
	$(RM_RF) build

## Print available targets
help:
	@echo ""
	@echo "  make              Build debug"
	@echo "  make test         Build & run all 23 engine tests"
	@echo "  make test-ui-headless Build & run standalone UI smoke tests in terminal"
	@echo "  make test:ui-headless Alias of test-ui-headless"
	@echo "  make test-ui      Build & launch standalone editor UI window"
	@echo "  make test:ui      Alias of test-ui"
	@echo "  make coverage     Build, run tests, generate HTML coverage report"
	@echo "  make release      Build release library"
	@echo "  make configure    Run cmake --preset only"
	@echo "  make clean        Remove debug build dir"
	@echo "  make clean-all    Remove all build dirs (incl. coverage)"
	@echo "  make format       Format all sources in-place"
	@echo "  make format-check Check formatting (exits non-zero if changes needed)"
	@echo ""
	@echo "CI / coverage:"
	@echo "  Windows: make coverage  (requires OpenCppCoverage — winget install OpenCppCoverage.OpenCppCoverage)"
	@echo "  Linux:   make coverage  (requires lcov — apt install lcov)"
	@echo "  GitHub:  see .github/workflows/ci.yml"
	@echo ""
