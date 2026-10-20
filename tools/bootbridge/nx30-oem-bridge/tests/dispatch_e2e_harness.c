/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "bridge_dispatch.h"
#include "bridge_provider_bundle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void facts_from_profile(const bdi_nand_profile *p,const char *route,bdi_facts *f)
{
    memset(f,0,sizeof(*f));
    size_t n=strlen(route);
    assert(n+1u<=sizeof(f->compatible));memcpy(f->compatible,route,n+1u);
    f->compatible_bytes=n+1u;strncpy(f->nand_model,p->model_aliases[0],sizeof(f->nand_model)-1u);
    f->jedec_bytes_observed=p->jedec_bytes_observed;f->manufacturer_id=p->manufacturer_id;
    f->chip_id=p->chip_id;f->capacity_mib=p->capacity_mib;f->page_bytes=p->page_bytes;
    f->erase_bytes=p->erase_bytes;f->raw_spare_bytes=p->raw_spare_bytes;
    f->mtd_oob_bytes=p->mtd_oob_bytes;f->ecc_bits=p->ecc_bits;
    f->have_compatible=f->have_nand_model=f->have_jedec=f->have_geometry=f->have_mtd_format=1;
}

static void facts_from_profile_name(const bdi_nand_profile *p,const char *route,
                                    const char *model,bdi_facts *f)
{
    facts_from_profile(p,route,f);
    assert(strlen(model)<sizeof(f->nand_model));strcpy(f->nand_model,model);
}

static const bdi_provider_record *select_with_one_ready(size_t target,
        const bdi_facts *facts,const bdi_nand_profile **matched,bdi_result *result)
{
    int *ready=calloc(BI_PROVIDER_COUNT,sizeof(*ready));assert(ready);ready[target]=1;
    const bdi_provider_record *selected=bdi_select_inspected_provider(
        bootbridge_provider_records,BI_PROVIDER_COUNT,BI_PROVIDER_SOURCE_SHA256,
        BI_PROVIDER_PRODUCT,facts,ready,matched,result);
    free(ready);return selected;
}

static void test_each_complete_c_record(void)
{
    unsigned candidates=0,profiles=0,aliases=0;
    for(size_t i=0;i<BI_PROVIDER_COUNT;i++){
        const bdi_provider_record *r=&bootbridge_provider_records[i];
        assert(r->nand_profile_count>0);
        for(size_t k=0;k<r->nand_profile_count;k++){
            bdi_facts facts;bdi_result result;const bdi_nand_profile *matched=NULL;
            facts_from_profile(&r->nand_profiles[k],r->route_compatible,&facts);
            const bdi_provider_record *selected=select_with_one_ready(i,&facts,&matched,&result);
            assert(selected==r&&matched==&r->nand_profiles[k]&&result==BDI_OK);
            assert(!strcmp(selected->record_id,r->record_id));
            assert(!strcmp(selected->candidate_id,r->candidate_id));
            assert(!strcmp(selected->hardware_class,r->hardware_class));
            assert(!strcmp(selected->provider_path,r->provider_path));
            profiles++;
            for(size_t a=0;a<r->nand_profiles[k].model_alias_count;a++){
                facts_from_profile_name(&r->nand_profiles[k],r->route_compatible,
                    r->nand_profiles[k].model_aliases[a],&facts);
                selected=select_with_one_ready(i,&facts,&matched,&result);
                assert(selected==r&&matched==&r->nand_profiles[k]&&result==BDI_OK);
                aliases++;
            }
        }
        candidates++;
    }
    printf("synthetic_inspector_readiness=one-row-ready; tested_candidates=%u tested_profiles=%u tested_aliases=%u\n",
           candidates,profiles,aliases);
}

static void test_negative_nand_facts(void)
{
    const bdi_provider_record *r=&bootbridge_provider_records[0];
    const bdi_nand_profile *p=&r->nand_profiles[0];bdi_facts f;bdi_result result;
    facts_from_profile(p,r->route_compatible,&f);
    size_t n=strlen(f.nand_model);assert(n>1);f.nand_model[n-1]=0;
    assert(!bdi_select_provider(bootbridge_provider_records,BI_PROVIDER_COUNT,
        BI_PROVIDER_SOURCE_SHA256,BI_PROVIDER_PRODUCT,&f,NULL,&result));
    assert(result==BDI_NO_MATCH);
    facts_from_profile(p,r->route_compatible,&f);
    strcpy(f.nand_model,"UNKNOWN-NAND-MODEL");f.manufacturer_id=0;f.chip_id=0;
    assert(!bdi_select_provider(bootbridge_provider_records,BI_PROVIDER_COUNT,
        BI_PROVIDER_SOURCE_SHA256,BI_PROVIDER_PRODUCT,&f,NULL,&result));
    assert(result==BDI_NO_MATCH);
    /* Explicitly reject the previously abbreviated Winbond model; it is not
     * an alias for the full observed W25N01GWZEIG string. */
    int found_w25=0;
    for(size_t i=0;i<BI_PROVIDER_COUNT&&!found_w25;i++)
        for(size_t k=0;k<bootbridge_provider_records[i].nand_profile_count;k++){
            const bdi_provider_record *wr=&bootbridge_provider_records[i];
            const bdi_nand_profile *wp=&wr->nand_profiles[k];
            for(size_t a=0;a<wp->model_alias_count;a++)if(!strcmp(wp->model_aliases[a],"W25N01GWZEIG")){
                facts_from_profile_name(wp,wr->route_compatible,"W25N01GW",&f);
                assert(!bdi_select_provider(bootbridge_provider_records,BI_PROVIDER_COUNT,
                    BI_PROVIDER_SOURCE_SHA256,BI_PROVIDER_PRODUCT,&f,NULL,&result));
                assert(result==BDI_NO_MATCH);found_w25=1;break;
            }
        }
    assert(found_w25);
    /* C8:41's same JEDEC ID does not make ECC4/OOB64 match its RC A0/C
     * emitted ECC8/OOB128 profile. Keep the observed raw spare at 128. */
    int found_c841=0;
    for(size_t i=0;i<BI_PROVIDER_COUNT&&!found_c841;i++)
        for(size_t k=0;k<bootbridge_provider_records[i].nand_profile_count;k++){
            const bdi_provider_record *cr=&bootbridge_provider_records[i];
            const bdi_nand_profile *cp=&cr->nand_profiles[k];
            if(strcmp(cp->profile_id,"h3c-oem-c841-128m-2k-sp128"))continue;
            facts_from_profile(cp,cr->route_compatible,&f);f.ecc_bits=4u;f.mtd_oob_bytes=64u;
            assert(!bdi_select_provider(bootbridge_provider_records,BI_PROVIDER_COUNT,
                BI_PROVIDER_SOURCE_SHA256,BI_PROVIDER_PRODUCT,&f,NULL,&result));
            assert(result==BDI_NO_MATCH);found_c841=1;break;
        }
    assert(found_c841);
    puts("negative_nand_facts=truncated_alias,W25N01GW,unknown_nand,C841_same_ID_ECC4_OOB64 PASS");
}

static int same_profile(const bdi_nand_profile *a,const bdi_nand_profile *b)
{
    return a->jedec_bytes_observed==b->jedec_bytes_observed&&
        a->manufacturer_id==b->manufacturer_id&&a->chip_id==b->chip_id&&
        a->capacity_mib==b->capacity_mib&&a->page_bytes==b->page_bytes&&
        a->erase_bytes==b->erase_bytes&&a->raw_spare_bytes==b->raw_spare_bytes&&
        a->mtd_oob_bytes==b->mtd_oob_bytes&&a->ecc_bits==b->ecc_bits&&
        !strcmp(a->model_aliases[0],b->model_aliases[0]);
}

static void test_bd_both_ready_is_ambiguous(void)
{
    for(size_t i=0;i<BI_PROVIDER_COUNT;i++){
        const bdi_provider_record *b=&bootbridge_provider_records[i];
        if(strcmp(b->hardware_class,"B"))continue;
        for(size_t j=0;j<BI_PROVIDER_COUNT;j++){
            const bdi_provider_record *d=&bootbridge_provider_records[j];
            if(strcmp(d->hardware_class,"D")||strcmp(b->route_compatible,d->route_compatible))continue;
            for(size_t x=0;x<b->nand_profile_count;x++)for(size_t y=0;y<d->nand_profile_count;y++){
                if(!same_profile(&b->nand_profiles[x],&d->nand_profiles[y]))continue;
                bdi_facts facts;bdi_result result;const bdi_nand_profile *matched=NULL;
                facts_from_profile(&b->nand_profiles[x],b->route_compatible,&facts);
                int *ready=calloc(BI_PROVIDER_COUNT,sizeof(*ready));assert(ready);ready[i]=ready[j]=1;
                const bdi_provider_record *selected=bdi_select_inspected_provider(
                    bootbridge_provider_records,BI_PROVIDER_COUNT,BI_PROVIDER_SOURCE_SHA256,
                    BI_PROVIDER_PRODUCT,&facts,ready,&matched,&result);
                free(ready);assert(!selected&&result==BDI_CANDIDATE_AMBIGUOUS);
                printf("bd_both_ready=BDI_CANDIDATE_AMBIGUOUS profile=%s\n",b->nand_profiles[x].profile_id);
                return;
            }
        }
    }
    fprintf(stderr,"no identical B/D route+NAND profile in complete table\n");abort();
}

static void test_bundle_path_contracts(void)
{
    const bdi_provider_record *r=&bootbridge_provider_records[0];
    const bdi_nand_profile *p=&r->nand_profiles[0];bdi_facts f;bdi_result result;
    facts_from_profile(p,r->route_compatible,&f);
    /* The actual table has shared content-addressed paths; valid identical
     * path/hash/size contracts must survive bundle validation. */
    assert(bootbridge_provider_records[0].payload_path[0]=='/');
    assert(bdi_select_provider(bootbridge_provider_records,BI_PROVIDER_COUNT,
        BI_PROVIDER_SOURCE_SHA256,BI_PROVIDER_PRODUCT,&f,NULL,&result)||result==BDI_AMBIGUOUS);
    bdi_provider_record *mut=(bdi_provider_record*)malloc(sizeof(*mut)*BI_PROVIDER_COUNT);assert(mut);
    memcpy(mut,bootbridge_provider_records,sizeof(*mut)*BI_PROVIDER_COUNT);
    mut[1].provider_path=mut[0].provider_path;
    assert(!bdi_select_provider(mut,BI_PROVIDER_COUNT,BI_PROVIDER_SOURCE_SHA256,
        BI_PROVIDER_PRODUCT,&f,NULL,&result)&&result==BDI_BAD_BUNDLE);
    memcpy(mut,bootbridge_provider_records,sizeof(*mut)*BI_PROVIDER_COUNT);
    mut[1].payload_path="/etc/bridge/provider-payload-bases/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.bin";
    assert(!bdi_select_provider(mut,BI_PROVIDER_COUNT,BI_PROVIDER_SOURCE_SHA256,
        BI_PROVIDER_PRODUCT,&f,NULL,&result)&&result==BDI_BAD_BUNDLE);
    memcpy(mut,bootbridge_provider_records,sizeof(*mut)*BI_PROVIDER_COUNT);
    mut[1].payload_sha256="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    assert(!bdi_select_provider(mut,BI_PROVIDER_COUNT,BI_PROVIDER_SOURCE_SHA256,
        BI_PROVIDER_PRODUCT,&f,NULL,&result)&&result==BDI_BAD_BUNDLE);
    memcpy(mut,bootbridge_provider_records,sizeof(*mut)*BI_PROVIDER_COUNT);
    mut[1].payload_bytes--;
    assert(!bdi_select_provider(mut,BI_PROVIDER_COUNT,BI_PROVIDER_SOURCE_SHA256,
        BI_PROVIDER_PRODUCT,&f,NULL,&result)&&result==BDI_BAD_BUNDLE);
    memcpy(mut,bootbridge_provider_records,sizeof(*mut)*BI_PROVIDER_COUNT);
    mut[0].provider_path="/usr/sbin/not-a-bridge-provider";
    assert(!bdi_select_provider(mut,BI_PROVIDER_COUNT,BI_PROVIDER_SOURCE_SHA256,
        BI_PROVIDER_PRODUCT,&f,NULL,&result)&&result==BDI_BAD_BUNDLE);
    /* Renaming every display identity must not allow the same candidate and
     * exact NAND parameter group twice under one board/source tuple. */
    memcpy(mut,bootbridge_provider_records,sizeof(*mut)*BI_PROVIDER_COUNT);
    bdi_nand_profile renamed_profile=mut[0].nand_profiles[0];
    renamed_profile.profile_id="renamed-profile-id-same-nand-parameters";
    mut[1].record_id="renamed-record-id";
    mut[1].candidate_id=mut[0].candidate_id;
    mut[1].hardware_class=mut[0].hardware_class;
    mut[1].nand_profiles=&renamed_profile;mut[1].nand_profile_count=1;
    mut[1].provider_path="/usr/sbin/bridge-candidates/renamed-provider/provider";
    assert(!bdi_select_provider(mut,BI_PROVIDER_COUNT,BI_PROVIDER_SOURCE_SHA256,
        BI_PROVIDER_PRODUCT,&f,NULL,&result)&&result==BDI_BAD_BUNDLE);
    /* A split alias set must also be rejected when any alias overlaps for the
     * same numeric NAND tuple; exact alias-list equality is not required. */
    memcpy(mut,bootbridge_provider_records,sizeof(*mut)*BI_PROVIDER_COUNT);
    const char *expanded_aliases[]={mut[0].nand_profiles[0].model_aliases[0],"SYNTHETIC-SECOND-ALIAS"};
    bdi_nand_profile expanded_profile=mut[0].nand_profiles[0];
    expanded_profile.model_aliases=expanded_aliases;expanded_profile.model_alias_count=2;
    bdi_nand_profile overlapping_profile=mut[0].nand_profiles[0];
    overlapping_profile.profile_id="different-profile-with-overlapping-alias";
    mut[0].nand_profiles=&expanded_profile;mut[0].nand_profile_count=1;
    mut[1].record_id="other-record-id";mut[1].candidate_id=mut[0].candidate_id;
    mut[1].hardware_class=mut[0].hardware_class;
    mut[1].nand_profiles=&overlapping_profile;mut[1].nand_profile_count=1;
    mut[1].provider_path="/usr/sbin/bridge-candidates/other-provider/provider";
    assert(!bdi_select_provider(mut,BI_PROVIDER_COUNT,BI_PROVIDER_SOURCE_SHA256,
        BI_PROVIDER_PRODUCT,&f,NULL,&result)&&result==BDI_BAD_BUNDLE);
    free(mut);puts("bundle_paths=shared-identical PASS; duplicate-provider,invalid-provider,wrong-base-name,digest/size-conflict REJECT PASS; renamed-identities and overlapping-alias duplicate tuple REJECT PASS");
}

int main(void)
{
    assert(BI_PROVIDER_BUNDLE_ABI_VERSION==2u);
    assert(BI_PROVIDER_COUNT>0u&&BI_PROVIDER_COUNT<=160u);
    test_each_complete_c_record();test_negative_nand_facts();
    test_bd_both_ready_is_ambiguous();test_bundle_path_contracts();
    puts("production dispatcher complete-table gate PASS");return 0;
}
