#include "bb_offline_rules.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    bb_bundle_catalog c; wchar_t message[160]; const bb_bundle_rule *selected=NULL; size_t i;
    if (bb_offline_rules_load_verify(&c,message,sizeof(message)/sizeof(message[0])) != 0) {
        fwprintf(stderr,L"load failed: %ls\n",message); return 10;
    }
    if (!c.schema || strcmp(c.schema,BB_BUNDLE_CATALOG_SCHEMA) || !c.bundles || !c.bundle_count || !c.release_set || strcmp(c.release_set,bb_offline_rules_release_set())) return 11;
    for (i=0;i<c.bundle_count;i++) {
        const bb_bundle_rule *r=&c.bundles[i];
        size_t j, matches=0;
        bb_select_status s;
        /* Each R6 bundle carries providers A-D; only an empty list is invalid. */
        if (!r->plan || r->provider_count==0) return 12;
        for (j=0;j<c.bundle_count;j++)
            if (c.bundles[j].plan->input_size==r->plan->input_size &&
                !memcmp(c.bundles[j].plan->input_sha256,r->plan->input_sha256,32)) ++matches;
        s=bb_select_bundle(&c,r->plan->input_size,r->plan->input_sha256,&selected);
        if (matches>1) { if(s!=BB_SELECT_AMBIGUOUS_BUNDLE || selected) return 13; continue; }
        if (s==BB_SELECT_AMBIGUOUS_BUNDLE) { if(selected) return 13; continue; }
        if (s!=BB_SELECT_OK || selected!=r) return 14;
    }
    fwprintf(stdout,L"verified %u bundle record(s); duplicate source fingerprints refuse as ambiguous\n",(unsigned)c.bundle_count);
    return 0;
}
