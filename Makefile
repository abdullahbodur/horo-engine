# horo-engine — convenience wrapper around CMake presets
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
    SENTINEL_DBG := build/$(PRESET_DBG)/HoroEngine.sln
    SENTINEL_REL := build/$(PRESET_REL)/HoroEngine.sln
    BUILD_DBG   = cmake --build build/$(PRESET_DBG) --config Debug --parallel 1
    BUILD_REL   = cmake --build build/$(PRESET_REL) --config Release --parallel 1
    BUILD_LAUNCHER_UNIT = cmake --build build/$(PRESET_DBG) --config Debug --target test_launcher_unit --parallel 1
    BUILD_UI_WINDOWED = cmake --build build/$(PRESET_DBG) --config Debug --target HoroEditorUiTest --parallel 1
    RUN_UI_WINDOWED = build/$(PRESET_DBG)/bin/Debug/HoroEditorUiTest.exe --run-ui-tests
    TEST_CMD    = ctest --test-dir build/$(PRESET_DBG) -C Debug --output-on-failure

    # Coverage — Windows: OpenCppCoverage (install via: winget install OpenCppCoverage.OpenCppCoverage)
    OPENCOV     := "C:/Program Files/OpenCppCoverage/OpenCppCoverage.exe"
    COV_DIR     := $(CURDIR)/build/coverage
    COV_FLAGS   := --sources "$(WIN_CURDIR)" --excluded_sources "$(WIN_CURDIR)\vendor\*" --excluded_sources "$(WIN_CURDIR)\build\*" --excluded_sources "$(WIN_CURDIR)\tests\*"
    COV_REPORT  := $(COV_DIR)/html/index.html
else
    PRESET_DBG  ?= debug
    PRESET_REL  ?= release
    SENTINEL_DBG := build/$(PRESET_DBG)/build.ninja
    SENTINEL_REL := build/$(PRESET_REL)/build.ninja
    BUILD_DBG   = cmake --build --preset $(PRESET_DBG)
    BUILD_REL   = cmake --build --preset $(PRESET_REL)
    BUILD_LAUNCHER_UNIT = cmake --build --preset $(PRESET_DBG) --target test_launcher_unit
    BUILD_UI_WINDOWED = cmake --build --preset $(PRESET_DBG) --target HoroEditorUiTest
    RUN_UI_WINDOWED = build/$(PRESET_DBG)/bin/HoroEditorUiTest --run-ui-tests
    TEST_CMD    = ctest --preset debug

    # Coverage — Linux/macOS: lcov
    PRESET_COV  ?= coverage
    SENTINEL_COV := build/$(PRESET_COV)/build.ninja
    COV_DIR     := build/coverage
    COV_REPORT  := $(COV_DIR)/html/index.html
endif

UI_TEST_DELAY_MS ?= 0
UI_TEST_CAPTURE ?= 0
UI_TEST_OUTPUT_DIR ?= $(CURDIR)/ui_test_output
TEST_LOG_LEVEL ?= debug
COVERAGE_UI_FILTER ?= launcher/create_project_from_launcher,editor/properties_panel_light_controls_workflow,editor/properties_panel_mixed_selection_workflow,editor/properties_panel_rename_delete_undo_workflow,editor/properties_panel_scene_save_reload_workflow

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

.PHONY: all configure build test ui-test ui-test-windowed release coverage coverage-source-summary clean clean-all format format-check help

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
	$(CMAKE_E) env HORO_LOG_LEVEL=$(TEST_LOG_LEVEL) $(TEST_CMD)

## Build + run launcher unit tests (Catch2; no window)
ui-test: $(SENTINEL_DBG)
	$(BUILD_LAUNCHER_UNIT)
	$(CMAKE_E) env HORO_LOG_LEVEL=$(TEST_LOG_LEVEL) ctest --test-dir build/$(PRESET_DBG) -C Debug --output-on-failure -R test_launcher_unit

## Build + run windowed launcher UI automation (optional capture/delay)
ui-test-windowed: $(SENTINEL_DBG)
	$(BUILD_UI_WINDOWED)
	$(MKDIR_P) "$(UI_TEST_OUTPUT_DIR)"
	$(CMAKE_E) env HORO_LOG_LEVEL=$(TEST_LOG_LEVEL) HORO_UI_TEST_CAPTURE=$(UI_TEST_CAPTURE) HORO_UI_TEST_DELAY_MS=$(UI_TEST_DELAY_MS) HORO_UI_TEST_OUTPUT_DIR="$(UI_TEST_OUTPUT_DIR)" $(RUN_UI_WINDOWED)

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
	@echo "[coverage] collecting full CTest suite ..."
	@$(OPENCOV) $(COV_FLAGS) \
	    --cover_children \
	    --export_type binary:"$(COV_DIR)/ctest.cov" \
	    -- ctest --test-dir "build/$(PRESET_DBG)" -C Debug --output-on-failure
	@echo "[coverage] collecting editor UI scenarios ..."
	-@$(OPENCOV) $(COV_FLAGS) \
	    --input_coverage "$(COV_DIR)/ctest.cov" \
	    --cover_children \
	    --export_type html:"$(COV_DIR)/html" \
	    --export_type cobertura:"$(COV_DIR)/cobertura.xml" \
	    -- $(CMAKE_E) env HORO_LOG_LEVEL=$(TEST_LOG_LEVEL) HORO_UI_TEST_CAPTURE=0 HORO_UI_TEST_DELAY_MS=0 HORO_UI_TEST_OUTPUT_DIR="$(UI_TEST_OUTPUT_DIR)" HORO_UI_TEST_FILTER='$(COVERAGE_UI_FILTER)' $(RUN_UI_WINDOWED)
	@echo ""
	@echo "Coverage report: $(COV_REPORT)"

coverage-source-summary:
	@echo "coverage-source-summary is not supported on Windows/MSVC yet" >&2
	@exit 1

else

## Generate HTML coverage report using lcov (Linux / macOS)
## Requires: apt install lcov  |  brew install lcov
$(SENTINEL_COV):
	cmake --preset $(PRESET_COV)

coverage: $(SENTINEL_COV)
	cmake --build build/$(PRESET_COV)
	@$(MKDIR_P) "$(COV_DIR)"
	ctest --test-dir build/$(PRESET_COV) --output-on-failure
	@echo "[coverage] running UI test scenarios ..."
	HORO_UI_TEST_FILTER='$(COVERAGE_UI_FILTER)' build/$(PRESET_COV)/bin/HoroEditorUiTest --run-ui-tests
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
	        --title "HoroEngine Coverage"
	@echo ""
	@echo "Coverage report: $(COV_REPORT)"

coverage-source-summary: $(SENTINEL_COV)
	cmake --build build/$(PRESET_COV)
	@$(MKDIR_P) "$(COV_DIR)"
	@rm -f "$(COV_DIR)/raw.info" "$(COV_DIR)/raw.info.tmp" "$(COV_DIR)/filtered.info" "$(COV_DIR)/filtered.info.tmp"; \
	set +e; \
	lcov --zerocounters \
	     --directory build/$(PRESET_COV) \
	     --rc lcov_branch_coverage=1 \
	     --ignore-errors inconsistent,source,format,unused,count,deprecated,unsupported; \
	zero_rc=$$?; \
	ctest --test-dir build/$(PRESET_COV) --output-on-failure; \
	test_rc=$$?; \
	if [ $$test_rc -ne 0 ]; then exit $$test_rc; fi; \
	HORO_UI_TEST_FILTER='$(COVERAGE_UI_FILTER)' build/$(PRESET_COV)/bin/HoroEditorUiTest --run-ui-tests; \
	ui_rc=$$?; \
	if [ $$ui_rc -ne 0 ]; then exit $$ui_rc; fi; \
	lcov --capture \
	     --directory build/$(PRESET_COV) \
	     --output-file "$(COV_DIR)/raw.info.tmp" \
	     --rc lcov_branch_coverage=1 \
	     --ignore-errors inconsistent,source,format,unused,count,deprecated,unsupported; \
	raw_rc=$$?; \
	if [ ! -s "$(COV_DIR)/raw.info.tmp" ]; then \
	    echo "[coverage-source-summary] missing raw coverage info: $(COV_DIR)/raw.info.tmp" >&2; \
	    exit 1; \
	fi; \
	mv "$(COV_DIR)/raw.info.tmp" "$(COV_DIR)/raw.info"; \
	lcov --remove "$(COV_DIR)/raw.info" \
	     '*/vendor/*' '*/_deps/*' '*/tests/*' '*/build/*' \
	     '/Applications/Xcode.app/*' \
	     --output-file "$(COV_DIR)/filtered.info.tmp" \
	     --rc lcov_branch_coverage=1 \
	     --ignore-errors inconsistent,source,format,unused,count,deprecated,unsupported; \
	remove_rc=$$?; \
	if [ ! -s "$(COV_DIR)/filtered.info.tmp" ]; then \
	    echo "[coverage-source-summary] missing filtered coverage info: $(COV_DIR)/filtered.info.tmp" >&2; \
	    exit 1; \
	fi; \
	mv "$(COV_DIR)/filtered.info.tmp" "$(COV_DIR)/filtered.info"; \
	if [ $$zero_rc -ne 0 ]; then echo "[coverage-source-summary] warning: lcov --zerocounters returned $$zero_rc" >&2; fi; \
	if [ $$raw_rc -ne 0 ]; then echo "[coverage-source-summary] warning: lcov --capture returned $$raw_rc" >&2; fi; \
	if [ $$remove_rc -ne 0 ]; then echo "[coverage-source-summary] warning: lcov --remove returned $$remove_rc" >&2; fi; \
	python3 scripts/project_source_coverage.py "$(COV_DIR)/filtered.info" --repo "$(CURDIR)" --threshold 80

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
	@echo "  make ui-test      Build & run headless launcher UI tests"
	@echo "  make ui-test-windowed Build & run windowed launcher UI automation"
	@echo "  make coverage     Build, run tests, generate HTML coverage report"
	@echo "  make coverage-source-summary Build, run tests, print source-only coverage summary"
	@echo "  make release      Build release library"
	@echo "  make configure    Run cmake --preset only"
	@echo "  make clean        Remove debug build dir"
	@echo "  make clean-all    Remove all build dirs (incl. coverage)"
	@echo "  make format       Format all sources in-place"
	@echo "  make format-check Check formatting (exits non-zero if changes needed)"
	@echo ""
	@echo "UI automation vars:"
	@echo "  UI_TEST_DELAY_MS=<ms> (default 0)"
	@echo "  UI_TEST_CAPTURE=0|1 (default 0)"
	@echo "  UI_TEST_OUTPUT_DIR=<path> (default ./ui_test_output)"
	@echo "  TEST_LOG_LEVEL=debug|info|warn|error (default debug)"
	@echo ""
	@echo "CI / coverage:"
	@echo "  Windows: make coverage  (requires OpenCppCoverage — winget install OpenCppCoverage.OpenCppCoverage)"
	@echo "  Linux:   make coverage  (requires lcov — apt install lcov)"
	@echo "  GitHub:  see .github/workflows/ci.yml"
	@echo ""
