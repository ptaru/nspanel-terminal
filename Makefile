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
