# Builds the tcc-evm-dao kit with TinyCC. See README.md.
TCC ?= tcc
CC ?= cc
CFLAGS = -std=c99 -Wall -Werror -Isrc
CLANG_FLAGS = -std=c99 -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only -Isrc
FRONT = src/arena.c src/diag.c src/lexer.c src/parser.c src/printer.c
SRC = src/main.c $(FRONT) src/check.c src/evm.c src/keccak.c domain/entries.c
TOOL = test/parsetool.c $(FRONT)
HEADERS = $(wildcard src/*.h domain/*.h)

.PHONY: build check check-clang clean

build: build/interestc build/parsetool

build/domain.c: domain/domain.lang gen/embed.c
	mkdir -p build
	$(TCC) -run gen/embed.c domain/domain.lang build/domain.c

build/interestc: $(SRC) $(HEADERS) build/domain.c
	$(TCC) $(CFLAGS) -o build/interestc $(SRC) build/domain.c

build/parsetool: $(TOOL) $(HEADERS) build/domain.c
	$(TCC) $(CFLAGS) -o build/parsetool $(TOOL) build/domain.c

build/evm-boundaries: test/evm-boundaries.c src/evm.c src/keccak.c $(HEADERS)
	mkdir -p build
	$(TCC) $(CFLAGS) -o $@ test/evm-boundaries.c src/evm.c src/keccak.c

build/metadata: test/metadata.c
	mkdir -p build
	$(TCC) $(CFLAGS) -o $@ test/metadata.c

check-clang: build/domain.c
	$(CC) $(CLANG_FLAGS) $(SRC) build/domain.c
	$(CC) $(CLANG_FLAGS) test/parsetool.c test/evmtool.c test/evm-boundaries.c test/metadata.c

# Test domains (test/domains.sh): build/NAME/interestc embeds test/domains/NAME.lang.
DOMAINS = k4 decision-arg review-k10-domain
.SECONDARY: $(DOMAINS:%=build/%/domain.c)

build/%/domain.c: test/domains/%.lang gen/embed.c
	mkdir -p build/$*
	$(TCC) -run gen/embed.c test/domains/$*.lang $@

build/%/interestc: $(SRC) $(HEADERS) build/%/domain.c
	$(TCC) $(CFLAGS) -o $@ $(SRC) build/$*/domain.c

check: build check-clang build/evm-boundaries build/metadata $(DOMAINS:%=build/%/interestc)
	sh test/gate.sh

clean:
	rm -rf build .gatework
