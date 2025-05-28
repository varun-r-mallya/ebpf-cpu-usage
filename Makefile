# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
CLANG ?= clang
CC = gcc
BPFTOOL ?= bpftool
LIBBPF_SRC = $(abspath ./libbpf/src)
LIBBPF_OBJ = $(abspath ./build/libbpf.a)
LIBBPF_INCLUDES = $(abspath ./libbpf/include)

# Architecture selection for vmlinux.h
ARCH ?= $(shell uname -m | sed 's/x86_64/x86/' \
			 | sed 's/arm.*/arm/' \
			 | sed 's/aarch64/arm64/' \
			 | sed 's/ppc64le/powerpc/' \
			 | sed 's/mips.*/mips/' \
			 | sed 's/riscv64/riscv/' \
			 | sed 's/loongarch64/loongarch/')
VMLINUX_DIR := ./vmlinux/vmlinux.h/include/$(ARCH)
VMLINUX := $(VMLINUX_DIR)/vmlinux.h

INCLUDES := -I./build -I$(LIBBPF_INCLUDES) -I$(LIBBPF_INCLUDES)/uapi -I$(VMLINUX_DIR) -I./src -I./build/bpf -I./build/libbpf
CFLAGS := -g -Wall
ALL_LDFLAGS := $(LDFLAGS) $(EXTRA_LDFLAGS)

# Source files
USER_SRCS = src/bootstrap.c
BPF_SRCS = src/bpf/bootstrap.bpf.c

# Object files
USER_OBJS = $(patsubst src/%.c, build/%.o, $(USER_SRCS))
BPF_OBJS = $(patsubst src/bpf/%.bpf.c, build/bpf/%.bpf.o, $(BPF_SRCS))
BPF_SKELS = $(patsubst src/bpf/%.bpf.c, build/bpf/%.skel.h, $(BPF_SRCS))

# Final binary
TARGET = build/bootstrap

.PHONY: all
all: $(VMLINUX) $(TARGET)

# Create build directory
build:
	mkdir -p build/bpf

run:
	sudo ./build/bootstrap

commands:
	make build;
	bear --output build/compile_commands.json -- make

# Generate vmlinux.h if it doesn't exist
$(VMLINUX):
	@echo "Generating vmlinux.h for architecture $(ARCH)..."
	mkdir -p $(VMLINUX_DIR)
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > $@

# Build libbpf
$(LIBBPF_OBJ): | build
	$(MAKE) -C $(LIBBPF_SRC) BUILD_STATIC_ONLY=1 OBJDIR=$(dir $@)libbpf DESTDIR=$(dir $@) INCLUDEDIR= LIBDIR= UAPIDIR= install

# Build BPF code
$(BPF_OBJS): build/bpf/%.bpf.o: src/bpf/%.bpf.c $(LIBBPF_OBJ) $(VMLINUX) | build
	$(CLANG) -O2 -g -target bpf -Wall -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

# Generate BPF skeletons
$(BPF_SKELS): build/bpf/%.skel.h: build/bpf/%.bpf.o | build
	$(BPFTOOL) gen skeleton $< > $@

# Build user-space code
$(USER_OBJS): build/%.o: src/%.c $(wildcard src/*.h) $(BPF_SKELS) | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Link final binary
$(TARGET): $(USER_OBJS) $(LIBBPF_OBJ)
	$(CC) $(CFLAGS) $^ $(ALL_LDFLAGS) -lelf -lz -o $@

.PHONY: clean
clean:
	rm -rf build

.DELETE_ON_ERROR:
.SECONDARY: