# ============================================================================
# Makefile – queue_benchmark
#
# Targets:
#   make / make demo  → build the standalone demo binary
#   make run          → build + run, writes latency.csv
#   make clean        → remove all build artefacts and CSV output
#   make help         → print this target list
#
# Override variables on the command line, e.g.:
#   make run WRITERS=4 READERS=4 SAMPLES=500000 PIN=1
#   make HEADER=path/to/queue_benchmark.hpp
# ============================================================================

CXX      := g++
CXXFLAGS := -O2 -std=c++20 -Wall -Wextra -pthread

# ── File locations ───────────────────────────────────────────────────────────
# HEADER: path to queue_benchmark.hpp relative to this Makefile.
# Override if your copy lives elsewhere:  make HEADER=path/to/queue_benchmark.hpp
HEADER     := queue_benchmark.hpp
TARGET     := bench
SRC        := bench_demo.cpp   # generated — do not edit by hand
FLAGS_FILE := .bench_flags

# ── Runtime knobs ────────────────────────────────────────────────────────────
# Passed as -D flags and consumed by the #ifndef defaults in queue_benchmark.hpp.
# Changing any value automatically triggers a full recompile.
WRITERS  := 2
READERS  := 2
SAMPLES  := 200000
WARMUP   := 3
PIN      := 0
FIRST_CPU:= 0
CSV      := latency.csv

DEMO_DEFINES := \
    -DQUEUE_BENCHMARK_MAIN \
    -DBENCH_N_WRITERS=$(WRITERS)  \
    -DBENCH_N_READERS=$(READERS)  \
    -DBENCH_SAMPLES=$(SAMPLES)    \
    -DBENCH_WARMUP=$(WARMUP)      \
    -DBENCH_PIN=$(PIN)            \
    -DBENCH_FIRST_CPU=$(FIRST_CPU)\
    -DBENCH_CSV=\"$(CSV)\"

# ============================================================================
# Rules
# ============================================================================

.PHONY: all demo run clean help

all: demo

## demo – compile the standalone benchmark binary
demo: $(TARGET)

$(TARGET): $(SRC) $(FLAGS_FILE)
	$(CXX) $(CXXFLAGS) $(DEMO_DEFINES) $(SRC) -o $@
	@echo "[make] Built: $@"

# Generate a thin .cpp that just #includes the header so:
#   • #pragma once works correctly (no 'pragma once in main file' warning)
#   • make rebuilds whenever the header changes
$(SRC): $(HEADER)
	@printf '#include "%s"\n' $(HEADER) > $@

# Flags sentinel — write the current defines to a file.
# If the content changes (i.e. the user passed different WRITERS/READERS/etc.)
# the sentinel file is updated, which makes $(TARGET) stale and triggers a
# recompile.  If the flags are unchanged the file is not touched and make
# skips the compile step entirely.
$(FLAGS_FILE): FORCE
	@if [ ! -f $@ ] || [ "$$(cat $@)" != "$(DEMO_DEFINES)" ]; then \
		echo "[make] Config changed — recompiling."; \
		printf '%s' "$(DEMO_DEFINES)" > $@; \
	fi

# Dummy target that is always considered out-of-date, used to trigger the
# flags check on every invocation without forcing a recompile of everything.
.PHONY: FORCE
FORCE:

## run – build then execute
run: $(TARGET)
	./$(TARGET)

## clean – remove all generated files
clean:
	rm -f $(TARGET) $(SRC) $(CSV) $(FLAGS_FILE)
	@echo "[make] Clean."

## help – list available targets
help:
	@echo ""
	@echo "  Targets:"
	@echo "    all / demo   build the benchmark binary (default)"
	@echo "    run          build and run; produces $(CSV)"
	@echo "    clean        remove $(TARGET), $(SRC), $(CSV), $(FLAGS_FILE)"
	@echo ""
	@echo "  Variables (override on the command line):"
	@echo "    CXX          compiler          [$(CXX)]"
	@echo "    HEADER       path to header    [$(HEADER)]"
	@echo "    WRITERS      producer threads  [$(WRITERS)]"
	@echo "    READERS      consumer threads  [$(READERS)]"
	@echo "    SAMPLES      samples/thread    [$(SAMPLES)]"
	@echo "    WARMUP       warm-up rounds    [$(WARMUP)]"
	@echo "    PIN          pin to CPUs 0/1   [$(PIN)]"
	@echo "    FIRST_CPU    first CPU index   [$(FIRST_CPU)]"
	@echo "    CSV          output CSV path   [$(CSV)]"
	@echo ""
	@echo "  Example:"
	@echo "    make run WRITERS=4 READERS=4 SAMPLES=500000 PIN=1"
	@echo ""
