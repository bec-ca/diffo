MELLOW ?= mellow
PROFILE ?= dev

.PHONY: build fetch

build:
	$(MELLOW) build --profile $(PROFILE)


fetch:
	$(MELLOW) fetch
