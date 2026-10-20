// SPDX-License-Identifier: GPL-2.0-or-later
#include "bridge_root_binding.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static br_gluebi_evidence valid(void)
{
    br_gluebi_evidence e;
    memset(&e, 0, sizeof(e));
    e.root_major=31; e.root_minor=24; e.event_major=31; e.event_minor=24;
    e.block_mtd_index=24; e.mtd_major=90; e.mtd_minor=48;
    e.mtd_name_matches=1; e.proc_mtd_name_matches=1;
    e.attached_ubi_count=1; e.ubi_id=0; e.ubi_mtd_index=15;
    e.root_volume_name_matches=1; e.volume_ubi_id=0; e.volume_id=3;
    e.volume_alignment=1; e.volume_corrupted=0; e.volume_update_marker=0;
    e.mtd_size=17395712; e.mtd_erase=126976; e.mtd_write=2048;
    e.volume_data_bytes=17395712; e.volume_reserved_ebs=137;
    e.usable_leb_size=126976;
    e.block_devname="mtdblock24"; e.block_devtype="disk";
    e.block_sysfs_path="/sys/devices/virtual/mtd/mtd24/mtdblock24";
    e.mtd_name="ubi_rootfs"; e.mtd_type="ubi";
    e.mount_source="mtd:ubi_rootfs"; e.mount_target="/";
    e.mount_type="squashfs"; e.mount_options="ro,relatime";
    e.cmdline="console=ttyMSM0,115200n8 ubi.mtd=rootfs root=mtd:ubi_rootfs rootfstype=squashfs rootwait";
    e.volume_name="ubi_rootfs"; e.volume_type="dynamic";
    return e;
}

int main(void)
{
    const br_gluebi_contract contract={17395712,126976,137,2048};
    uint32_t idx=0;
    br_gluebi_evidence e=valid();
    assert(br_validate_gluebi_root(&e,&contract,&idx)==0 && idx==15);

    e=valid();e.event_minor=23;assert(br_validate_gluebi_root(&e,&contract,&idx)==BR_GB_ERR_ROOT_DEVICE); /* st_dev/uevent mismatch */
    e=valid();e.block_devname="mtdblock23";assert(br_validate_gluebi_root(&e,&contract,&idx)==BR_GB_ERR_BLOCK_IDENTITY);
    e=valid();e.mtd_name_matches=2;assert(br_validate_gluebi_root(&e,&contract,&idx)==BR_GB_ERR_MTD_IDENTITY); /* duplicate gluebi MTD name */
    e=valid();e.proc_mtd_name_matches=2;assert(br_validate_gluebi_root(&e,&contract,&idx)!=0); /* duplicate proc mapping */
    e=valid();e.volume_ubi_id=1;assert(br_validate_gluebi_root(&e,&contract,&idx)==BR_GB_ERR_UBI_MAPPING); /* rootfs volume must belong to attached UBI */
    e=valid();e.volume_data_bytes--;assert(br_validate_gluebi_root(&e,&contract,&idx)==BR_GB_ERR_VOLUME_GEOMETRY); /* different UBI capacity */
    e=valid();e.volume_reserved_ebs=136;assert(br_validate_gluebi_root(&e,&contract,&idx)!=0); /* different volume geometry */
    e=valid();e.mount_options="rw,relatime";assert(br_validate_gluebi_root(&e,&contract,&idx)==BR_GB_ERR_MOUNT_POLICY); /* writable root */
    e=valid();e.mount_options="ro,rw,relatime";assert(br_validate_gluebi_root(&e,&contract,&idx)!=0); /* contradictory root flags */
    e=valid();e.block_sysfs_path="/sys/devices/virtual/mtd/mtd23/mtdblock23";assert(br_validate_gluebi_root(&e,&contract,&idx)!=0); /* wrong sysfs backing */
    e=valid();e.attached_ubi_count=2;assert(br_validate_gluebi_root(&e,&contract,&idx)!=0); /* ambiguous UBI attach */
    e=valid();e.ubi_mtd_index=16;assert(br_validate_gluebi_root(&e,&contract,&idx)==0 && idx==16); /* caller maps selector/partition */
    e=valid();e.ubi_id=1;assert(br_validate_gluebi_root(&e,&contract,&idx)!=0); /* not observed ubi0 */
    e=valid();e.volume_id=2;assert(br_validate_gluebi_root(&e,&contract,&idx)!=0); /* not volume 3 */
    e=valid();e.volume_type="static";assert(br_validate_gluebi_root(&e,&contract,&idx)!=0);
    e=valid();e.cmdline="ubi.mtd=rootfs ubi.mtd=15 root=mtd:ubi_rootfs rootfstype=squashfs";assert(br_validate_gluebi_root(&e,&contract,&idx)==BR_GB_ERR_CMDLINE_BINDING); /* duplicate/ambiguous cmdline */
    e=valid();e.cmdline="ubi.mtd=rootfs root=/dev/ubiblock0_3 rootfstype=squashfs";assert(br_validate_gluebi_root(&e,&contract,&idx)!=0); /* wrong root binding */
    /* Exact source geometries: RC/RW149, RT153, NX137. Cross-package
     * values remain rejected; generated inputs are not live observations. */
    const unsigned lebs[]={149,149,153,137};
    for(unsigned i=0;i<4;i++){
        br_gluebi_contract source={126976u*lebs[i],126976,lebs[i],2048};
        e=valid();e.mtd_size=e.volume_data_bytes=source.volume_bytes;e.volume_reserved_ebs=source.reserved_ebs;
        assert(br_validate_gluebi_root(&e,&source,&idx)==0);
        if(lebs[i]!=137)assert(br_validate_gluebi_root(&e,&contract,&idx)==BR_GB_ERR_VOLUME_GEOMETRY);
        e.volume_reserved_ebs--;assert(br_validate_gluebi_root(&e,&source,&idx)==BR_GB_ERR_VOLUME_GEOMETRY);
        e=valid();source.usable_leb_bytes=0;assert(br_validate_gluebi_root(&e,&source,&idx)==BR_GB_ERR_ARGUMENT);
        source=(br_gluebi_contract){UINT64_MAX,2,UINT64_MAX,1};
        assert(br_validate_gluebi_root(&e,&source,&idx)==BR_GB_ERR_ARGUMENT);
    }
    puts("GlueBI root identity, uniqueness, volume geometry, read-only mount and cmdline tests PASS");
    return 0;
}
