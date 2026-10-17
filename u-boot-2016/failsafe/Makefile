# SPDX-License-Identifier: GPL-2.0

ifneq ($(CONFIG_SPL_BUILD),y)

obj-y += modules/
obj-y += fs.o failsafe.o

# All H3C NAND board configurations embed the same public web assets.
ifeq ($(CONFIG_H3C_HW_D_NOR_RESCUE),y)
FS_SELECTED_FILES := nor_rescue.html nor_rescue.js
endif

FS_GEN_SCRIPT := makefsdata
FSDATA_C := $(obj)/httpd-fsdata.c

quiet_cmd_gen_fsdata = GEN     $@
cmd_gen_fsdata = \
	perl $(srctree)/failsafe/$(FS_GEN_SCRIPT) "$(abspath $@)" "$(srctree)/failsafe/fsdata" $(FS_SELECTED_FILES)
ccflags-y += -I$(obj)


$(FSDATA_C): $(srctree)/failsafe/$(FS_GEN_SCRIPT) $(wildcard $(srctree)/failsafe/fsdata/*)
	$(call cmd_gen_fsdata)

$(obj)/fs.o: $(FSDATA_C)

clean-files += httpd-fsdata.c

endif
