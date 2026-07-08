.PHONY: build test smoke compare fuzz bench docs lint format check contrib contrib-z3 contrib-cvc5 contrib-cadical clean

BUILD_DIR ?= build
PRIMO ?= $(BUILD_DIR)/primo
CONTRIB_PREFIX ?= contrib/local
PYTHON ?= python3
PATH_WITH_CONTRIB := $(CURDIR)/$(CONTRIB_PREFIX)/bin:$(PATH)

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

docs:
	@if command -v jupyter-book >/dev/null 2>&1; then \
		jupyter-book build docs; \
	elif command -v jupyter >/dev/null 2>&1 && jupyter book --help >/dev/null 2>&1; then \
		jupyter book build docs; \
	elif command -v myst >/dev/null 2>&1; then \
		myst build docs --html; \
	else \
		echo "Jupyter Book/MyST is not installed; skipping docs build."; \
	fi

lint:
	$(PYTHON) -m py_compile scripts/*.py contrib/*.py
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format --dry-run --Werror $$(find src -name '*.cpp' -o -name '*.hpp'); \
	else \
		echo "clang-format is not installed; skipping C++ format check."; \
	fi
	@if command -v ruff >/dev/null 2>&1; then \
		ruff check scripts contrib; \
	else \
		echo "ruff is not installed; skipping Python lint."; \
	fi

format:
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format -i $$(find src -name '*.cpp' -o -name '*.hpp'); \
	else \
		echo "clang-format is not installed; skipping C++ formatting."; \
	fi
	@if command -v ruff >/dev/null 2>&1; then \
		ruff format scripts contrib; \
	else \
		echo "ruff is not installed; skipping Python formatting."; \
	fi

check: lint test

contrib: contrib-z3 contrib-cvc5 contrib-cadical

contrib-z3:
	$(PYTHON) contrib/install_z3.py --prefix $(CONTRIB_PREFIX)

contrib-cvc5:
	$(PYTHON) contrib/install_cvc5.py --prefix $(CONTRIB_PREFIX)

contrib-cadical:
	$(PYTHON) contrib/install_cadical.py --prefix $(CONTRIB_PREFIX)

clean:
	rm -rf $(BUILD_DIR) results fuzz-results docs/_build
