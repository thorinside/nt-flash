# NT Flash Tool - Cross-platform Makefile
# Unified firmware flashing tool for disting NT

# Detect platform
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

# Tool configuration
TARGET := nt-flash

# Source directories
BLFWK_DIR := lib/nxp_blhost_sdphost
BLFWK_SRC := $(BLFWK_DIR)/src/blfwk/src
BLFWK_INC := $(BLFWK_DIR)/src/blfwk
INC_DIR := $(BLFWK_DIR)/src/include
CRC_SRC := $(BLFWK_DIR)/src/crc/src

# Our source files
SRC_DIR := src
SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(SRCS:.cpp=.o)

# Embedded libraries
LIB_MINIZ := lib/miniz/miniz.o lib/miniz/miniz_tdef.o lib/miniz/miniz_tinfl.o lib/miniz/miniz_zip.o
LIB_CJSON := lib/cJSON/cJSON.o

# BLFWK core objects (from nxp_blhost_sdphost)
BLFWK_OBJS := \
	$(BLFWK_SRC)/Bootloader.o \
	$(BLFWK_SRC)/BusPal.o \
	$(BLFWK_SRC)/BusPalPeripheral.o \
	$(BLFWK_SRC)/Command.o \
	$(BLFWK_SRC)/DataSource.o \
	$(BLFWK_SRC)/DataTarget.o \
	$(BLFWK_SRC)/ELFSourceFile.o \
	$(BLFWK_SRC)/GHSSecInfo.o \
	$(BLFWK_SRC)/IntelHexSourceFile.o \
	$(BLFWK_SRC)/Logging.o \
	$(BLFWK_SRC)/SBSourceFile.o \
	$(BLFWK_SRC)/SDPCommand.o \
	$(BLFWK_SRC)/SDPUsbHidPacketizer.o \
	$(BLFWK_SRC)/SRecordSourceFile.o \
	$(BLFWK_SRC)/SearchPath.o \
	$(BLFWK_SRC)/SerialPacketizer.o \
	$(BLFWK_SRC)/SourceFile.o \
	$(BLFWK_SRC)/StELFFile.o \
	$(BLFWK_SRC)/StExecutableImage.o \
	$(BLFWK_SRC)/StIntelHexFile.o \
	$(BLFWK_SRC)/StSRecordFile.o \
	$(BLFWK_SRC)/UartPeripheral.o \
	$(BLFWK_SRC)/UsbHidPacketizer.o \
	$(BLFWK_SRC)/UsbHidPeripheral.o \
	$(BLFWK_SRC)/Value.o \
	$(BLFWK_SRC)/format_string.o \
	$(BLFWK_SRC)/jsoncpp.o \
	$(BLFWK_SRC)/options.o \
	$(BLFWK_SRC)/serial.o \
	$(BLFWK_SRC)/utils.o \
	$(CRC_SRC)/crc16.o

# Platform-specific configuration
ifeq ($(UNAME_S),Linux)
    CXX := g++
    CC := gcc
    PLATFORM_DEFS := -DLINUX -DBOOTLOADER_HOST
    PLATFORM_LIBS := -ludev -lpthread
    BLFWK_OBJS += $(BLFWK_SRC)/hid-linux.o
    TARGET_EXT :=
endif

ifeq ($(UNAME_S),Darwin)
    CXX := clang++
    CC := clang
    PLATFORM_DEFS := -DMACOSX -DBOOTLOADER_HOST
    PLATFORM_LIBS := -framework IOKit -framework CoreFoundation -lpthread
    BLFWK_OBJS += $(BLFWK_SRC)/hid-mac.o
    TARGET_EXT :=
endif

# Windows (MinGW)
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
    CXX := g++
    CC := gcc
    PLATFORM_DEFS := -DWIN32 -DBOOTLOADER_HOST -D_WIN32_WINNT=0x0601
    PLATFORM_LIBS := -lsetupapi -lhid -lws2_32
    BLFWK_OBJS += $(BLFWK_SRC)/hid-windows.o
    TARGET_EXT := .exe
endif
ifeq ($(findstring MSYS,$(UNAME_S)),MSYS)
    CXX := g++
    CC := gcc
    PLATFORM_DEFS := -DWIN32 -DBOOTLOADER_HOST -D_WIN32_WINNT=0x0601
    PLATFORM_LIBS := -lsetupapi -lhid -lws2_32
    BLFWK_OBJS += $(BLFWK_SRC)/hid-windows.o
    TARGET_EXT := .exe
endif

# Compiler flags
# Note: -I$(BLFWK_DIR)/src allows includes like "blfwk/Logging.h"
CXXFLAGS := -std=c++11 -O2 -Wall $(PLATFORM_DEFS) \
	-I$(BLFWK_DIR)/src -I$(BLFWK_INC) -I$(INC_DIR) -Ilib/miniz -Ilib/cJSON -Iinclude

CFLAGS := -O2 -Wall $(PLATFORM_DEFS) \
	-I$(BLFWK_DIR)/src -I$(BLFWK_INC) -Ilib/miniz -Ilib/cJSON

# Build targets
.PHONY: all clean tools

all: $(TARGET)$(TARGET_EXT)

# Build the unified tool
$(TARGET)$(TARGET_EXT): $(OBJS) $(BLFWK_OBJS) $(LIB_MINIZ) $(LIB_CJSON)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(PLATFORM_LIBS)

# Build individual blhost/sdphost tools (for testing)
tools: blhost sdphost

blhost: $(BLFWK_OBJS) $(BLFWK_DIR)/proj/blhost/src/blhost.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(PLATFORM_LIBS)

sdphost: $(BLFWK_OBJS) $(BLFWK_DIR)/sdphost.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(PLATFORM_LIBS)

# Compile rules
$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BLFWK_SRC)/%.o: $(BLFWK_SRC)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BLFWK_SRC)/%.o: $(BLFWK_SRC)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(CRC_SRC)/%.o: $(CRC_SRC)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BLFWK_DIR)/sdphost.o: $(BLFWK_DIR)/sdphost.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BLFWK_DIR)/proj/blhost/src/blhost.o: $(BLFWK_DIR)/proj/blhost/src/blhost.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

lib/miniz/%.o: lib/miniz/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

lib/cJSON/cJSON.o: lib/cJSON/cJSON.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(TARGET).exe blhost sdphost blhost.exe sdphost.exe
	rm -f $(OBJS) $(BLFWK_OBJS) $(LIB_MINIZ) $(LIB_CJSON)
	rm -f $(BLFWK_DIR)/sdphost.o $(BLFWK_DIR)/proj/blhost/src/blhost.o

# Print detected platform (for debugging)
info:
	@echo "Platform: $(UNAME_S)"
	@echo "CXX: $(CXX)"
	@echo "Target: $(TARGET)$(TARGET_EXT)"
	@echo "Platform libs: $(PLATFORM_LIBS)"
