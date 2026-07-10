.PHONY: build test asan-build asan-test smoke compare fuzz fuzzsmt bench bench-interesting-qf-uf docs docs-venv docs-serve lint tidy include-cleaner format check contrib contrib-required contrib-antlr4 contrib-cli11 contrib-z3 contrib-cvc5 contrib-cadical contrib-fuzzsmt contrib-ddsmt clean

BUILD_DIR ?= build
ASAN_BUILD_DIR ?= build-asan
PRIMO ?= $(BUILD_DIR)/primo
CONTRIB_PREFIX ?= contrib/local
PYTHON ?= python3
PATH_WITH_CONTRIB := $(CURDIR)/$(CONTRIB_PREFIX)/bin:$(PATH)
CPP_FORMAT_FILES := $$(find src tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) | grep -v '^src/smtlib/antlr/')
CPP_TIDY_FILES := $$(git ls-files | rg '^(src|tests)/.*\.cpp$$' | rg -v '^src/smtlib/antlr/')
CLI11_STAMP := $(CONTRIB_PREFIX)/.cli11.stamp
ANTLR4_STAMP := $(CONTRIB_PREFIX)/.antlr4.stamp
CADICAL_STAMP := $(CONTRIB_PREFIX)/.cadical.stamp

build: contrib-required
	cmake -S . -B $(BUILD_DIR) -DCMAKE_PREFIX_PATH=$(CURDIR)/$(CONTRIB_PREFIX)
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

asan-build: contrib-required
	cmake -S . -B $(ASAN_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$(CURDIR)/$(CONTRIB_PREFIX) -DPRIMO_SANITIZERS=address
	cmake --build $(ASAN_BUILD_DIR)

asan-test: asan-build
	ASAN_OPTIONS=detect_leaks=1:strict_init_order=1 ctest --test-dir $(ASAN_BUILD_DIR) --output-on-failure

smoke: build
	$(PYTHON) scripts/run_smtlib.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --timeout 5

compare: build
	PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/compare_with_z3.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --refs z3 cvc5 --timeout 5 --out results/compare.csv

fuzz: build
	@if PATH="$(PATH_WITH_CONTRIB)" command -v typefuzz >/dev/null 2>&1; then \
		PATH="$(PATH_WITH_CONTRIB)" scripts/yinyang_fuzz.sh \
			--seeds benchmarks/seeds/qf_uf \
			--solver './$(PRIMO) --quiet' \
			--ref 'z3 model_validate=true' \
			--seconds 60 \
			--iterations 5 \
			--solver-timeout 5 \
			--bugs fuzz-results/qf_uf-yinyang/bugs \
			--scratch fuzz-results/qf_uf-yinyang/scratch; \
	else \
		PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/fuzz_external.py --fuzzer yinyang --seeds benchmarks/seeds/qf_uf --solver ./$(PRIMO) --refs z3 cvc5 --logic QF_UF --count 1 --timeout 5 --out fuzz-results/qf_uf-yinyang; \
	fi

bench: build
	$(PYTHON) scripts/bench.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --timeout 5 --out results/bench.csv

bench-interesting-qf-uf: build
	$(PYTHON) scripts/bench_interesting_qf_uf.py --solver ./$(PRIMO) --solver-arg=--quiet --timeout 30 --manifest benchmarks/interesting-qf-uf.txt --out results/interesting-qf-uf.csv

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

fuzzsmt: build contrib-fuzzsmt contrib-z3
	PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/fuzz_external.py --fuzzer fuzzsmt --solver ./$(PRIMO) --refs z3 --logic QF_UF --count 20 --timeout 5 --fuzzer-args '-ms 1 -Ms 2 -mv 1 -Mv 2 -mf 1 -mp 1 -ma 1 -Ma 2' --out fuzz-results/qf_uf-fuzzsmt

contrib: contrib-required contrib-z3 contrib-cvc5 contrib-fuzzsmt contrib-ddsmt

contrib-required: contrib-antlr4 contrib-cli11 contrib-cadical

contrib-antlr4: $(ANTLR4_STAMP)

$(ANTLR4_STAMP): contrib/install_antlr4.py
	$(PYTHON) contrib/install_antlr4.py --prefix $(CONTRIB_PREFIX)
	touch $(ANTLR4_STAMP)

contrib-cli11: $(CLI11_STAMP)

$(CLI11_STAMP): contrib/install_cli11.py
	$(PYTHON) contrib/install_cli11.py --prefix $(CONTRIB_PREFIX)
	touch $(CLI11_STAMP)

contrib-z3:
	$(PYTHON) contrib/install_z3.py --prefix $(CONTRIB_PREFIX)

contrib-cvc5:
	$(PYTHON) contrib/install_cvc5.py --prefix $(CONTRIB_PREFIX)

contrib-cadical: $(CADICAL_STAMP)

$(CADICAL_STAMP): contrib/install_cadical.py
	$(PYTHON) contrib/install_cadical.py --prefix $(CONTRIB_PREFIX)
	touch $(CADICAL_STAMP)

contrib-fuzzsmt:
	$(PYTHON) contrib/install_fuzzsmt.py --prefix $(CONTRIB_PREFIX)

contrib-ddsmt:
	$(PYTHON) contrib/install_ddsmt.py --prefix $(CONTRIB_PREFIX)

clean:
	rm -rf $(BUILD_DIR) results fuzz-results _build docs/_build
