# === 1. 路径定义 ===
TOOLCHAIN=$(HOME)/buildroot
CROSS_COMPILE=$(TOOLCHAIN)/bin/arm-buildroot-linux-uclibcgnueabi-
SYSROOT=$(TOOLCHAIN)/arm-buildroot-linux-uclibcgnueabi/sysroot
ZTE_LIB=$(TOOLCHAIN)/ztelib

# === 2. 工具链定义 ===
CC=$(CROSS_COMPILE)gcc
STRIP=$(CROSS_COMPILE)strip

# === 3. 编译参数 (CFLAGS) ===
CFLAGS  = -Wextra \
          -Os -mcpu=cortex-a53 -mtune=cortex-a53 \
          -mfloat-abi=soft -mthumb \
          -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer \
          -fno-ident -fno-stack-protector \
          -ffunction-sections -fdata-sections -fvisibility=hidden \
          --sysroot=$(SYSROOT)

# === 4. 链接参数 (LDFLAGS & LIBS) ===
LDFLAGS = -L$(ZTE_LIB) \
          -Wl,-O2 \
          -Wl,--gc-sections \
          -Wl,--allow-shlib-undefined \
          -Wl,-dynamic-linker,/lib/ld-uClibc.so.0 \
          -Wl,-z,max-page-size=4096

LIBS    = -Wl,--start-group \
          -latutils -lsoftap -lsoft_timer -lnvram -lpthread -lc -lgcc_s \
          -Wl,--end-group

# === 5. 目标与规则 ===
TARGET = at
SRCS   = at.c

.PHONY: all clean

all: $(TARGET)
	$(STRIP) --strip-all -R .note -R .comment -R .gnu_debuglink -R .gnu_debugdata $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)
