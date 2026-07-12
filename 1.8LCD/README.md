# 1.8寸 TFT 液晶显示屏驱动程序 (RV1126B)

基于正点原子 F103 1.8寸TFT液晶驱动（带字库）移植到 RV1126B Linux 平台。

## 硬件规格

| 项目 | 规格 |
|------|------|
| 驱动芯片 | ST7735S |
| 分辨率 | 128×160 (可旋转) |
| 色彩 | 16位色 RGB565 (65K) |
| 接口 | 4线 SPI (MOSI/SCLK/CS/DC) + BLK |
| 字库 | 外挂 SPI 字库芯片 (GT20L16S1Y 或兼容) |

## 引脚连接

| LCD引脚 | RV1126B GPIO | 功能 |
|---------|-------------|------|
| SCL(SCLK) | SPI CLK | SPI时钟 |
| SDI(MOSI) | SPI MOSI | SPI数据输出 |
| SDO(MISO) | SPI MISO | SPI数据输入（字库芯片） |
| CS | SPI CS0 | 片选（LCD+字库共用） |
| D/C(RS) | GPIO1_A1 (101) | 数据/命令选择 |
| BLK | GPIO1_A2 (102) | 背光控制 |
| RST | GPIO1_A3 (103) | 复位（可选） |

## 文件说明

| 文件 | 描述 |
|------|------|
| `lcd.h` | 主头文件：颜色定义、引脚配置、函数声明 |
| `lcd.c` | 驱动核心：SPI/GPIO初始化、LCD初始化序列、绘图函数、文本显示 |
| `lcdfont.h` | 内置字库数据：ASCII 6x12/8x16/12x24/16x32 + GB2312 12/16/24/32 |
| `zk.h` | 字库芯片接口声明 |
| `zk.c` | 字库芯片驱动：SPI读取、GB2312/ASCII/Arial/TimesNewRoman显示 |
| `main.c` | 演示程序：清屏、文本、汉字、绘图、色彩测试 |
| `Makefile` | 交叉编译构建脚本 |

## 编译方法

### 交叉编译 (RV1126B)

```bash
# 使用 aarch64 工具链
make CROSS_COMPILE=aarch64-linux-gnu-

# 或使用 32位 ARM 工具链 (Buildroot)
make CROSS_COMPILE=arm-linux-gnueabihf-
```

### 本机编译测试 (x86_64)

```bash
make CC=gcc
```

### 推送到设备

```bash
make install REMOTE_IP=192.168.1.100
```

## 运行方法

在 RV1126B 设备上：

```bash
# 确保 SPI 设备已启用
ls /dev/spidev*

# 运行（需要 root 权限访问 SPI 和 GPIO）
sudo ./lcd_demo
```

## 引脚配置修改

如需修改引脚，编辑 [lcd.h](lcd.h):

```c
#define PIN_DC   101   // 改为你的 DC 引脚
#define PIN_BLK  102   // 改为你的背光引脚
#define PIN_RST  103   // 改为你的复位引脚
#define SPI_DEV  "/dev/spidev0.0"  // 改为你的 SPI 设备
```

## 注意事项

1. **SPI 设备**: RV1126B 的设备树需启用 SPI 接口。检查 `/dev/spidev*` 是否存在。
2. **GPIO 权限**: 程序需要访问 GPIO 和 SPI，需 root 权限或配置 udev 规则。
3. **字库芯片**: 如果模块上没有字库芯片（GT20L16S1Y），在 `main.c` 中将 `#if 1` 改为 `#if 0` 来禁用它。
4. **libgpiod**: 需要目标系统上有 libgpiod 库。Buildroot 可启用 `BR2_PACKAGE_LIBGPIOD`。

## 与原STM32版本的区别

| 特性 | STM32F103 原版 | RV1126B 移植版 |
|------|---------------|---------------|
| SPI 方式 | 软件模拟 SPI (GPIO bit-bang) | 硬件 SPI (/dev/spidev) |
| GPIO 控制 | 寄存器直接操作 | libgpiod 库 / sysfs |
| 延时 | SysTick 定时器 | nanosleep/usleep |
| 编译 | Keil MDK | GCC + Makefile |
| OS | 裸机 | Linux userspace |
