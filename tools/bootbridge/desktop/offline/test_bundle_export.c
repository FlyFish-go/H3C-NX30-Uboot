#include "bb_offline_rules.h"
#include "../src/bb_export.h"
#include <string.h>

int main(int argc,char **argv)
{
    bb_bundle_catalog catalog;wchar_t message[160];const bb_bundle_rule *selected=NULL;
    if(argc!=3)return 2;
    if(bb_offline_rules_load_verify(&catalog,message,sizeof(message)/sizeof(message[0]))!=0)return 3;
    if(!catalog.schema||strcmp(catalog.schema,BB_BUNDLE_CATALOG_SCHEMA)||!catalog.bundle_count)return 4;
    if(bb_select_bundle(&catalog,catalog.bundles[0].plan->input_size,catalog.bundles[0].plan->input_sha256,&selected)!=BB_SELECT_OK||!selected)return 5;
    return bb_export_plan_bin(argv[1],argv[2],selected->plan,1,selected->plan->id).status==BB_OK?0:6;
}
