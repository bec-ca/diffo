MELLOW ?= mellow
PROFILE ?= dev

.PHONY: build fetch

build:
	$(MELLOW) fetch
	$(MELLOW) config
	$(MELLOW) build --profile $(PROFILE)
