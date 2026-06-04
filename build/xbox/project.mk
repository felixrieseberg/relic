# nxdk-style project Makefile. Invoked from build_relic.sh inside the Docker
# image. The outer build/xbox/Makefile never runs this directly. Variables
# match nxdk's project conventions so the include at the bottom does the
# heavy lifting (clang/lld flags, .xbe generation via cxbe, lwIP link).

XBE_TITLE = Relic
NXDK_DIR ?= /opt/nxdk
# Intentionally leave GEN_XISO unset: nxdk's GEN_XISO rule consumes
# bin/default.xbe into the ISO and then rmdir's bin/, leaving nothing to
# copy into /out. We invoke extract-xiso ourselves from build_relic.sh
# after archiving default.xbe.

# lwIP (TLS) + USB (HID keyboard for interactive REPL). NXDK_USB alone only
# brings in the core; HID class support is gated on NXDK_USB_ENABLE_HID.
NXDK_NET = y
NXDK_USB = y
NXDK_USB_ENABLE_HID = y

# SRC_ROOT / THIRD_PARTY / BEARSSL / BUILD_UNIX_DAYS are passed in by the
# build script so paths stay out of this Makefile.
SRCS = \
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
  $(SRC_ROOT)/plat/xbox/plat_xbox.c \
  $(SRC_ROOT)/plat/xbox/xbox_stdio.c \
  $(SRC_ROOT)/plat/xbox/xbox_kbd.c \
  $(SRC_ROOT)/plat/xbox/xbox_lua.c \
  $(SRC_ROOT)/plat/lua_shell.c \
  $(SRC_ROOT)/compat/nosys_bearssl.c

# Additional include and define flags. nxdk's Makefile appends its own
# required flags (target triple, sysroot) after these.
CFLAGS += \
  -I$(SRC_ROOT) \
  -I$(BEARSSL_INC) \
  -I$(LUA_INC) \
  -I$(THIRD_PARTY) \
  -I$(NXDK_DIR)/lib/hal \
  -fsigned-char \
  -Wno-deprecated-declarations \
  -DBUILD_UNIX_DAYS=$(BUILD_UNIX_DAYS)

# Force-include the Xbox BearSSL-config shim for tls_client.c. The BearSSL
# archive itself was already built with this shim in build_bearssl.sh; this
# keeps the tls_client.c TU consistent with those compile-time knobs.
CFLAGS += -include $(SRC_ROOT)/compat/xbox_compat.h

include $(NXDK_DIR)/Makefile

# Link the prebuilt BearSSL archive. nxdk's top-level Makefile links via
# `$(LD) ... -out:main.exe $^`, so everything in main.exe's prerequisites
# gets passed positionally to lld-link. Adding libbearssl.a after the
# include (so our addition layers on top of nxdk's main.exe recipe rather
# than racing its variable evaluation) puts the archive on the final link
# line without us having to replicate nxdk's entire recipe.
main.exe: $(BEARSSL) $(LUA)
