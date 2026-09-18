# xrick - native Linux build (SDL3)

CC       ?= cc
SRC_DIR  := xrick/src
INC_DIR  := xrick/include
BUILD_DIR:= build/linux

SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)
VORBIS_CFLAGS := $(shell pkg-config --cflags vorbisfile)
VORBIS_LIBS   := $(shell pkg-config --libs vorbisfile)

CFLAGS  ?= -O2 -g -Wall
CFLAGS  += -MMD -MP -I$(INC_DIR) $(SDL_CFLAGS) $(VORBIS_CFLAGS)
LDLIBS  += $(SDL_LIBS) $(VORBIS_LIBS) -lz -lm

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

TARGET := build/xrick

GLSLC       ?= glslc
SHADER_DIR  := $(SRC_DIR)/shaders
SHADER_BUILD:= build/shaders
SHADER_SRCS := $(wildcard $(SHADER_DIR)/*.vert $(SHADER_DIR)/*.frag)
SHADER_SPVS := $(patsubst $(SHADER_DIR)/%,$(SHADER_BUILD)/%.spv,$(SHADER_SRCS))

# data.zip (built from data/) and the compiled shaders are embedded directly
# into the executable via #embed, so a normal run needs no separate data
# files. They're internal build artifacts, not release assets -- live under
# build/, not the repo root, and aren't needed once the binary is linked.
EMBED_ZIP := build/data.zip

.PHONY: all clean run install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDLIBS)

# data_embedded.c / shaders_embedded.c pull in build artifacts via #embed,
# which needs a C23 compiler; every other file keeps the default (older,
# K&R-tolerant) dialect
$(BUILD_DIR)/data_embedded.o $(BUILD_DIR)/shaders_embedded.o $(BUILD_DIR)/bezels_embedded.o $(BUILD_DIR)/masks_embedded.o: CFLAGS += -std=gnu23
$(BUILD_DIR)/data_embedded.o: $(EMBED_ZIP)
$(BUILD_DIR)/shaders_embedded.o: $(SHADER_SPVS)
$(BUILD_DIR)/bezels_embedded.o: $(wildcard $(SRC_DIR)/bezels/*.png)
$(BUILD_DIR)/masks_embedded.o: $(wildcard $(SRC_DIR)/masks/*.png)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SHADER_BUILD):
	mkdir -p $(SHADER_BUILD)

-include $(DEPS)

$(EMBED_ZIP): $(shell find data -type f) | $(BUILD_DIR)
	rm -f $@
	cd data && zip -qr ../$@ .

$(SHADER_BUILD)/%.vert.spv: $(SHADER_DIR)/%.vert | $(SHADER_BUILD)
	$(GLSLC) -fshader-stage=vert $< -o $@

$(SHADER_BUILD)/%.frag.spv: $(SHADER_DIR)/%.frag | $(SHADER_BUILD)
	$(GLSLC) -fshader-stage=frag $< -o $@

install: $(TARGET)

run: $(TARGET)
	./$(TARGET) -data data

clean:
	rm -rf $(BUILD_DIR) $(SHADER_BUILD) $(EMBED_ZIP) $(TARGET)
