SHELL := /bin/bash

BUILD_DIR ?= build
BUILD_TYPE ?= Debug
# 离线构建：make build GTEST_SRC=/path/to/googletest-src
# 装了 libgtest-dev 就不需要它（CMakeLists 会优先用系统包）
GTEST_SRC ?=
GTEST_FLAG := $(if $(GTEST_SRC),-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$(GTEST_SRC))

.DEFAULT_GOAL := help

help:
	@echo "VisionReactor-CPP"
	@echo "  make build       配置并编译（Debug）"
	@echo "  make release     配置并编译（Release，跑基准与端到端用它）"
	@echo "  make asan        配置并编译 ASan+UBSan 构建（build-asan）"
	@echo "  make test        编译并运行全部单元测试"
	@echo "  make test-asan   跑 ASan 构建的单元测试"
	@echo "  make bench       运行 Buffer/ThreadPool 基准"
	@echo "  make clean       删除构建目录"
	@echo ""
	@echo "离线（无 GitHub 网络）两种做法："
	@echo "  sudo apt install libgtest-dev          # 之后 make build 直接可用"
	@echo "  make build GTEST_SRC=/path/to/googletest-src"

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(GTEST_FLAG)

build: configure
	cmake --build $(BUILD_DIR) --parallel

# Release：基准与端到端测量用这个构建
release:
	cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release $(GTEST_FLAG)
	cmake --build build-release --parallel

# ASan + UBSan：查越界、use-after-free、未定义行为
asan:
	cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
	  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" $(GTEST_FLAG)
	cmake --build build-asan --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

test-asan: asan
	ctest --test-dir build-asan --output-on-failure

bench: release
	./build-release/Buffer_bench
	./build-release/ThreadPool_bench

clean:
	cmake -E remove_directory $(BUILD_DIR)

.PHONY: help configure build release asan test test-asan bench clean
