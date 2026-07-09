.PHONY: build test smoke compare fuzz bench docs docs-venv docs-serve lint tidy include-cleaner format check contrib contrib-antlr4 contrib-cli11 contrib-z3 contrib-cvc5 contrib-cadical clean

BUILD_DIR ?= build
PRIMO ?= $(BUILD_DIR)/primo
CONTRIB_PREFIX ?= contrib/local
PYTHON ?= python3
PATH_WITH_CONTRIB := $(CURDIR)/$(CONTRIB_PREFIX)/bin:$(PATH)
CPP_FORMAT_FILES := $$(find src tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) | grep -v '^src/smtlib/antlr/')
CPP_TIDY_FILES := $$(git ls-files | rg '^(src|tests)/.*\.cpp$$' | rg -v '^src/smtlib/antlr/')

build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_PREFIX_PATH=$(CURDIR)/$(CONTRIB_PREFIX)
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

smoke: build
	$(PYTHON) scripts/run_smtlib.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --timeout 5

compare: build
	PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/compare_with_z3.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --refs z3 cvc5 --timeout 5 --out results/compare.csv

fuzz: build
	PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/fuzz_external.py --fuzzer yinyang --seeds benchmarks/seeds/qf_uf --solver ./$(PRIMO) --refs z3 cvc5 --logic QF_UF --count 1 --timeout 5 --out fuzz-results/qf_uf-yinyang

bench: build
	$(PYTHON) scripts/bench.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --timeout 5 --out results/bench.csv

docs-venv:
	$(PYTHON) -m venv .venv-docs
	.venv-docs/bin/python -m pip install --upgrade pip
	.venv-docs/bin/python -m pip install -r docs/requirements.txt

docs:
	bash scripts/build_docs.sh

docs-serve: docs
	$(PYTHON) -m http.server 8000 -d docs/_build/html

lint:
	$(PYTHON) -m py_compile scripts/*.py contrib/*.py
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format --dry-run --Werror $(CPP_FORMAT_FILES); \
	else \
		echo "clang-format is not installed; skipping C++ format check."; \
	fi
	@if command -v ruff >/dev/null 2>&1; then \
		ruff check scripts contrib; \
	else \
		echo "ruff is not installed; skipping Python lint."; \
	fi

tidy: build
	@if command -v clang-tidy >/dev/null 2>&1; then \
		clang-tidy -p $(BUILD_DIR) $(CPP_TIDY_FILES); \
	else \
		echo "clang-tidy is not installed; skipping C++ tidy."; \
	fi

include-cleaner: build
	@if command -v clang-tidy >/dev/null 2>&1; then \
		clang-tidy -p $(BUILD_DIR) -checks='-*,clang-diagnostic-*,misc-include-cleaner' $(CPP_TIDY_FILES); \
	else \
		echo "clang-tidy is not installed; skipping include-cleaner scan."; \
	fi

format:
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format -i $(CPP_FORMAT_FILES); \
	else \
		echo "clang-format is not installed; skipping C++ formatting."; \
	fi
	@if command -v ruff >/dev/null 2>&1; then \
		ruff format scripts contrib; \
	else \
		echo "ruff is not installed; skipping Python formatting."; \
	fi

check: lint test

contrib: contrib-antlr4 contrib-cli11 contrib-z3 contrib-cvc5 contrib-cadical

contrib-antlr4:
	$(PYTHON) contrib/install_antlr4.py --prefix $(CONTRIB_PREFIX)

contrib-cli11:
	$(PYTHON) contrib/install_cli11.py --prefix $(CONTRIB_PREFIX)

contrib-z3:
	$(PYTHON) contrib/install_z3.py --prefix $(CONTRIB_PREFIX)

contrib-cvc5:
	$(PYTHON) contrib/install_cvc5.py --prefix $(CONTRIB_PREFIX)

contrib-cadical:
	$(PYTHON) contrib/install_cadical.py --prefix $(CONTRIB_PREFIX)

clean:
	rm -rf $(BUILD_DIR) results fuzz-results _build docs/_build
