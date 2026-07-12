# Makefile for 1.8-inch TFT LCD driver (ST7735S) on RV1126B
#
# Target: Rockchip RV1126B (ARM Cortex-A7, 32-bit or AArch64)
# Build:  make                    - cross-compile for RV1126B
#         make CC=gcc             - compile for host (x86_64) testing
#         make clean              - remove build artifacts
#
# Prerequisites:
#   - Cross toolchain: aarch64-linux-gnu-gcc (or arm-linux-gnueabihf-gcc)
#   - libgpiod (cross-compiled for target): libgpiod-dev
#   - Linux kernel headers with SPI support

# ==================== Toolchain ====================
# RV1126B 使用 ARM Cortex-A7 (32位 ARMv7) 或 AArch64
# 根据你的 SDK 选择:
#   Buildroot RV1126B: 通常使用 arm-linux-gnueabihf-
#   Debian/Ubuntu RV1126B: 通常使用 aarch64-linux-gnu-
#
# 常见配置:
#   CROSS_COMPILE ?= arm-linux-gnueabihf-      # 32位 ARM
#   CROSS_COMPILE ?= aarch64-linux-gnu-         # 64位 ARM (推荐)

CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc

# ==================== Compiler Flags ====================
# 目标架构
# aarch64 (64位 ARMv8): 不需要额外 flags，FPU 是标配
# arm32 (32位 ARMv7): 取消下面注释
# ARCH_FLAGS ?= -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
ARCH_FLAGS ?=

# Sysroot (如果交叉编译工具链需要)
# SYSROOT ?= /path/to/rv1126b/sysroot
# SYSROOT_FLAG = --sysroot=$(SYSROOT)

CFLAGS = -Wall -Wextra -O2 $(ARCH_FLAGS) $(SYSROOT_FLAG)
LDFLAGS = $(SYSROOT_FLAG)

# ==================== Libraries ====================
# libgpiod: GPIO 控制库
# libm:     数学库
# librt:    实时库 (nanosleep)
LDLIBS = -lm -lrt

# ==================== Source Files ====================
SRCS = main.c lcd.c zk.c
OBJS = $(SRCS:.c=.o)
TARGET = lcd_demo

# ==================== Build Rules ====================
.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@echo "Build success: $(TARGET)"
	@file $(TARGET)

%.o: %.c lcd.h lcdfont.h
	$(CC) $(CFLAGS) -c -o $@ $<

# zk.o 额外依赖 zk.h
zk.o: zk.c zk.h lcd.h
	$(CC) $(CFLAGS) -c -o $@ $<

# ==================== Clean ====================
clean:
	rm -f $(OBJS) $(TARGET)

# ==================== Install ====================
# 推送到 RV1126B 设备 (通过 scp/adb)
# 根据实际连接方式修改 IP 和路径
REMOTE_IP ?= 192.168.1.100
REMOTE_PATH ?= /home/root/

install: $(TARGET)
	scp $(TARGET) root@$(REMOTE_IP):$(REMOTE_PATH)
	@echo "Pushed $(TARGET) to $(REMOTE_IP):$(REMOTE_PATH)"

# ==================== Help ====================
help:
	@echo "1.8 TFT LCD Driver for RV1126B - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          Build lcd_demo binary (default)"
	@echo "  clean        Remove build artifacts"
	@echo "  install      Push binary to remote device via scp"
	@echo ""
	@echo "Variables:"
	@echo "  CROSS_COMPILE  Cross-compiler prefix (default: aarch64-linux-gnu-)"
	@echo "  REMOTE_IP      Remote device IP for install target"
	@echo "  REMOTE_PATH    Remote path for install target"
	@echo ""
	@echo "Examples:"
	@echo "  make                                    # Cross-compile for RV1126B"
	@echo "  make CC=gcc                             # Compile for host PC (test)"
	@echo "  make CROSS_COMPILE=arm-linux-gnueabihf- # 32-bit ARM toolchain"
	@echo "  make install REMOTE_IP=192.168.1.100    # Deploy via scp"
