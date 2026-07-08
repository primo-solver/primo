.PHONY: build test smoke compare fuzz bench docs clean

BUILD_DIR ?= build
PRIMO ?= $(BUILD_DIR)/primo

build:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

smoke: build
	python3 scripts/run_smtlib.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --timeout 5

compare: build
	python3 scripts/compare_with_z3.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --refs z3 cvc5 --timeout 5 --out results/compare.csv

fuzz: build
	python3 scripts/fuzz_external.py --fuzzer yinyang --seeds benchmarks/seeds/qf_uf --solver ./$(PRIMO) --refs z3 cvc5 --logic QF_UF --count 1 --timeout 5 --out fuzz-results/qf_uf-yinyang

bench: build
	python3 scripts/bench.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --timeout 5 --out results/bench.csv

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

clean:
	rm -rf $(BUILD_DIR) results fuzz-results docs/_build
