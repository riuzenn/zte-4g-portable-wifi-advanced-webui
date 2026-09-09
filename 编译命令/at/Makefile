# === 1. 路径定义 ===
TOOLCHAIN=$(HOME)/usr
CROSS_COMPILE=$(TOOLCHAIN)/bin/arm-buildroot-linux-uclibcgnueabi-
SYSROOT=$(TOOLCHAIN)/arm-buildroot-linux-uclibcgnueabi/sysroot
ZTE_LIB=$(HOME)/ztelib

# === 2. 工具链定义 ===
CC=$(CROSS_COMPILE)gcc
STRIP=$(CROSS_COMPILE)strip

# === 3. 编译参数 (CFLAGS) ===
CFLAGS  = -Wextra -Os \
          -mcpu=cortex-a53 -mtune=cortex-a53 \
          -mfloat-abi=soft -mthumb \
          -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer \
          -fno-ident \
          -ffunction-sections -fdata-sections -fvisibility=hidden \
          --sysroot=$(SYSROOT) \
          -fno-pie -fno-stack-protector \
          -falign-functions=2 -falign-loops=2 -falign-jumps=2

# === 4. 链接参数 (LDFLAGS & LIBS) ===
LDFLAGS = -mcpu=cortex-a53 -mtune=cortex-a53 \
          -Wl,--as-needed -Wl,--no-copy-dt-needed-entries \
          -L$(ZTE_LIB) \
          -Wl,-rpath-link=${ZTE_LIB} \
          -Wl,-O2 \
          -Wl,--gc-sections \
          -Wl,-dynamic-linker,/lib/ld-uClibc.so.0 \
          -Wl,-z,max-page-size=4096 \
          -Wl,--no-undefined -Wl,--no-allow-shlib-undefined \
          -Wl,-z,norelro -Wl,-z,lazy \
          -Wl,-t

LIBS    = -Wl,--start-group \
          -lsoftap -lnvram -lsoft_timer \
          -Wl,--end-group

# === 5. 目标与规则 ===
TARGET = at
SRCS   = at.c

.PHONY: all clean strip sstrip detail

all: $(TARGET)
	@echo "Build complete: ./$(TARGET)"
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS) $(LIBS) -Wl,-t

strip:
	$(STRIP) --strip-all -R .note -R .comment -R .gnu_debuglink -R .gnu_debugdata $(TARGET)

sstrip: $(TARGET)
	~/ELFkickers/sstrip/sstrip $(TARGET)

clean:
	rm -f $(TARGET)
	
detail:
	@{ \
	    file at; \
		$(CROSS_COMPILE)readelf -d $(TARGET) | grep --color=auto NEEDED; \
		$(CROSS_COMPILE)readelf -h $(TARGET) | grep --color=auto Type; \
		$(CROSS_COMPILE)readelf -l $(TARGET) | grep --color=auto GNU_RELRO; \
		$(CROSS_COMPILE)readelf -d $(TARGET) | grep --color=auto BIND_NOW; \
		$(CROSS_COMPILE)readelf -s $(TARGET) | grep --color=auto stack_chk; \
		$(CROSS_COMPILE)readelf -l $(TARGET) | grep --color=auto GNU_STACK; \
	}
