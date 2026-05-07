# Orizon OS Root Makefile
#
# This repository is intentionally centered on the active x86_64 core in
# `orizon-os-x86_64/`.

SUBDIR := orizon-os-x86_64

.PHONY: all build run run-bios clean distclean help

all: build

build:
	$(MAKE) -C $(SUBDIR) all

run:
	$(MAKE) -C $(SUBDIR) run

run-bios:
	$(MAKE) -C $(SUBDIR) run-bios

clean:
	$(MAKE) -C $(SUBDIR) clean

distclean:
	$(MAKE) -C $(SUBDIR) distclean

help:
	@echo "Orizon OS root commands"
	@echo ""
	@echo "  make          - Build the active x86_64 ISO"
	@echo "  make run      - Run the ISO in QEMU (UEFI)"
	@echo "  make run-bios - Run the ISO in QEMU (legacy BIOS)"
	@echo "  make clean    - Remove build artifacts"
	@echo ""
	@echo "Remote VM loop:"
	@echo "  python scripts/orizon/build_x86_64_on_zimaos.py --deploy-vm"
