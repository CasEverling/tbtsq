# ─────────────────────────────────────────────────────────────────────────────
#  tbtsq  —  thread-safe queue benchmark
#
#  Targets
#  ───────
#  make              build the benchmark binary  (default)
#  make run          build + run, write CSVs to tests/results/
#  make plot         run the Python plotting script (requires seaborn)
#  make all          build + run + plot
#  make clean        remove build artefacts and results
#  make help         print this message
#
#  Usage examples
#  ──────────────
#  make                        # just compile
#  make run                    # compile and benchmark
#  make all                    # compile, benchmark, plot
#  make CXX=clang++            # use clang instead of g++
# ─────────────────────────────────────────────────────────────────────────────

# ── Toolchain ────────────────────────────────────────────────────────────────

CXX      ?= g++
CXXFLAGS  = -O2 -std=c++20 -Wall -Wextra -Wpedantic
CXXFLAGS += -march=native
CXXFLAGS += -I.
LDFLAGS   = -lpthread -latomic

# ── Directories ──────────────────────────────────────────────────────────────

TESTS_DIR   = tests
RESULTS_DIR = $(TESTS_DIR)/results
PLOTS_DIR   = $(TESTS_DIR)/plots
BUILD_DIR   = build

# ── Sources ──────────────────────────────────────────────────────────────────

QUEUE_SRCS = coarse_mutex.cpp   \
             two_locks.cpp      \
             ms.cpp             \
             hazard_pointer.cpp \
             spsc_ring.cpp      \
             vuykov.cpp

BENCH_SRC  = $(TESTS_DIR)/benchmarking.cpp

QUEUE_OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(QUEUE_SRCS))
BENCH_OBJ  = $(BUILD_DIR)/$(TESTS_DIR)/benchmarking.o
OBJS       = $(QUEUE_OBJS) $(BENCH_OBJ)

TARGET     = $(TESTS_DIR)/bench_all

# ── Headers (rebuild everything if any header changes) ───────────────────────

HDRS = base_queue.hpp          \
       coarse_mutex.hpp        \
       two_locks.hpp           \
       ms.hpp                  \
       hazard_pointer.hpp      \
       spsc_ring.hpp           \
       vuykov.hpp              \
       $(TESTS_DIR)/queue_benchmark.hpp

# ── Phony targets ─────────────────────────────────────────────────────────────

.PHONY: all build run plot clean help

build: $(TARGET)

# ── Link ─────────────────────────────────────────────────────────────────────

$(TARGET): $(OBJS)
	@echo "[LD]  $@"
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "[OK]  binary → $@"

# ── Compile: queue sources ────────────────────────────────────────────────────

$(BUILD_DIR)/%.o: %.cpp $(HDRS)
	@mkdir -p $(dir $@)
	@echo "[CXX] $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ── Compile: test driver ──────────────────────────────────────────────────────

$(BUILD_DIR)/$(TESTS_DIR)/benchmarking.o: $(BENCH_SRC) $(HDRS)
	@mkdir -p $(dir $@)
	@echo "[CXX] $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ── Run benchmark ─────────────────────────────────────────────────────────────

run: $(TARGET)
	@mkdir -p $(RESULTS_DIR)
	@echo "[RUN] bench_all  →  results in $(RESULTS_DIR)/"
	cd $(TESTS_DIR) && ./bench_all

# ── Plot ──────────────────────────────────────────────────────────────────────

plot:
	@mkdir -p $(PLOTS_DIR)
	@echo "[PLOT] plot_latency.py"
	cd $(TESTS_DIR) && python3 plot_latency.py \
		--results results/ \
		--plots   plots/

# ── All ───────────────────────────────────────────────────────────────────────

all: run plot

# ── Clean ─────────────────────────────────────────────────────────────────────

clean:
	@echo "[CLEAN]"
	rm -rf $(BUILD_DIR)
	rm -f  $(TARGET)
	rm -rf $(RESULTS_DIR)
	rm -rf $(PLOTS_DIR)

# ── Help ──────────────────────────────────────────────────────────────────────

help:
	@echo ""
	@echo "  make              compile the benchmark binary"
	@echo "  make run          compile + run  (CSVs → tests/results/)"
	@echo "  make plot         plot results   (PNGs → tests/plots/)"
	@echo "  make all          compile + run + plot"
	@echo "  make clean        remove build/, binary, results/, plots/"
	@echo ""
	@echo "  Options:"
	@echo "    CXX=clang++     use clang instead of g++"
	@echo ""
