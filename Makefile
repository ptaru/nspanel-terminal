# Display firmware build shortcuts. These targets are offline and never upload.
PYTHON ?= python3
EU_TFT := build/terminal-eu.tft
US_TFT := build/terminal-us.tft

.PHONY: all tft check verify clean

all: check

tft:
	$(PYTHON) tools/build_tft.py --panel eu
	$(PYTHON) tools/build_tft.py --panel us

check:
	$(PYTHON) tools/build_tft.py --panel eu --check
	$(PYTHON) tools/build_tft.py --panel us --check

verify: check
	$(PYTHON) tools/verify_tft.py $(EU_TFT)
	$(PYTHON) tools/verify_tft.py --panel us $(US_TFT)

clean:
	rm -rf build

# Portable sender/receiver integration and sparse-run checks.
.PHONY: test
test:
	mkdir -p build
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/check_burst.cpp -o build/check-burst
	./build/check-burst
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/check_gap_splitter.cpp -o build/check-gap-splitter
	./build/check-gap-splitter
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/check_dirty_scan.cpp -o build/check-dirty-scan
	./build/check-dirty-scan
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/check_encoding.cpp -o build/check-encoding
	./build/check-encoding
