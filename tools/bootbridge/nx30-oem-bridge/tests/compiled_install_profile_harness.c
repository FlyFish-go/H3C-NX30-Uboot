#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define main bridge_install_daemon_main
#include "../src/bridge_install.c"
#ifdef BFI_PROFILE_RECORD_HEADER
#include BFI_PROFILE_RECORD_HEADER
#endif
#undef main
#include <assert.h>

#if !BI_HAVE_INSTALL_PROFILE || BI_TEST
#error "this harness must compile the production compiled-profile branch"
#endif

static uint8_t *read_exact_file(const char *path, size_t bytes)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data = malloc(bytes);
    assert(f && data);
    assert(fread(data, 1, bytes, f) == bytes);
    assert(fgetc(f) == EOF);
    assert(fclose(f) == 0);
    return data;
}

static void make_bc(uint8_t *b, unsigned app, unsigned root)
{
    static const char *const rows[8] = {
        "0:QSEE", "0:DEVCFG", "0:CDT", "0:APPSBL", "0:HLOS",
        "rootfs", "0:WIFIFW", "0:BTFW"
    };
    memset(b, 0xff, BC_BYTES);
    put_le32(b, MAGIC);
    put_le32(b + 4, 14);
    put_le32(b + 8, 8);
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t *entry = b + 12 + i * 20;
        size_t n = strlen(rows[i]);
        memset(entry, 0, 16);
        memcpy(entry, rows[i], n);
        put_le32(entry + 16, i == 3 ? app : (i == 5 ? root : 0));
    }
    put_le32(b + 332, TAIL);
}

static void init_part(bi_part *part, unsigned off, unsigned size,
                      const bfi_install_profile *profile, uint8_t *snapshot)
{
    memset(part, 0, sizeof(*part));
    part->off = off;
    part->size = size;
    part->page = profile->page;
    part->erase = profile->erase;
    part->oob = profile->mtd_oob;
    part->ecc = profile->storage_ecc_bits;
    part->fd = -1;
    part->snapshot = snapshot;
}

static void expect_reject(bi_part *parts, bi_part *env, bi_nand_identity *id,
                          uint8_t *payload, struct bmo_appsbl_pair_report *pages)
{
    g_first_install_qualified = 0;
    g_first_install_record = NULL;
    g_can_reboot = 0;
    assert(qualify_compiled_first_install(parts, 7, &parts[0], env, id,
                                          payload, pages) != 0);
    assert(g_first_install_qualified == 0);
    assert(g_modified == 0 && g_can_reboot == 0);
}

static void write_text(const char *path, const char *value)
{
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(value, 1, strlen(value), f) == strlen(value));
    assert(fclose(f) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 7);
    const bfi_install_profile *profiles = bootbridge_first_install_profiles;
    const bfi_install_profile *p = &profiles[0];
    assert(BI_FIRST_INSTALL_PROFILE_COUNT == 4u);
    const bfi_board_record *board = bfi_board_candidate_record(BI_HARDWARE_CLASS);
    uint8_t *candidate = read_exact_file(argv[2], APPSBL_BYTES);
    uint8_t *other_candidate = read_exact_file(argv[6], APPSBL_BYTES);
    uint8_t *sbl = read_exact_file(argv[3], p->sbl_partition_bytes);
    uint8_t *active = read_exact_file(argv[4], p->target_size);
    uint8_t *env_bytes = read_exact_file(argv[5], 0x80000u);
    assert(board && board->machid == BI_EXPECTED_MACHID);
#ifdef BFI_PROFILE_RECORD_HEADER
    assert(bfi_candidate_profile_select(BFI_GENERATED_BOARD_CLASS) == board);
    assert(bfi_candidate_profile_select((bfi_board_class)(BFI_GENERATED_BOARD_CLASS == BFI_BOARD_A ?
           BFI_BOARD_B : BFI_BOARD_A)) == NULL);
    assert(board->candidate_count == 1u && board->candidates[0] == &bfi_generated_candidate);
    assert(bfi_candidate_profile_accepts(board, &bfi_generated_candidate));
#endif

    bi_part parts[7];
    init_part(&parts[0], p->sbl_offset, p->sbl_partition_bytes, p, sbl);
    init_part(&parts[1], p->active_offset, p->target_size, p, active);
    init_part(&parts[2], p->target_offset, p->target_size, p, candidate);
    init_part(&parts[3], BC0_OFF, BC_BYTES, p, malloc(BC_BYTES));
    init_part(&parts[4], BC1_OFF, BC_BYTES, p, malloc(BC_BYTES));
    init_part(&parts[5], p->bridge_offset, p->bridge_bytes, p, NULL);
    init_part(&parts[6], p->oem_offset, p->oem_bytes, p, NULL);
    assert(parts[3].snapshot && parts[4].snapshot);
    make_bc(parts[3].snapshot, p->bc_appsbl, p->bc_current_rootfs);
    memcpy(parts[4].snapshot, parts[3].snapshot, BC_BYTES);

    bi_part env;
    init_part(&env, 0, 0x80000u, p, env_bytes);
    bi_nand_identity id;
    memset(&id, 0, sizeof(id));
    id.manufacturer = p->manufacturer;
    id.chip_id = p->chip_id;
    id.capacity_mib = p->capacity_mib;
    id.erase_kib = p->erase / 1024u;
    id.page_bytes = p->page;
    id.raw_oob_bytes = p->raw_spare;
    snprintf(id.model, sizeof(id.model), "%s", p->model);

    struct bmo_appsbl_pair_report pages;
    memset(&pages, 0, sizeof(pages));
    pages.observation_complete = pages.binding_valid = pages.clean = 1;
    for (unsigned i = 0; i < 2; ++i) {
        struct bmo_partition_report *r = &pages.appsbl[i];
        r->observation_complete = r->binding_valid = r->clean = 1;
        r->pages_read = p->target_size / p->page;
        r->covered_bytes = p->target_size;
        r->eraseblocks_checked = p->target_size / p->erase;
        r->observed_oob_bytes = p->mtd_oob;
        r->ecc_stats_state = BMO_ECC_STATS_SUPPORTED;
    }

    snprintf(g_root, sizeof(g_root), "%s", argv[1]);
    snprintf(g_boot_id, sizeof(g_boot_id), "%s", "11111111-1111-4111-8111-111111111111");
    g_common_bound = g_oem_bound = g_oem_verified = g_oem_records_verified = 1;
    g_oem_bridge_off = p->bridge_offset;
    g_oem_offset = p->oem_offset;
    g_oem_selector = p->oem_selector;
    g_oem_pool_match.product = BI_RETURN_SOURCE_PRODUCT;
    g_oem_pool_match.exit_policy = BI_FIRST_INSTALL_OEM_EXIT_POLICY;
    char active_hash[65], candidate_hash[65];
    hash_hex(active, p->target_size, active_hash);
    hash_hex(candidate, APPSBL_BYTES, candidate_hash);
    g_version = evaluate_version(active_hash, candidate_hash);
    if (g_version.decision != BV_FIRST_INSTALL) {
        fprintf(stderr, "compiled trusted release registry rejected the exact OEM/candidate pair: %s\n",
                version_reason(g_version.decision));
        return 12;
    }

    int qualified = qualify_compiled_first_install(parts, 7, &parts[0], &env,
                                                   &id, candidate, &pages);
    if (qualified != 0) {
        fprintf(stderr, "production qualifier rejected synthetic facts: %s\n", g_blocked);
        return 10;
    }
    assert(g_first_install_qualified == 1);
    assert(g_first_install_record && !strcmp(g_first_install_record->name,
                                             BI_FIRST_INSTALL_RECORD));
    assert(g_first_install_profile == &profiles[0]);
    assert(g_modified == 0 && g_can_reboot == 0);

    /* Same exact OEM/SBL/NAND/candidate and HIGH->LOW APPSBL transaction,
     * with the bridge running from physical B and preserved OEM in A. */
    parts[5].off=profiles[1].bridge_offset;
    parts[6].off=profiles[1].oem_offset;
    g_oem_bridge_off=profiles[1].bridge_offset;
    g_oem_offset=profiles[1].oem_offset;
    g_oem_selector=profiles[1].oem_selector;
    make_bc(parts[3].snapshot,profiles[1].bc_appsbl,profiles[1].bc_current_rootfs);
    memcpy(parts[4].snapshot,parts[3].snapshot,BC_BYTES);
    g_version=evaluate_version(active_hash,candidate_hash);
    assert(g_version.decision==BV_FIRST_INSTALL);
    qualified=qualify_compiled_first_install(parts,7,&parts[0],&env,&id,candidate,&pages);
    if(qualified!=0){fprintf(stderr,"reverse physical-slot profile rejected synthetic observations: %s\n",g_blocked);return 13;}
    assert(g_first_install_profile==&profiles[1]);
    assert(g_first_install_qualified==1&&g_modified==0&&g_can_reboot==0);

    /* All four directions must retain every production qualifier gate. */
    for(unsigned direction=0;direction<4;direction++){
        const bfi_install_profile *q=&profiles[direction];
        parts[1].off=q->active_offset;parts[2].off=q->target_offset;
        parts[5].off=q->bridge_offset;parts[6].off=q->oem_offset;
        g_oem_bridge_off=q->bridge_offset;g_oem_offset=q->oem_offset;g_oem_selector=q->oem_selector;
        make_bc(parts[3].snapshot,q->bc_appsbl,q->bc_current_rootfs);
        memcpy(parts[4].snapshot,parts[3].snapshot,BC_BYTES);
        assert(qualify_compiled_first_install(parts,7,&parts[0],&env,&id,candidate,&pages)==0);
        assert(g_first_install_profile==q&&g_first_install_qualified&&g_modified==0);
        parts[1].snapshot[0]^=1;expect_reject(parts,&env,&id,candidate,&pages);parts[1].snapshot[0]^=1;
        parts[0].snapshot[0]^=1;expect_reject(parts,&env,&id,candidate,&pages);parts[0].snapshot[0]^=1;
        pages.appsbl[1].ecc_uncorrected_after=1;expect_reject(parts,&env,&id,candidate,&pages);
        pages.appsbl[1].ecc_uncorrected_after=0;
        parts[4].snapshot[400]^=1;expect_reject(parts,&env,&id,candidate,&pages);parts[4].snapshot[400]^=1;
    }
    parts[1].off=p->active_offset;parts[2].off=p->target_offset;
    parts[5].off=profiles[1].bridge_offset;parts[6].off=profiles[1].oem_offset;
    g_oem_bridge_off=profiles[1].bridge_offset;g_oem_offset=profiles[1].oem_offset;
    g_oem_selector=profiles[1].oem_selector;
    make_bc(parts[3].snapshot,profiles[1].bc_appsbl,profiles[1].bc_current_rootfs);
    memcpy(parts[4].snapshot,parts[3].snapshot,BC_BYTES);

    /* A live selector contradicting the verified running root must reject. */
    g_oem_selector=1u;
    expect_reject(parts,&env,&id,candidate,&pages);
    g_oem_selector=profiles[1].oem_selector;

    /* Restore the index-zero A-bridge tuple for the existing fail-closed cases. */
    parts[5].off=profiles[0].bridge_offset;
    parts[6].off=profiles[0].oem_offset;
    g_oem_bridge_off=profiles[0].bridge_offset;
    g_oem_offset=profiles[0].oem_offset;
    g_oem_selector=profiles[0].oem_selector;
    make_bc(parts[3].snapshot,profiles[0].bc_appsbl,profiles[0].bc_current_rootfs);
    memcpy(parts[4].snapshot,parts[3].snapshot,BC_BYTES);
    g_version=evaluate_version(active_hash,candidate_hash);

    /* Exercise only the actual production wrapper. Every mutation is restored
     * before the next case; these are synthetic snapshots, never device I/O. */
    parts[0].snapshot[0] ^= 1;
    expect_reject(parts, &env, &id, candidate, &pages);
    parts[0].snapshot[0] ^= 1;

    parts[1].snapshot[0] ^= 1;
    expect_reject(parts, &env, &id, candidate, &pages);
    parts[1].snapshot[0] ^= 1;

    put_le32(parts[3].snapshot + 12 + 3 * 20 + 16, 0);
    put_le32(parts[4].snapshot + 12 + 3 * 20 + 16, 0);
    expect_reject(parts, &env, &id, candidate, &pages);
    put_le32(parts[3].snapshot + 12 + 3 * 20 + 16, p->bc_appsbl);
    put_le32(parts[4].snapshot + 12 + 3 * 20 + 16, p->bc_appsbl);

    parts[4].snapshot[400] ^= 1;
    expect_reject(parts, &env, &id, candidate, &pages);
    parts[4].snapshot[400] ^= 1;

    id.raw_oob_bytes++;
    expect_reject(parts, &env, &id, candidate, &pages);
    id.raw_oob_bytes--;
    parts[1].oob++;
    expect_reject(parts, &env, &id, candidate, &pages);
    parts[1].oob--;

    pages.appsbl[0].ecc_uncorrected_after = 1;
    expect_reject(parts, &env, &id, candidate, &pages);
    pages.appsbl[0].ecc_uncorrected_after = 0;

    uint8_t *saved_env = env.snapshot;
    env.snapshot = NULL;
    expect_reject(parts, &env, &id, candidate, &pages);
    env.snapshot = saved_env;

    char cmdline_path[PATH_MAX];
    assert(snprintf(cmdline_path, sizeof(cmdline_path), "%s/proc/cmdline", g_root) <
           (int)sizeof(cmdline_path));
    write_text(cmdline_path, "machid=deadbeef console=ttyMSM0,115200n8\n");
    expect_reject(parts, &env, &id, candidate, &pages);
    char restored_cmdline[96];
    assert(snprintf(restored_cmdline, sizeof(restored_cmdline),
                    "machid=%x console=ttyMSM0,115200n8\n",
                    (unsigned)BI_EXPECTED_MACHID) < (int)sizeof(restored_cmdline));
    write_text(cmdline_path, restored_cmdline);

    uint8_t *damaged = malloc(APPSBL_BYTES);
    assert(damaged);
    memcpy(damaged, candidate, APPSBL_BYTES);
    damaged[0] ^= 1;
    expect_reject(parts, &env, &id, damaged, &pages);
    free(damaged);
    expect_reject(parts, &env, &id, other_candidate, &pages);

    g_oem_pool_match.product = (BI_RETURN_SOURCE_PRODUCT % 4u) + 1u;
    expect_reject(parts, &env, &id, candidate, &pages);
    g_oem_pool_match.product = BI_RETURN_SOURCE_PRODUCT;
    g_oem_pool_match.exit_policy = 0;
    expect_reject(parts, &env, &id, candidate, &pages);
    g_oem_pool_match.exit_policy = BI_FIRST_INSTALL_OEM_EXIT_POLICY;

    /* Overwrite install: an unknown running APPSBL (an earlier custom U-Boot)
     * is accepted for a registered candidate; every hardware gate still holds. */
    uint8_t *old_custom = malloc(p->target_size);
    assert(old_custom);
    memcpy(old_custom, active, p->target_size);
    old_custom[p->target_size - 1] ^= 0x5a;
    char old_hash[65];
    hash_hex(old_custom, p->target_size, old_hash);
    parts[1].snapshot = old_custom;
    snprintf(g_active_hash, sizeof(g_active_hash), "%s", old_hash);
    g_version = evaluate_version(old_hash, candidate_hash);
    assert(g_version.decision == BV_OVERRIDE_UNKNOWN);
    g_first_install_qualified = 0;
    if (qualify_compiled_first_install(parts, 7, &parts[0], &env, &id, candidate, &pages) != 0) {
        fprintf(stderr, "overwrite install rejected an unknown running APPSBL: %s\n", g_blocked);
        return 14;
    }
    assert(g_first_install_qualified == 1 && g_first_install_profile == &profiles[0]);
    assert(g_modified == 0 && g_can_reboot == 0);
    /* The running APPSBL must still be the one the version decision saw. */
    old_custom[0] ^= 1;
    expect_reject(parts, &env, &id, candidate, &pages);
    old_custom[0] ^= 1;
    /* Hardware gates are unchanged in overwrite mode. */
    parts[0].snapshot[0] ^= 1;
    expect_reject(parts, &env, &id, candidate, &pages);
    parts[0].snapshot[0] ^= 1;
    pages.appsbl[1].ecc_uncorrected_after = 1;
    expect_reject(parts, &env, &id, candidate, &pages);
    pages.appsbl[1].ecc_uncorrected_after = 0;
    parts[4].snapshot[400] ^= 1;
    expect_reject(parts, &env, &id, candidate, &pages);
    parts[4].snapshot[400] ^= 1;
    expect_reject(parts, &env, &id, other_candidate, &pages);
    parts[1].snapshot = active;
    free(old_custom);
    /* An unregistered candidate never becomes an override. */
    g_version = evaluate_version(old_hash, old_hash);
    assert(g_version.decision == BV_REJECT_UNKNOWN);
    expect_reject(parts, &env, &id, candidate, &pages);

    puts("PASS production compiled-profile qualifier; exact positive and fail-closed mismatch predicates; all observations synthetic; no device I/O or writes");
    free(parts[3].snapshot); free(parts[4].snapshot);
    free(env_bytes); free(active); free(sbl); free(other_candidate); free(candidate);
    return 0;
}
