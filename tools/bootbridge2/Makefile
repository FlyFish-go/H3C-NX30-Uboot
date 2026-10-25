# BootBridge v2. `make test` runs host tests (needs BB_FIXTURES, BB_RULES and
# BB_PACK, see tests/*.c); `make arm` builds the device binaries.

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror
ARM_CC ?= arm-openwrt-linux-gcc
ARM_CFLAGS ?= -Os -static

RETURN_SRC := runtime/flash.c runtime/bootconfig.c runtime/ubi_check.c runtime/oem_return.c
FACTS_SRC := runtime/facts.c runtime/rules.c
INSTALL_SRC := runtime/install.c runtime/pack.c runtime/sha256.c runtime/backup.c third_party/lzma/LzmaDec.c
LINUX_SRC := runtime/flash_mtd.c runtime/facts_linux.c
CORE := $(RETURN_SRC) $(FACTS_SRC) $(INSTALL_SRC)
HDRS := $(wildcard runtime/*.h tests/*.h third_party/lzma/*.h)
OUT := out
SAN := -fsanitize=address,undefined

.PHONY: all test arm clean

all: test

$(OUT)/test_oem_return: $(RETURN_SRC) tests/flash_mem.c tests/test_oem_return.c $(HDRS)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(filter %.c,$^)

$(OUT)/test_facts_rules: $(FACTS_SRC) tests/test_facts_rules.c $(HDRS)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(filter %.c,$^)

$(OUT)/test_install: $(CORE) tests/flash_mem.c tests/test_install.c $(HDRS)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(filter %.c,$^)

test: $(OUT)/test_oem_return $(OUT)/test_facts_rules $(OUT)/test_install
	$(OUT)/test_oem_return
	$(OUT)/test_facts_rules
	$(OUT)/test_install

$(OUT)/arm/%: runtime/%_main.c $(CORE) $(LINUX_SRC) $(HDRS)
	@mkdir -p $(OUT)/arm
	$(ARM_CC) $(ARM_CFLAGS) -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -o $@ \
		$< $(CORE) $(LINUX_SRC)

$(OUT)/arm/bb_init: runtime/boot/bb_init.c
	@mkdir -p $(OUT)/arm
	$(ARM_CC) $(ARM_CFLAGS) -std=c99 -Wall -Wextra -Werror -o $@ $<

$(OUT)/arm/bb_dhcp: runtime/boot/bb_dhcp.c
	@mkdir -p $(OUT)/arm
	$(ARM_CC) $(ARM_CFLAGS) -std=c99 -Wall -Wextra -Werror -o $@ $<

$(OUT)/arm/bb_keyd: runtime/bb_keyd_main.c
	@mkdir -p $(OUT)/arm
	$(ARM_CC) $(ARM_CFLAGS) -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -o $@ $<

arm: $(OUT)/arm/bb_oem_return $(OUT)/arm/bb_facts $(OUT)/arm/bbd $(OUT)/arm/bb_init $(OUT)/arm/bb_dhcp $(OUT)/arm/bb_keyd

clean:
	rm -rf $(OUT)
