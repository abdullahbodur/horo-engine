PYTHON ?= python3

TARGETS := configure build test ui-test ui-test-windowed release coverage coverage-source-summary clean clean-all format format-check help

.PHONY: all $(TARGETS)

all:
	$(PYTHON) scripts/dev.py build

$(TARGETS):
	$(PYTHON) scripts/dev.py $(if $(filter help,$@),,$@)
