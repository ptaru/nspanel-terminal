# Display firmware build shortcuts. These targets are offline and never upload.
PYTHON ?= python3
TFT := build/nspanel-v92-blank.tft

.PHONY: all tft check verify clean

all: check

tft:
	$(PYTHON) tools/build_tft.py

check:
	$(PYTHON) tools/build_tft.py --check

verify: check
	$(PYTHON) tools/verify_tft.py $(TFT)

clean:
	rm -rf build
