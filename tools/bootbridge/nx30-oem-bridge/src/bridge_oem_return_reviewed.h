#ifndef BRIDGE_OEM_RETURN_REVIEWED_H
#define BRIDGE_OEM_RETURN_REVIEWED_H
/* Independently reviewed exit-only BC I/O inheritance, not hardware-family or
 * new candidate APPSBL qualification. Parent review 2026-10-15 approved this
 * exact repaired-Winbond64/R012 route. Evidence packet under
 * work/bootbridge-goal-20261015/oem-return-separation/BC-IO-APPROVED-21d3f3b4fca93cd7566e1987e7b40335d939c083207d9df8ef9d3609383e2c99.json.
 * Source fields cannot add rows; any transformed FIT/route/IO change needs a
 * fresh review. Missing truthful source header fails before this can match.
 */
static const br_return_rule bootbridge_reviewed_return_rules[] = {{
  1u,BOS_PRODUCT_NX30,BOS_EXIT_NX30_R012_REGISTERED_STOCK_BASELINE0,
  "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922",
  "5878e39213f37fb3d26b51a5197e09b05e7de18ee275fd7f430ea6eac5e7dc85",
  "bf9683e4ef1e75e91f3c82280dc36e6ad02ae4137dfa4f4234565f94e2bf668e",
  "40a6acb40913eeb3d112b78399c24f5559a80e0e09bbc3fab86d7f2a70e1ed3f",
  "config@mp02.1","qcom,ipq5018-mp02.1","h3c-oem-efba-128m-2k-sp64",
  4u,64u,BR_RETURN_BC_WRITE_REVIEWED_LINUX_MTD_V1,BR_RETURN_ENV_EXPLICIT_BOOTIPQ_V1,
  "ae0767a2ffbf040f9b73ef74b1fdf0b6490679595ce302299157d846fad062e6",
  "8a59fdb5d6864a98fa833d0fa56aa615a13acc99038c459d7e0039fed460acfd",
  "21d3f3b4fca93cd7566e1987e7b40335d939c083207d9df8ef9d3609383e2c99"
}};
#define BI_RETURN_RULES bootbridge_reviewed_return_rules
#define BI_RETURN_RULE_COUNT (sizeof(bootbridge_reviewed_return_rules)/sizeof(bootbridge_reviewed_return_rules[0]))
#endif
