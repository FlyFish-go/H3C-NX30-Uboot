#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
/* Synthetic transaction-only harness: compile-time TEST is mandatory. */
#include "../src/bridge_install.c"
#if !BI_TEST
#error synthetic harness requires BRIDGE_INSTALL_TEST
#endif
static int linux_geometry_case(void){
    bi_nand_identity id;memset(&id,0,sizeof(id));
    id.manufacturer=0xc8;id.chip_id=0x41;strcpy(id.model,"GD5F1GQ5REYIG");
    id.capacity_mib=128;id.page_bytes=2048;id.erase_kib=128;id.raw_oob_bytes=128;
    const struct h3c_nand_catalog_profile *p=match_linux_oem_probe(&id,8,128);
    if(!p||strcmp(p->profile_id,"h3c-oem-c841-128m-2k-sp128"))return 20;
    if(!match_linux_oem_probe(&id,4,128))return 21;
    if(match_linux_oem_probe(&id,8,80)||match_linux_oem_probe(&id,8,64))return 22;
    id.raw_oob_bytes=80;if(match_linux_oem_probe(&id,8,80))return 23;
    id.raw_oob_bytes=128;strcpy(id.model,"GD5F1GQ5REYIG-extra");
    if(match_linux_oem_probe(&id,8,128))return 24;
    puts("PASS OEM Linux raw-spare MTD geometry; U-Boot normalized OOB is not substituted");return 0;
}
static int collector_case(void){
    const bfi_install_profile *profile=bfi_d_install_profile();
    bi_part parts[MAX_PARTS];size_t count=0;memset(parts,0,sizeof(parts));
    for(size_t i=0;i<MAX_PARTS;i++)parts[i].fd=-1;
    if(!profile||discover_all(parts,&count)||protected_snapshot(parts,count))return 10;
    bi_part *sbl=profile_part_at(parts,count,profile->sbl_offset);bi_nand_identity id;
    struct bmo_appsbl_pair_report pages;memset(&pages,0,sizeof(pages));bfi_facts facts;
    if(!sbl||read_nand_probe_identity(&id))return 11;
    bi_part *bridge=profile_part_at(parts,count,profile->bridge_offset);
    bi_part *oem=profile_part_at(parts,count,profile->oem_offset);
    if(!bridge||!oem||bridge->snapshot||oem->snapshot)return 12;
    if(collect_install_facts(profile,parts,count,sbl,&id,&pages,&facts))return 13;
    uint8_t *saved=sbl->snapshot;sbl->snapshot=NULL;
    if(!collect_install_facts(profile,parts,count,sbl,&id,&pages,&facts))return 14;
    sbl->snapshot=saved;
    bi_part *target=profile_part_at(parts,count,profile->target_offset);
    if(!target||!target->snapshot)return 16;
    saved=target->snapshot;target->snapshot=NULL;
    if(!collect_install_facts(profile,parts,count,sbl,&id,&pages,&facts))return 17;
    target->snapshot=saved;
    unsigned size=bridge->size;bridge->size=profile->bridge_bytes-profile->erase;
    if(!collect_install_facts(profile,parts,count,sbl,&id,&pages,&facts))return 15;
    bridge->size=size;
    for(size_t i=0;i<count;i++){free(parts[i].snapshot);part_close(&parts[i]);}
    puts("PASS production profile collector accepts descriptor-bound 40MiB bridge/OEM without RAM snapshots and rejects missing startup snapshot/wrong system size");
    return 0;
}
int main(int argc,char**argv){
    char error[256],state[STATUS_CAP];
    if(argc!=3)return 2;
    if(!strcmp(argv[2],"linux-geometry"))return linux_geometry_case();
    bi_init(argv[1]);if(strcmp(g_phase,"ready"))return 3;
    if(!strcmp(argv[2],"collector"))return collector_case();
    if(!strcmp(argv[2],"revision"))g_oem_pool_match.pool_revision_sha256[0]^=1;
    else if(!strcmp(argv[2],"candidate"))g_oem_pool_match.candidate_sha256[0]^=1;
    else if(!strcmp(argv[2],"group"))g_oem_pool_match.content_group++;
    else if(!strcmp(argv[2],"policy"))g_oem_pool_match.exit_policy=0;
    else return 4;
    if(bi_start(g_token,error,sizeof(error)))return 5;
    for(unsigned i=0;i<6000&&g_child>0;i++){struct timespec t={0,10000000};bi_reap();nanosleep(&t,NULL);}
    if(bi_status_json(state,sizeof(state)))return 6;
    puts(state);
    return strcmp(g_phase,"failed")||g_modified||g_can_reboot;
}
