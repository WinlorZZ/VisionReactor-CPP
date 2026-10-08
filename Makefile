SHELL := /bin/bash

BUILD_DIR ?= build
BUILD_TYPE ?= Debug

.DEFAULT_GOAL := help

help:
	@echo "VisionReactor-CPP"
	@echo "  make build       配置并编译"
	@echo "  make test        编译并运行全部单元测试"
	@echo "  make bench       运行 Buffer/ThreadPool 基准"
	@echo "  make clean       删除构建目录"

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	cmake --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

bench: build
	$(BUILD_DIR)/Buffer_bench
	$(BUILD_DIR)/ThreadPool_bench

clean:
	cmake -E remove_directory $(BUILD_DIR)

.PHONY: help configure build test bench clean
