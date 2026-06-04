CC      ?= cc
# -std=c89 keeps core/ honest about syntax (no // comments, no mixed decls,
# no VLAs); -D_DEFAULT_SOURCE re-enables snprintf/popen/DT_DIR/sigaction in
# glibc headers, which would otherwise be hidden under strict-ANSI mode.
CFLAGS  ?= -std=c89 -D_DEFAULT_SOURCE -Wall -Wextra -O2 -fsigned-char
CFLAGS  += $(EXTRA_CFLAGS)
ROOT    := $(abspath ../..)
-include $(ROOT)/.env
-include $(ROOT)/.env.local
export
OUT     := $(ROOT)/dist/posix
BEAR    := $(ROOT)/third_party/bearssl
BUILD_DAYS := $(shell expr `date +%s` / 86400)

CORE_SRC := $(ROOT)/src/core/util.c \
            $(ROOT)/src/core/jsonp.c \
            $(ROOT)/src/core/http.c \
            $(ROOT)/src/core/json_write.c \
            $(ROOT)/src/core/anth.c \
            $(ROOT)/src/core/conv.c \
            $(ROOT)/src/core/netcfg.c \
            $(ROOT)/src/core/ui.c \
            $(ROOT)/src/core/tools.c \
            $(ROOT)/src/core/scroll.c \
            $(ROOT)/src/core/slash.c \
            $(ROOT)/src/core/sess.c \
            $(ROOT)/src/core/xport.c \
            $(ROOT)/src/core/agent.c
TLS_SRC  := $(ROOT)/src/core/tls_client.c
PLAT_SRC := $(ROOT)/src/plat/posix/plat_posix.c $(ROOT)/src/plat/plat_cfg.c

.PHONY: all relic relic-mock ptyrun test test-san test-valgrind e2e e2e-mock clean
all: relic

$(BEAR)/build/libbearssl.a:
	$(MAKE) -C $(BEAR)

# BearSSL headers use `inline`, so the linked binary can't be -std=c89.
# Core sources are still proven C89-clean by the `test` target below.
relic: $(BEAR)/build/libbearssl.a $(CORE_SRC) $(TLS_SRC) $(PLAT_SRC) $(ROOT)/src/main.c
	@mkdir -p $(OUT)
	$(CC) -Wall -Wextra -O2 -fsigned-char -Wno-deprecated-declarations -DBUILD_UNIX_DAYS=$(BUILD_DAYS) \
	  -I$(BEAR)/inc -I$(ROOT)/src \
	  $(ROOT)/src/main.c $(CORE_SRC) $(TLS_SRC) $(PLAT_SRC) \
	  $(BEAR)/build/libbearssl.a -o $(OUT)/relic

ptyrun: $(ROOT)/test/e2e/ptyrun.c
	@mkdir -p $(OUT)
	$(CC) -Wall -Wextra -O2 -o $(OUT)/ptyrun $(ROOT)/test/e2e/ptyrun.c

# Full binary linked against a fixture-reading TLS stub instead of BearSSL,
# so e2e scenarios can run offline against canned responses.
relic-mock: $(CORE_SRC) $(PLAT_SRC) $(ROOT)/src/main.c $(ROOT)/test/e2e/tls_mock.c
	@mkdir -p $(OUT)
	$(CC) -Wall -Wextra -O2 -fsigned-char -DBUILD_UNIX_DAYS=$(BUILD_DAYS) \
	  -I$(ROOT)/src \
	  $(ROOT)/src/main.c $(CORE_SRC) $(PLAT_SRC) $(ROOT)/test/e2e/tls_mock.c \
	  -o $(OUT)/relic-mock

e2e: relic ptyrun
	sh $(ROOT)/test/e2e/run.sh $(OUT)/relic $(E2E)

e2e-mock: relic-mock ptyrun
	RELIC_MOCK=1 ANTHROPIC_API_KEY= sh $(ROOT)/test/e2e/run.sh $(OUT)/relic-mock $(E2E)

# ---- unit tests ------------------------------------------------------------
# Each test/test_<name>.c links the core sources it needs (no BearSSL), so the
# whole suite stays -std=c89 clean. Tests are auto-discovered from
# test/test_*.c; the per-test source list defaults to src/core/<name>.c (if it
# exists) and can be overridden with TEST_<name>_SRC below.

C  := $(ROOT)/src/core
TESTS := $(sort $(patsubst $(ROOT)/test/test_%.c,%,$(wildcard $(ROOT)/test/test_*.c)))

TEST_json_write_SRC := $(C)/json_write.c $(C)/jsonp.c $(C)/util.c
TEST_http_SRC       := $(C)/http.c $(C)/util.c
TEST_anth_SRC       := $(C)/anth.c $(C)/json_write.c $(C)/conv.c $(C)/util.c $(C)/jsonp.c
TEST_anth_FLAGS     := -DANTH_TEST_HELPERS
TEST_conv_SRC       := $(C)/conv.c $(C)/json_write.c $(C)/util.c
TEST_netcfg_SRC     := $(C)/netcfg.c $(C)/util.c
TEST_ui_SRC         := $(C)/ui.c $(C)/util.c
TEST_scroll_SRC     := $(C)/scroll.c $(C)/util.c
TEST_tools_SRC      := $(C)/tools.c $(C)/jsonp.c $(C)/json_write.c $(C)/util.c $(PLAT_SRC)
TEST_agent_SRC      := $(C)/agent.c $(C)/xport.c $(C)/http.c $(C)/conv.c $(C)/anth.c $(C)/jsonp.c $(C)/json_write.c $(C)/util.c
TEST_sess_SRC       := $(C)/sess.c $(C)/conv.c $(C)/json_write.c $(C)/util.c $(PLAT_SRC)
TEST_xport_SRC      := $(C)/xport.c $(C)/http.c $(C)/conv.c $(C)/anth.c $(C)/jsonp.c $(C)/json_write.c $(C)/util.c
TEST_plat_SRC       := $(PLAT_SRC) $(C)/util.c

test_src = $(or $(TEST_$(1)_SRC),$(wildcard $(C)/$(1).c))

TEST_BINS := $(TESTS:%=$(OUT)/test_%)

.SECONDEXPANSION:
$(OUT)/test_%: $(ROOT)/test/test_%.c $(ROOT)/test/t.h $$(call test_src,$$*)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(TEST_$*_FLAGS) -I$(ROOT)/src $< $(call test_src,$*) -o $@

RUNNER ?=

test: $(TEST_BINS)
	@cd $(ROOT) && fail=0; \
	if [ -t 1 ]; then G='\033[32m'; R='\033[31;1m'; X='\033[0m'; fi; \
	for t in $(TEST_BINS); do \
	  $(RUNNER) $$t || fail=1; echo; \
	done; \
	if [ $$fail -eq 0 ]; then \
	  printf "$$G== ALL %s SUITES PASSED ==$$X\n" "$(words $(TESTS))"; \
	else \
	  printf "$$R== TESTS FAILED ==$$X\n"; exit 1; \
	fi

# Rebuild + run under ASan/UBSan into a separate output dir.
test-san:
	$(MAKE) -C . test \
	  OUT=$(ROOT)/dist/posix-san \
	  EXTRA_CFLAGS="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"

# Run the existing binaries under valgrind (no rebuild).
test-valgrind: $(TEST_BINS)
	@command -v valgrind >/dev/null || { echo "valgrind not installed"; exit 1; }
	$(MAKE) -C . test RUNNER="valgrind -q --error-exitcode=1 --leak-check=full"

clean:
	rm -rf $(OUT) $(ROOT)/dist/posix-san
