# === 1. 路径定义 ===
BUILD_ROOT = $(shell echo $$HOME)/at_build
COMPILER   = $(shell echo $$HOME)/buildroot
SYSROOT    = $(COMPILER)/arm-buildroot-linux-uclibcgnueabi/sysroot
ZTE_LIB    = $(BUILD_ROOT)/lib

# === 2. 工具链定义 ===
CC      = $(COMPILER)/bin/arm-buildroot-linux-uclibcgnueabi-gcc
STRIP   = $(COMPILER)/bin/arm-buildroot-linux-uclibcgnueabi-strip

# === 3. 编译参数 (CFLAGS) ===
CFLAGS  = -Wextra \
          -O3 -mcpu=cortex-a53 -mtune=cortex-a53 \
          -mfloat-abi=softfp -mfpu=neon-fp-armv8 -mthumb \
          -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer \
          -finline-functions -funroll-loops -ftree-vectorize \
          -falign-functions=16 -falign-loops=16 -falign-jumps=16 \
          -ffunction-sections -fdata-sections -fvisibility=hidden \
          --sysroot=$(SYSROOT)

# === 4. 链接参数 (LDFLAGS & LIBS) ===
LDFLAGS = -L$(ZTE_LIB) \
          -Wl,-O1 -Wl,--gc-sections \
          -Wl,-rpath-link=$(ZTE_LIB) \
          -Wl,--allow-shlib-undefined \
          -Wl,-dynamic-linker,/lib/ld-uClibc.so.0 \
          -Wl,-z,max-page-size=4096 \

LIBS    = -Wl,--start-group \
          -latutils -lsoftap -lsoft_timer -lnvram -lpthread -lc -lgcc_s \
          -Wl,--end-group

# === 5. 目标与规则 ===
TARGET = at
SRCS   = at.c

.PHONY: all clean

all: $(TARGET)
	$(STRIP) --strip-all $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)