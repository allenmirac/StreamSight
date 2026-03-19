DEUBG = -D_DEBUG

TARGET1 = rtsp_server
TARGET2 = rtsp_pusher
TARGET3 = rtsp_h264_file
TARGET4 = rtsp_analysis_server

OBJS_PATH = objs

CROSS_COMPILE =
CXX   = $(CROSS_COMPILE)g++
CC    = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

INC  = -I$(shell pwd)/src/ \
       -I$(shell pwd)/src/net \
       -I$(shell pwd)/src/xop \
       -I$(shell pwd)/src/ai \
       -I$(shell pwd)/src/3rdpart

# OpenCV flags (queried at build time)
OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4 2>/dev/null || pkg-config --cflags opencv 2>/dev/null)
OPENCV_LIBS   := $(shell pkg-config --libs   opencv4 2>/dev/null || pkg-config --libs   opencv 2>/dev/null)

LIB  = $(OPENCV_LIBS)

LD_FLAGS  = -lrt -pthread -lpthread -ldl -lm $(DEBUG)
CXX_FLAGS = -std=c++11 $(OPENCV_CFLAGS)

O_FLAG = -O2

# ── net + xop (shared by all targets) ────────────────────────────────────────
SRC1  = $(notdir $(wildcard ./src/net/*.cpp))
OBJS1 = $(patsubst %.cpp,$(OBJS_PATH)/%.o,$(SRC1))

SRC2  = $(notdir $(wildcard ./src/xop/*.cpp))
OBJS2 = $(patsubst %.cpp,$(OBJS_PATH)/%.o,$(SRC2))

# ── example objects ───────────────────────────────────────────────────────────
SRC3  = $(notdir $(wildcard ./example/rtsp_server.cpp))
OBJS3 = $(patsubst %.cpp,$(OBJS_PATH)/%.o,$(SRC3))

SRC4  = $(notdir $(wildcard ./example/rtsp_pusher.cpp))
OBJS4 = $(patsubst %.cpp,$(OBJS_PATH)/%.o,$(SRC4))

SRC5  = $(notdir $(wildcard ./example/rtsp_h264_file.cpp))
OBJS5 = $(patsubst %.cpp,$(OBJS_PATH)/%.o,$(SRC5))

# ── AI layer ──────────────────────────────────────────────────────────────────
SRC6  = $(notdir $(wildcard ./src/ai/*.cpp))
OBJS6 = $(patsubst %.cpp,$(OBJS_PATH)/%.o,$(SRC6))

SRC7  = $(notdir $(wildcard ./example/rtsp_analysis_server.cpp))
OBJS7 = $(patsubst %.cpp,$(OBJS_PATH)/%.o,$(SRC7))

# ── Targets ───────────────────────────────────────────────────────────────────
all: BUILD_DIR $(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4)

BUILD_DIR:
	@-mkdir -p $(OBJS_PATH)

$(TARGET1) : BUILD_DIR $(OBJS1) $(OBJS2) $(OBJS3)
	$(CXX) $(OBJS1) $(OBJS2) $(OBJS3) -o $@ $(CFLAGS) $(LD_FLAGS) $(CXX_FLAGS)

$(TARGET2) : BUILD_DIR $(OBJS1) $(OBJS2) $(OBJS4)
	$(CXX) $(OBJS1) $(OBJS2) $(OBJS4) -o $@ $(CFLAGS) $(LD_FLAGS) $(CXX_FLAGS)

$(TARGET3) : BUILD_DIR $(OBJS1) $(OBJS2) $(OBJS5)
	$(CXX) $(OBJS1) $(OBJS2) $(OBJS5) -o $@ $(CFLAGS) $(LD_FLAGS) $(CXX_FLAGS)

$(TARGET4) : BUILD_DIR $(OBJS1) $(OBJS2) $(OBJS6) $(OBJS7)
	$(CXX) $(OBJS1) $(OBJS2) $(OBJS6) $(OBJS7) -o $@ $(CFLAGS) $(LD_FLAGS) $(CXX_FLAGS) $(LIB)

# ── Compile rules ─────────────────────────────────────────────────────────────
$(OBJS_PATH)/%.o : ./example/%.cpp
	$(CXX) -c  $< -o  $@  $(CXX_FLAGS) $(INC)
$(OBJS_PATH)/%.o : ./src/net/%.cpp
	$(CXX) -c  $< -o  $@  $(CXX_FLAGS) $(INC)
$(OBJS_PATH)/%.o : ./src/xop/%.cpp
	$(CXX) -c  $< -o  $@  $(CXX_FLAGS) $(INC)
$(OBJS_PATH)/%.o : ./src/ai/%.cpp
	$(CXX) -c  $< -o  $@  $(CXX_FLAGS) $(INC)

clean:
	-rm -rf $(OBJS_PATH) $(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4)
