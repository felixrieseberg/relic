# devkitPPC project Makefile. Invoked from build_relic.sh inside the Docker
# image. The outer build/wii/Makefile never runs this directly. Variables
# match devkitPro's wii_rules conventions so the include at the bottom does
# the heavy lifting (CFLAGS, MACHDEP, link script, elf2dol).

ifeq ($(strip $(DEVKITPPC)),)
$(error "DEVKITPPC not set")
endif
include $(DEVKITPPC)/wii_rules

TARGET   := relic
BUILD    := build
SOURCES  :=
DATA     :=
INCLUDES :=

# SRC_ROOT / THIRD_PARTY / BEARSSL / LUA / BUILD_UNIX_DAYS are passed in by
# the build script so paths stay out of this Makefile.
CFILES := \
  $(SRC_ROOT)/main.c \
  $(SRC_ROOT)/core/util.c \
  $(SRC_ROOT)/core/jsonp.c \
  $(SRC_ROOT)/core/http.c \
  $(SRC_ROOT)/core/json_write.c \
  $(SRC_ROOT)/core/anth.c \
  $(SRC_ROOT)/core/conv.c \
  $(SRC_ROOT)/core/netcfg.c \
  $(SRC_ROOT)/core/ui.c \
  $(SRC_ROOT)/core/tools.c \
  $(SRC_ROOT)/core/scroll.c \
  $(SRC_ROOT)/core/slash.c \
  $(SRC_ROOT)/core/sess.c \
  $(SRC_ROOT)/core/xport.c \
  $(SRC_ROOT)/core/agent.c \
  $(SRC_ROOT)/core/tls_client.c \
  $(SRC_ROOT)/plat/plat_cfg.c \
  $(SRC_ROOT)/plat/wii/plat_wii.c \
  $(SRC_ROOT)/plat/wii/wii_kbd.c \
  $(SRC_ROOT)/plat/wii/wii_lua.c \
  $(SRC_ROOT)/plat/lua_shell.c \
  $(SRC_ROOT)/compat/nosys_bearssl.c

OFILES := $(addprefix $(BUILD)/,$(notdir $(CFILES:.c=.o)))
VPATH  := $(sort $(dir $(CFILES)))

CFLAGS  = -g -O2 -Wall -Wno-format-truncation $(MACHDEP) \
  -I$(SRC_ROOT) -I$(BEARSSL_INC) -I$(LUA_INC) -I$(THIRD_PARTY) \
  -I$(LIBOGC_INC) -I$(DEVKITPRO)/portlibs/wii/include \
  -I$(DEVKITPRO)/portlibs/ppc/include \
  -DBUILD_UNIX_DAYS=$(BUILD_UNIX_DAYS) \
  -include $(SRC_ROOT)/compat/wii_compat.h
LDFLAGS = -g $(MACHDEP) -Wl,-Map,$(TARGET).map

LIBS    := $(BEARSSL) $(LUA) -lfat -logc -lm
LIBDIRS := -L$(LIBOGC_LIB) -L$(DEVKITPRO)/portlibs/wii/lib \
           -L$(DEVKITPRO)/portlibs/ppc/lib

.PHONY: all clean
all: $(TARGET).dol

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) $(LIBDIRS) $(LIBS) -o $@

# wii_rules supplies the %.dol: %.elf -> elf2dol pattern.

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).dol $(TARGET).map
