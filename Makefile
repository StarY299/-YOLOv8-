# ============================================================
#  RV1126B Component Recognition — 交叉编译 Makefile
# ============================================================

# 交叉编译器
CC  = aarch64-none-linux-gnu-gcc
CXX = aarch64-none-linux-gnu-g++

TARGET = component_ai

# 源文件
SRCS = main.c capture.c oled.c font.c oled_display.c voice_service.c \
       ai_processor.c rknn_infer.c
OBJS = $(SRCS:.c=.o)

# OpenCV 4.x 交叉编译 (取消注释以启用)
# OPENCV_CFLAGS = $(shell aarch64-none-linux-gnu-pkg-config --cflags opencv4 2>/dev/null)
# OPENCV_LIBS   = $(shell aarch64-none-linux-gnu-pkg-config --libs   opencv4 2>/dev/null)
# OPENCV_DEFS   = -DCV_BRANCH_HAS_OPENCV

OPENCV_CFLAGS =
OPENCV_LIBS   =
OPENCV_DEFS   =

# 编译选项
CFLAGS  = -Wall -O2 -std=gnu11 -pthread $(OPENCV_CFLAGS) $(OPENCV_DEFS)
LDFLAGS = -pthread $(OPENCV_LIBS) -lm

LINKER = $(if $(OPENCV_LIBS),$(CXX),$(CC))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LINKER) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# C++ 文件特殊处理
ai_processor.o: ai_processor.c ai_processor.h rknn_infer.h
	$(CXX) $(CFLAGS) -c $< -o $@

rknn_infer.o: rknn_infer.c rknn_infer.h rknn_api.h
	$(CXX) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
