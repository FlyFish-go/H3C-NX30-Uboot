/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BRIDGE_FIRST_INSTALL_H
#define BRIDGE_FIRST_INSTALL_H
#include <stddef.h>
#include <stdint.h>
#include "bridge_mtd_observer.h"

#define BFI_D_RECORD "hw-d-r012-5f486f5f-low-ecc4-v1"
#define BFI_D_CANDIDATE_SHA256 "61b10ffdec0af3101657bccd5f4425ca4e8a8e6dc62dd2e8d5f8d08e73bc3595"
#define BFI_D_SBL_SHA256 "c8bc5e1d35a3092dc1292d1b8ba13061082e08992a2cb07c05dbf7561a1244b5"
#define BFI_D_OEM_SHA256 "7f1f7bf0938aae5d6c5816d6816524d5877eefb41e9fbdf4427b24bf34fb2b48"

#define BFI_D_PUBLIC_RECORD "hw-d-r012-public-91e9-low-ecc4-trial-v1"
#define BFI_D_PUBLIC_CANDIDATE_SHA256 "91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362"
#define BFI_D_PUBLIC_MBN_SHA256 "ca3e059c9de2209c793948fbb5f155cc8b5cb2011b5c1ca9a903321157dce713"
#define BFI_D_PUBLIC_LOAD_SHA256 "e524e0f1429323ee3d612284e5ec0d66059996999e0a03dc891334ec66225572"
#define BFI_MTD_OOB_64_MASK  0x01u
#define BFI_MTD_OOB_128_MASK 0x02u
#define BFI_MTD_OOB_256_MASK 0x04u
#define BFI_MTD_OOB_MASK_ALL (BFI_MTD_OOB_64_MASK|BFI_MTD_OOB_128_MASK|BFI_MTD_OOB_256_MASK)

typedef enum {
    BFI_CANDIDATE_NONE = 0,
    BFI_CANDIDATE_NATIVE_QUALIFIED = 1,
    BFI_CANDIDATE_CONTROLLED_TRIAL = 2
} bfi_candidate_id;

/* Explicit software build class. This is selected by the compiled target;
 * it is not inferred from NAND ID, machid, ENV, or a user-supplied runtime
 * board string. The product/source package remains a separate build input. */
typedef enum {
    BFI_BOARD_UNSPECIFIED = 0,
    BFI_BOARD_A = 1,
    BFI_BOARD_B = 2,
    BFI_BOARD_C = 3,
    BFI_BOARD_D = 4
} bfi_board_class;
typedef bfi_candidate_id bfi_d_candidate_id;
#define BFI_D_CANDIDATE_NONE BFI_CANDIDATE_NONE
#define BFI_D_CANDIDATE_CANONICAL_5F BFI_CANDIDATE_NATIVE_QUALIFIED
#define BFI_D_CANDIDATE_PUBLIC_91E9_TRIAL BFI_CANDIDATE_CONTROLLED_TRIAL

/* These records bind the exact padded partition bytes. BSS end is an
 * independently reviewed value associated with that exact digest; it is not
 * claimed to be an ELF header field. The public candidate is trial-eligible,
 * not marked native-qualified. */
typedef struct {
    bfi_candidate_id id; /* qualification class; name/hash uniquely identify record */
    const char *name;
    const char *partition_sha256;
    const char *mbn_sha256;
    const char *load_sha256;
    uint32_t partition_bytes, load_offset, load_bytes, entry, bss_end;
    unsigned payload_ecc_bits;
    unsigned native_qualified;
} bfi_candidate_record;
typedef bfi_candidate_record bfi_d_candidate_record;

/* Immutable public candidate mapping for one explicit compiled board class.
 * Historical sample observations are descriptive only. NULL/zero means no
 * inspected exact value; it must never be treated as a match or grant. */
typedef struct {
    bfi_board_class board_class;
    const char *profile_id;
    const char *sample_scope;
    const char *target;
    const char *defconfig_sha256;
    const char *dts_sha256;
    const char *dt_compatible;
    uint32_t machid;
    const char *nand_model;
    unsigned jedec_bytes_observed, manufacturer, chip_id;
    /* raw_spare is physical NAND geometry; mtd_oob is the controller/Linux
     * exposed OOB after ECC codeword normalization. They may differ. */
    unsigned capacity_mib, page, erase, raw_spare, mtd_oob, observed_linux_ecc;
    const char *historical_sbl_sha256;
    unsigned historical_sbl_valid_bytes;
    const char *historical_sbl_extent_sha256;
    const bfi_candidate_record *const *candidates;
    size_t candidate_count;
    /* Fresh ABI2 builds distinguish payload-load ECC from live Linux/storage
     * ECC. Zero preserves the legacy equality rule. */
    unsigned candidate_load_ecc_bits;
    /* Exact Linux MTD OOB values admitted for this source/profile. Zero keeps
     * the legacy single-value check through mtd_oob. */
    unsigned accepted_mtd_oob_mask;
} bfi_board_record;
typedef bfi_board_record bfi_candidate_profile;

typedef struct {
    const char *profile_id;
    const char *model;
    /* Keep physical NAND spare separate from Linux MTD OOB. For the expected
     * HW-B C8:41 OEM Linux profile both are 128; U-Boot QPIC's 80-byte BCH8
     * OOB is a different runtime path. */
    unsigned manufacturer, chip_id, capacity_mib, raw_spare, mtd_oob, linux_ecc;
    unsigned page, erase;
    unsigned sbl_offset, sbl_partition_bytes;
    const char *sbl_sha256;
    unsigned sbl_bytes, sbl_tail_ff;
    const char *active_oem_sha256;
    unsigned bc_appsbl, bc_current_rootfs, bc_equal_valid;
    unsigned active_offset, target_offset, target_size;
    /* startup_bytes bounds the bootloader/APPSBL area; active/target APPSBL
     * ranges must fit inside it and remain disjoint from system slots. */
    unsigned startup_bytes;
    unsigned bridge_offset, bridge_bytes, oem_offset, oem_selector, oem_bytes;
    unsigned load_ecc_bits, storage_ecc_bits;
    const bfi_candidate_record *const *candidates;
    size_t candidate_count;
    /* Exact Linux MTD OOB set for the bound source profile; zero retains the
     * legacy scalar `mtd_oob` contract. */
    unsigned accepted_mtd_oob_mask;
} bfi_install_profile;

/* Internal facts collected by the bridge, never deserialized from a user file.
 * Linux reports two ID bytes. The third byte in the sample's hardware record
 * is not a live Linux observation. Linux ECC is not the PBL/SBL selector. */
typedef struct {
    char boot_before[80], boot_after[80];
    char model[64], sbl_sha256[65], active_sha256[65];
    unsigned manufacturer, chip_id, capacity_mib, raw_spare, linux_ecc;
    unsigned page, erase, sbl_bytes, sbl_tail_ff;
    unsigned active_offset, target_offset, target_size;
    unsigned bc_appsbl, bc_current_rootfs, bc_equal_valid;
    unsigned bridge_offset, bridge_bytes, oem_offset, oem_selector, oem_bytes;
    unsigned bridge_and_oem_verified, r012_route_verified, route_contract_verified;
    unsigned unsigned_boot_allowed;
    unsigned declared_load_ecc, declared_storage_ecc, env_bootargs_verified;
    struct bmo_appsbl_pair_report pages;
    unsigned observed_mtd_oob;
} bfi_facts;

/* Returns NULL only for this one recorded first-install combination.
 * The external empirical record covers exact-byte default OEM boot and UART
 * recovery. This predicate does not observe a button/serial event; RESET has
 * no hardware result in that record. */
const char *bfi_check_d_first_install(const bfi_facts *, const uint8_t *, size_t);
/* Explicit record API for callers that intentionally admit a listed trial
 * candidate. On success, *selected identifies the exact record checked. */
const char *bfi_check_d_first_install_record(const bfi_facts *, const uint8_t *, size_t,
                                             const bfi_d_candidate_record **selected);
const bfi_d_candidate_record *bfi_d_candidate_select(const uint8_t *, size_t);
/* Generic exact-byte candidate selection. `board_class` must be explicitly
 * selected by the compiled target; there is deliberately no default class. */
const bfi_candidate_profile *bfi_candidate_profile_select(bfi_board_class board_class);
const bfi_candidate_record *bfi_candidate_select_for_profile(
    const bfi_candidate_profile *, const uint8_t *, size_t);
int bfi_candidate_profile_accepts(const bfi_candidate_profile *,
                                  const bfi_candidate_record *);
int bfi_board_profile_accepts_mtd_oob(const bfi_board_record *, unsigned observed_oob);
int bfi_install_profile_accepts_mtd_oob(const bfi_install_profile *, unsigned observed_oob);
/* String-facing form for generated BI_HARDWARE_CLASS build constants only. */
const bfi_candidate_profile *bfi_board_profile(const char *board_class);
const bfi_board_record *bfi_board_candidate_record(const char *board_class);
const bfi_candidate_record *bfi_board_candidate_select(
    const char *board_class, const uint8_t *, size_t);
/* Read-only build-time D descriptor; no runtime profile selection is exposed. */
const bfi_install_profile *bfi_d_install_profile(void);
/* Pure comparison against an immutable, build-selected profile and candidate
 * descriptor. Callers must never choose the profile from runtime user input. */
const char *bfi_match_install_profile(const bfi_install_profile *,
                                     const bfi_candidate_record *, const bfi_facts *);
/* Select one of four immutable exact tuples from already verified BC and
 * physical mappings. APPSBL and system-slot directions are independent;
 * every tuple retains the same exact source/OEM/NAND/candidate contract. */
const bfi_install_profile *bfi_select_install_profile(
    const bfi_install_profile *, size_t count, unsigned bridge_offset,
    unsigned oem_offset, unsigned oem_selector, unsigned current_root_selector,
    unsigned current_appsbl_selector);
int bfi_d_candidate_format(const uint8_t *, size_t);
int bfi_d_env_bootargs(const uint8_t *, size_t);
#endif
