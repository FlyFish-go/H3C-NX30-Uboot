/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bridge_install_identity.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENV_BYTES 0x80000u
#define ENV_CRC_BYTES 0x40000u

typedef struct {
    const char *board, *candidate, *machid, *env_alias;
    unsigned filter_env_root_keys, allow_missing_env_machid;
} candidate_policy;

typedef struct {
    const char *source_sha256, *source_product, *observed_product, *env_alias;
    unsigned board_mask;
} source_policy;

/* A fresh candidate is admitted only by a generated, compile-time pinned
 * policy. Runtime input cannot extend this registry. The archive descriptor,
 * payload digest and source matrix are independently checked by the builder
 * and provider before this ENV compatibility check runs. */
#ifdef BII_CANDIDATE_POLICY_HEADER
#include BII_CANDIDATE_POLICY_HEADER
#endif

/* Exact public release payloads from the reviewed 5f486f5f source archive.
 * These records identify offline controlled-trial bytes only; they grant no
 * install, ECC, SBL, OEM-return, or native qualification. B and D share a
 * machid, so both board and candidate digest are mandatory selectors.
 * `filter_env_root_keys` is enabled only where that exact release build
 * enables the C/D source filter in update_bootargs (C production port, D
 * RAM_WEB). A appends generic root args; B's RT3000 composer filters only
 * after resolving a runtime UBI label, so both remain conservative here. */
static const candidate_policy policies[] = {
#ifdef BII_EXTRA_CANDIDATE_POLICIES
    BII_EXTRA_CANDIDATE_POLICIES
#endif
    /* Rule set H3C-BOOTBRIDGE-NAND-OEM-20261020-R4: the 84 source-bound
     * candidates (4 boards x 21 NAND profiles). These are the exact
     * appsbl.partition.bin digests the R4 provider payload bases carry; the
     * table is pinned here so a release can never ship a provider whose
     * admission policy does not know its own payload. Keep the historical
     * pre.1 records below for the frozen baseline archive. */
    /* HW-A: 21 个 R4 候选 */
    {"A", "0104c3073d717b294f68731814d533deb3c7a016b8507dec6eaa83c59d0715b2", "08040001", NULL, 0, 0},  /* h3c-oem-c811-128m-2k-sp64 ecc4 */
    {"A", "065c65b18780a97f9d5b73391edf4c9109dfe543c6bebeb835145c9ad1b4dd0e", "08040001", NULL, 0, 0},  /* h3c-oem-a1a5-128m-2k-sp64-p2048-e131072 ecc4 */
    {"A", "40521f6e0f2bd308b186cbf782176db756799ee3afc14b6c55e714cabb01b972", "08040001", NULL, 0, 0},  /* h3c-oem-c8c1-128m-2k-sp128 ecc8 */
    {"A", "48ecebccbc64f474e3ab502dfb1185d1a261107dda3d6b13350aab555a545411", "08040001", NULL, 0, 0},  /* h3c-oem-c841-128m-2k-sp128 ecc4 */
    {"A", "541edc9b929e61fa8aa6eef85ec011790b6d668d16e3cf89b722ccdb4d7285e1", "08040001", NULL, 0, 0},  /* h3c-oem-c845-128m-2k-sp128 ecc4 */
    {"A", "5a9ab9f5df2072016b6bb4d4445f877abc97e8b7da943cd5cf58b6105a3a5a14", "08040001", NULL, 0, 0},  /* h3c-oem-98db-128m-2k-sp64 ecc4 */
    {"A", "5b7eff02b545fdb7752837667ef10f412e93aaffbcb541488d2a620caa5ac70e", "08040001", NULL, 0, 0},  /* h3c-oem-c822-256m-2k-sp64 ecc4 */
    {"A", "7b5a8d42a2f9a5ac3d25b1661dce9559eaad77e91942399584a6925ef1697145", "08040001", NULL, 0, 0},  /* h3c-oem-efbc-128m-2k-sp64 ecc4 */
    {"A", "80e55dc1f306ed24d655f81a01f04e1434d8dc824113eceab8eca0a62e8e6efc", "08040001", NULL, 0, 0},  /* h3c-oem-e5a4-128m-2k-sp128 ecc8 */
    {"A", "8f705ef0121f123c82a6399956538d97cbb598e407fb65b22cb8c6b4360fc43f", "08040001", NULL, 0, 0},  /* h3c-oem-2c15-128m-2k-sp128 ecc8 */
    {"A", "92acaaed58c45dcf1f4fe833628a861561fb2d40e9000264f88624f1cc599d14", "08040001", NULL, 0, 0},  /* h3c-oem-c292-128m-2k-sp64 ecc4 */
    {"A", "a9b0b48fedff7088cf1fe08e7085e52ddd5c77e253f5ff8797cbe618a8e551f9", "08040001", NULL, 0, 0},  /* h3c-oem-e521-128m-2k-sp128 ecc4 */
    {"A", "b63d8ba9dd2e24575bc3f13f89e7a7e66031755f29448c7f69a649bcef0671ae", "08040001", NULL, 0, 0},  /* h3c-oem-efbf-256m-2k-sp64 ecc4 */
    {"A", "c16e1418c3dbceb781d420f927766ff1a2f0c8b6e93d00f36c7517001042efd6", "08040001", NULL, 0, 0},  /* h3c-oem-c8c9-128m-2k-sp64 ecc4 */
    {"A", "cb9c3c42a504295a95ac03acd8f598e5112cf4d2faed5dcc553a7ecaddd992c2", "08040001", NULL, 0, 0},  /* h3c-oem-e572-128m-2k-sp64 ecc4 */
    {"A", "d7eb81bde2342d76a7df3b59242f286232ec9e5cf6056aae7c6240f23d9d0f80", "08040001", NULL, 0, 0},  /* h3c-oem-98d2-128m-2k-sp128 ecc8 */
    {"A", "dbaabc885d6d8a8a970b7ac0284079a7bdf25196eefcbef2fa8bbd9325e8f450", "08040001", NULL, 0, 0},  /* h3c-oem-c821-128m-2k-sp64 ecc4 */
    {"A", "ec7845271a40580b727be6eac2cd4854801d871ef202c3df7bc4bb0a241cc44f", "08040001", NULL, 0, 0},  /* h3c-oem-c881-128m-2k-sp128 ecc8 */
    {"A", "fcf4080c4175704ff395e6ea84958f97f7f067e4a7b6e91c91c658fba6ba100a", "08040001", NULL, 0, 0},  /* h3c-oem-c841-128m-2k-sp128 ecc8 */
    {"A", "fd8a5f0effc45b7595024ba963e4f0c497d22aeb7be5f88019651a88f82df3cb", "08040001", NULL, 0, 0},  /* h3c-oem-efba-128m-2k-sp64 ecc4 */
    {"A", "ff6b8144104ebb09d9729b096e7c17b14f3cf14a74bf20cbc7d9cc53654ee8ab", "08040001", NULL, 0, 0},  /* h3c-oem-a1a5-128m-2k-sp128-p2048-e131072 ecc8 */
    /* HW-B: 21 个 R4 候选 */
    {"B", "0ac14319701d09dab97d37eb7ced12738090c9a0d1f20028c5c3cdd122194506", "08040000", NULL, 0, 0},  /* h3c-oem-efbf-256m-2k-sp64 ecc4 */
    {"B", "0b69f4d41516eb4e4799690313ee7a3c10fef02be2b0fdae78441ed13779a3dd", "08040000", NULL, 0, 0},  /* h3c-oem-efba-128m-2k-sp64 ecc4 */
    {"B", "2b9a56c427b8173e33235fc02ce0309607fe9aebddd2154f00378c23fd03d32a", "08040000", NULL, 0, 0},  /* h3c-oem-c822-256m-2k-sp64 ecc4 */
    {"B", "31838fe967478f5448eb57aac88e237eb3275764f8ea7ae9a6c66bde9228f696", "08040000", NULL, 0, 0},  /* h3c-oem-a1a5-128m-2k-sp128-p2048-e131072 ecc8 */
    {"B", "3ef6b4cbfe9b6188b946bee65256caf1f0fbcfe4f967704b09f80b283c493fe0", "08040000", NULL, 0, 0},  /* h3c-oem-c881-128m-2k-sp128 ecc8 */
    {"B", "3f004d196eb3a8d3d8af5f5f1e11d629b6a9fe5563be8ace09929558a9499738", "08040000", NULL, 0, 0},  /* h3c-oem-c841-128m-2k-sp128 ecc4 */
    {"B", "660339f0552f3e4431db573449969436a9b92f3aa712c437268ba8f63d4b046f", "08040000", NULL, 0, 0},  /* h3c-oem-98db-128m-2k-sp64 ecc4 */
    {"B", "746747c508d715f8680df7f2ebd3a38a3d18e027211343fa566f6aea3248d1c2", "08040000", NULL, 0, 0},  /* h3c-oem-c821-128m-2k-sp64 ecc4 */
    {"B", "88d1f2d9fa85cd6b68cea71727e75f858837feca64ceca6c66e23fafb8d67359", "08040000", NULL, 0, 0},  /* h3c-oem-c8c9-128m-2k-sp64 ecc4 */
    {"B", "9205553ebf3c6008ba2d2d20168d32b594130f71b30bb81f8dedf7eba7834304", "08040000", NULL, 0, 0},  /* h3c-oem-e5a4-128m-2k-sp128 ecc8 */
    {"B", "a31d65b2efdb04ad72dd888776f6241983c7f86ad1f1387e145d6b024f753184", "08040000", NULL, 0, 0},  /* h3c-oem-98d2-128m-2k-sp128 ecc8 */
    {"B", "a60ea87edc3fec2618b30a1df9545209c0c607dcb4e7ea9cf27de9f2c0e46379", "08040000", NULL, 0, 0},  /* h3c-oem-e572-128m-2k-sp64 ecc4 */
    {"B", "a7ebebda361d41413db848bf14bc9003bbeb689086a6acd4eb75c41a5588e85b", "08040000", NULL, 0, 0},  /* h3c-oem-efbc-128m-2k-sp64 ecc4 */
    {"B", "acbb09a063dff7f5fd058b8495901d1ac33e393ba6014e5b59f235a28f7bf6dc", "08040000", NULL, 0, 0},  /* h3c-oem-2c15-128m-2k-sp128 ecc8 */
    {"B", "aeab4c7a41f9fd0e55eb33319fcb38f55dabd9cb27daaaaf57c31f281e3d4f7e", "08040000", NULL, 0, 0},  /* h3c-oem-e521-128m-2k-sp128 ecc4 */
    {"B", "b714e98ed0cdd286b073adf9ff5bc359006f717f95e6eb3e86b3698360410906", "08040000", NULL, 0, 0},  /* h3c-oem-c845-128m-2k-sp128 ecc4 */
    {"B", "dbc3b15fc2ac1b6563ed37ab5424d20d9f466ac9a0850995d6b943d68fcbd5a4", "08040000", NULL, 0, 0},  /* h3c-oem-c841-128m-2k-sp128 ecc8 */
    {"B", "e1b199d9500847db8f47ff4c19b70c2edf9f2f747e7b9d732f4c105c6af0c962", "08040000", NULL, 0, 0},  /* h3c-oem-c8c1-128m-2k-sp128 ecc8 */
    {"B", "f4f962ea305f5f987ef7a0b08959fc421ff5b3a8939415eb9fb4cc1ea5a42f68", "08040000", NULL, 0, 0},  /* h3c-oem-a1a5-128m-2k-sp64-p2048-e131072 ecc4 */
    {"B", "f9e0397152fafe80259c1860c0da5213eb6710ec12da67f69eb2feead215c0f7", "08040000", NULL, 0, 0},  /* h3c-oem-c811-128m-2k-sp64 ecc4 */
    {"B", "fc07eeb861a4f8f637beb1654a3125bb2af15254c1bfc4a5de33926458043860", "08040000", NULL, 0, 0},  /* h3c-oem-c292-128m-2k-sp64 ecc4 */
    /* HW-C: 21 个 R4 候选 */
    {"C", "19a5ad8b52964a2b327bd9d2cd0815ab5efc5f3486ab0f2fac45ede1673a814c", "08040101", NULL, 1, 0},  /* h3c-oem-c845-128m-2k-sp128 ecc4 */
    {"C", "3689dd0ba8a058e4c659d949ac3ed6bddfdf563da5eab761f6afdda3717f1fc9", "08040101", NULL, 1, 0},  /* h3c-oem-c292-128m-2k-sp64 ecc4 */
    {"C", "55881f0739e08cf725d6a138cd514156c0aa9cf03270011a9bde651ee806b3ee", "08040101", NULL, 1, 0},  /* h3c-oem-98d2-128m-2k-sp128 ecc8 */
    {"C", "63f6538e7431b1b9a3c208f19844296bad605fb44546369a6495d7ec5725efc0", "08040101", NULL, 1, 0},  /* h3c-oem-2c15-128m-2k-sp128 ecc8 */
    {"C", "65b2dd1f105abaa4035c1110ed0c40bad6898fe5547e5811aab86fd1b3457f09", "08040101", NULL, 1, 0},  /* h3c-oem-e572-128m-2k-sp64 ecc4 */
    {"C", "69f7a5fd98a596eddcd02cf1e54eb974c448ae4086b16b051564368fa011fe8e", "08040101", NULL, 1, 0},  /* h3c-oem-a1a5-128m-2k-sp128-p2048-e131072 ecc8 */
    {"C", "6a4ec3fd3fa4deb141ff7ac62d19a92cb16460d0b7eb1081a846580f04dfa7e9", "08040101", NULL, 1, 0},  /* h3c-oem-c881-128m-2k-sp128 ecc8 */
    {"C", "810f80f492561138dccfbe3969c4692e798a581eabd4535b337d2bb7f5cd7575", "08040101", NULL, 1, 0},  /* h3c-oem-a1a5-128m-2k-sp64-p2048-e131072 ecc4 */
    {"C", "81b1326a18622460facf00a1086bb0d4645156d5bb9e639010119b3608bb2935", "08040101", NULL, 1, 0},  /* h3c-oem-98db-128m-2k-sp64 ecc4 */
    {"C", "885ca7e203903a52eabdee195ab6f7e0b8c12d752680a545057cae27197b6228", "08040101", NULL, 1, 0},  /* h3c-oem-e521-128m-2k-sp128 ecc4 */
    {"C", "8c0b69c405ddc04184648ce96bd87c8037e1516a77856f04b5d90ed0f3d35f24", "08040101", NULL, 1, 0},  /* h3c-oem-e5a4-128m-2k-sp128 ecc8 */
    {"C", "8ee89443e00276b2614943d1f7f0601c2fe6492d9504655af16f8979c6d8d367", "08040101", NULL, 1, 0},  /* h3c-oem-efbc-128m-2k-sp64 ecc4 */
    {"C", "977a953da0781c380ebcaf3aeebbb0b6568ed28d07dcac92460abe1e0d1c1d06", "08040101", NULL, 1, 0},  /* h3c-oem-c821-128m-2k-sp64 ecc4 */
    {"C", "9b3a6c9e648e1b8829f4f60199f1d8e4e673c0b3b6f40c8e735440426f9efb72", "08040101", NULL, 1, 0},  /* h3c-oem-c841-128m-2k-sp128 ecc8 */
    {"C", "c4ccf2c1f8e26ff3f714e0dca332453c59c71be96a91dae89898d44682370967", "08040101", NULL, 1, 0},  /* h3c-oem-c841-128m-2k-sp128 ecc4 */
    {"C", "ca630ffee7230c2cd802e783fd4254677a8f066a6978748df94279f70981ba80", "08040101", NULL, 1, 0},  /* h3c-oem-efba-128m-2k-sp64 ecc4 */
    {"C", "d31f70d79ac81981cebd48dac1e893cd05ab4061d0c5fac368ac57635bff938c", "08040101", NULL, 1, 0},  /* h3c-oem-efbf-256m-2k-sp64 ecc4 */
    {"C", "d9cc8de4a409136fea2509e6e6dcc71b37263023a0dcb958c7f9056b2ef7ee14", "08040101", NULL, 1, 0},  /* h3c-oem-c8c9-128m-2k-sp64 ecc4 */
    {"C", "daec06f40e88b0c84ed3bf7fb314040f1daa23097ee3d8c4ac138ff42d832591", "08040101", NULL, 1, 0},  /* h3c-oem-c811-128m-2k-sp64 ecc4 */
    {"C", "eb9a034ca1fff09ad402f98a6d6acf1e4605eee63eb7ac36f052e07cc359ee8e", "08040101", NULL, 1, 0},  /* h3c-oem-c8c1-128m-2k-sp128 ecc8 */
    {"C", "f57d1d751dacd92bc463ba92599cccc8db5ccb281ef6e17d7bd0caf88a741de9", "08040101", NULL, 1, 0},  /* h3c-oem-c822-256m-2k-sp64 ecc4 */
    /* HW-D: 21 个 R4 候选 */
    {"D", "0089ae8b8827deb9454b3583ffe960008590671dab8ec82de6c44cbe78ee7778", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-a1a5-128m-2k-sp64-p2048-e131072 ecc4 */
    {"D", "0e46762184c6a728c4005a9995b5705bc90f191822aa8358a4b8e5fec4567fa9", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-a1a5-128m-2k-sp128-p2048-e131072 ecc8 */
    {"D", "135dcc79984949242dc93231ce958277da64015fdc6242c4e15fcf1d4fc3c997", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-efba-128m-2k-sp64 ecc4 */
    {"D", "1de37d8c72a1ca31d5963c5511b27a924a55fa7b113969207713e5747142a163", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-c8c9-128m-2k-sp64 ecc4 */
    {"D", "34c336a12da62bb0610fae224af6c080c75a2d7ea01fe7f185cac4bef87cdd2a", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-e5a4-128m-2k-sp128 ecc8 */
    {"D", "36a8b72a9215159f88cc57f7671cadb7456ebc937d68ca4c3d049876e16dc936", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-e572-128m-2k-sp64 ecc4 */
    {"D", "46ad5dbbe512a2f3d1ae2db8c677fe26afbc3f1dc9f141ed2648ee0b430c546d", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-c845-128m-2k-sp128 ecc4 */
    {"D", "4959dfaa074f6ac59fe173384424b66ebe873c425aacaf08fbc72f627c2de223", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-c8c1-128m-2k-sp128 ecc8 */
    {"D", "59bd4a28a08b0d1b04cf89e4e4ca6e89dd65a5e0ad8faf1c9776416b426e6d31", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-c841-128m-2k-sp128 ecc8 */
    {"D", "6b5c466b5fd3d5dbe8993255381beb476adf299da8fc9b4912eaf7cd2636af6c", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-98db-128m-2k-sp64 ecc4 */
    {"D", "6f5868322c2c34fad09db3e218d94e91434b9c575fcae838a13c628aec69eaf7", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-c821-128m-2k-sp64 ecc4 */
    {"D", "8bac23fc7a5d83c7be80f6e5811574029a9e57e9e28c18f8d5d42543ddda8b4a", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-c811-128m-2k-sp64 ecc4 */
    {"D", "9a018f4ab434d648be6635eced25e03e09d4fca202ad900fba2fbb5df69e2178", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-e521-128m-2k-sp128 ecc4 */
    {"D", "9b1a17170d43e944ae8366e3849e26a470e1126d94b724935df4b70b1c16a944", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-c822-256m-2k-sp64 ecc4 */
    {"D", "a1b0c9dc1610a4fe293cd8f46595c29506258f6c4b9afa717968d9fac3f164df", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-98d2-128m-2k-sp128 ecc8 */
    {"D", "a93196c836a345c8c92b087523c7307555142bc7fa552bf59df6020731bae539", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-c841-128m-2k-sp128 ecc4 */
    {"D", "c58f2e18ca7610fa9f62407b8b3d0ebf8e748bca7594971f24380bd2389acc75", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-c292-128m-2k-sp64 ecc4 */
    {"D", "c990e83496c149f38b166f185df1c58fc3167d7f0ac4094884ba8a0b0cc8658d", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-2c15-128m-2k-sp128 ecc8 */
    {"D", "e2c94d267dbe299df922a54eeed38b94ac69b375f81b2d5d43f942083715c981", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-efbc-128m-2k-sp64 ecc4 */
    {"D", "f129cb6bf8f245afacbb96c19a853a006147a2d9eb3c6adb728156751ef665d9", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-efbf-256m-2k-sp64 ecc4 */
    {"D", "f1e21bacde44e105c9c55101b46227e5869bbd6c3b81272574e438cd394d1e17", "08040000", "NX30_2i5x", 1, 1},  /* h3c-oem-c881-128m-2k-sp128 ecc8 */

    /* Frozen pre.1 baseline records (ABCD archive bcca16ce...). */
    {"A", "8ed8b32594d0ebbe3d5f23a8de1853b9ed3e7492c391361ffaf6f4cf7b1b4abb", "08040001", NULL, 0, 0},
    {"B", "912159169a8b61adb40e572b5366a21da68b65d48fcd47f113e65de44c908589", "08040000", NULL, 0, 0},
    {"C", "57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e", "08040101", NULL, 1, 0},
    /* OEM R012 for the D sample uses this exact productname alias and omits
     * machid from both ENV and Linux cmdline. This narrow absence rule is
     * bound to D's exact candidate and board record; identity is not hardware
     * detection and does not replace the runtime NAND/loader checks. If a
     * machid is present in either source it must still be unique and exact. */
    {"D", "91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362", "08040000", "NX30_2i5x", 1, 1}
};

#define BOARD_A (1u << 0)
#define BOARD_B (1u << 1)
#define BOARD_C (1u << 2)
#define BOARD_D (1u << 3)
#define BOARD_ABCD (BOARD_A|BOARD_B|BOARD_C|BOARD_D)
/* Each row is a precise source-product / observed-product / provider matrix
 * entry. */
static const source_policy sources[] = {
    {"69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44", "RC3000", "RC3000", NULL, BOARD_ABCD},
    {"cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b", "RT3000", "RT3000", NULL, BOARD_ABCD},
    {"5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5", "RW3000", "RW3000", NULL, BOARD_ABCD},
    /* The reviewed NX30 official response is also used for the observed
     * RX3000 identity. RX requires exact productname=RX3000 on any class;
     * NX30/NX30_2i5x remains D-only. No other RX/NX aliases are accepted. */
    {"61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922", "NX30", "RX3000", NULL, BOARD_ABCD},
    {"61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922", "NX30", "NX30", NULL, BOARD_D},
    {"61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922", "NX30", "NX30", "NX30_2i5x", BOARD_D}
};

static int eq(const char *a,const char *b)
{ return a&&b&&!strcmp(a,b); }

static int sha256_text(const char *s)
{
    if(!s||strlen(s)!=64)return 0;
    for(size_t i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}

static int parse_machid(const char *s,char out[16])
{
    const char *p=s;size_t n;
    if(!p||!*p)return -1;
    if(p[0]=='0'&&(p[1]=='x'||p[1]=='X'))p+=2;
    n=strlen(p);if(!n||n>8)return -1;
    for(size_t i=0;i<n;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')||(p[i]>='A'&&p[i]<='F')))return -1;
    unsigned long value;char *end;errno=0;value=strtoul(p,&end,16);
    if(errno||*end||value>0xfffffffful)return -1;
    snprintf(out,16,"%08lx",value);return 0;
}

static uint32_t env_crc(const uint8_t *p)
{
    uint32_t crc=0xffffffffu;
    for(size_t i=4;i<ENV_CRC_BYTES;i++){crc^=p[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&-(int32_t)(crc&1));}
    return ~crc;
}
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int copy_value(char *dst,size_t cap,const uint8_t *p,size_t n)
{ if(n>=cap)return -1;memcpy(dst,p,n);dst[n]=0;return 0; }
static int root_key(const uint8_t *p,size_t n)
{
    static const char *const keys[]={"ubi.mtd=","ubi.block=","root=","rootfstype=","rootflags=","panic="};
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++){size_t k=strlen(keys[i]);if(n>=k&&!memcmp(p,keys[i],k))return 1;}
    return (n==8&&!memcmp(p,"rootwait",8))||(n>=9&&!memcmp(p,"rootwait=",9));
}
static int bootargs_ok(const candidate_policy *policy,const uint8_t *p,size_t n)
{
    size_t at=0;
    while(at<n){size_t start;while(at<n&&(p[at]==' '||p[at]=='\t'||p[at]=='\r'||p[at]=='\n'))at++;if(at==n)break;
        start=at;while(at<n&&p[at]!=' '&&p[at]!='\t'&&p[at]!='\r'&&p[at]!='\n'){
            if(p[at]<0x21||p[at]==0x7f)return 0;
            at++;
        }
        if(!policy->filter_env_root_keys&&root_key(p+start,at-start))return 0;
    }
    return 1;
}
static int env_string(const uint8_t *env,size_t at,size_t end,char *dst,size_t cap)
{
    return copy_value(dst,cap,env+at,end-at);
}

static bii_code check_public_candidate(const char *board,const char *source_product,
                                   const char *official_source_sha256,
                                   const char *machid,const char *candidate_sha256,
                                   const uint8_t *env,size_t env_bytes,
                                   const char *cmdline,bii_result *out,
                                   unsigned runtime_platform_verified)
{
    const candidate_policy *policy=NULL;const source_policy *source=NULL;bii_result result;char expected[16];unsigned board_bit=0,source_context=0;
    unsigned products=0,machids=0,commands=0,args=0;size_t pos=4;
    if(!board||!source_product||!*source_product||strlen(source_product)>=64||
       !sha256_text(official_source_sha256)||!sha256_text(candidate_sha256)||parse_machid(machid,expected))return BII_POLICY;
    if(eq(board,"A"))board_bit=BOARD_A;else if(eq(board,"B"))board_bit=BOARD_B;
    else if(eq(board,"C"))board_bit=BOARD_C;else if(eq(board,"D"))board_bit=BOARD_D;
    if(!board_bit)return BII_POLICY;
    for(size_t i=0;i<sizeof(sources)/sizeof(sources[0]);i++)
        if(eq(official_source_sha256,sources[i].source_sha256)&&eq(source_product,sources[i].source_product)&&
           (sources[i].board_mask&board_bit)){source_context=1;break;}
    if(!source_context)return BII_POLICY;
    for(size_t i=0;i<sizeof(policies)/sizeof(policies[0]);i++)
        if(eq(board,policies[i].board)&&eq(candidate_sha256,policies[i].candidate)){policy=&policies[i];break;}
    if(!policy||!eq(expected,policy->machid))return BII_POLICY;
    if(!env||env_bytes!=ENV_BYTES)return BII_SIZE;
    if(env_crc(env)!=le32(env))return BII_CRC;
    memset(&result,0,sizeof(result));
    while(pos<ENV_CRC_BYTES){size_t end=pos,eqpos=SIZE_MAX;
        if(env[pos]==0)break;
        while(end<ENV_CRC_BYTES&&env[end])end++;
        if(end==ENV_CRC_BYTES)return BII_FORMAT;
        for(size_t i=pos;i<end;i++){
            if(env[i]<0x20||env[i]==0x7f)return BII_FORMAT;
            if(env[i]=='='&&eqpos==SIZE_MAX)eqpos=i;
        }
        if(eqpos==SIZE_MAX||eqpos==pos)return BII_FORMAT;
        size_t keylen=eqpos-pos,vlen=end-eqpos-1;const uint8_t *value=env+eqpos+1;
        if(keylen==11&&!memcmp(env+pos,"productname",11)){
            if(++products!=1||env_string(env,eqpos+1,end,result.product,sizeof(result.product)))return BII_PRODUCT;
        }else if(keylen==6&&!memcmp(env+pos,"machid",6)){
            char found[16];if(++machids!=1||env_string(env,eqpos+1,end,result.machid,sizeof(result.machid))||parse_machid(result.machid,found)||!eq(found,expected))return BII_ENV_MACHID;
        }else if(keylen==7&&!memcmp(env+pos,"bootcmd",7)){
            if(++commands!=1)return BII_FORMAT;
            result.have_bootcmd=1;
            /* Release autoboot ignores this value, but reject control bytes. */
            for(size_t i=0;i<vlen;i++)if(value[i]<0x20||value[i]==0x7f)return BII_FORMAT;
        }else if(keylen==8&&!memcmp(env+pos,"bootargs",8)){
            if(++args!=1||!bootargs_ok(policy,value,vlen))return BII_BOOTARGS;
            result.have_bootargs=1;
        }
        pos=end+1;
    }
    if(products!=1)return BII_PRODUCT;
    for(size_t i=0;i<sizeof(sources)/sizeof(sources[0]);i++)
        if(eq(official_source_sha256,sources[i].source_sha256)&&eq(source_product,sources[i].source_product)&&
           (sources[i].board_mask&board_bit)&&
           (eq(result.product,sources[i].observed_product)||
            (sources[i].env_alias&&eq(result.product,sources[i].env_alias)&&
             policy->env_alias&&eq(sources[i].env_alias,policy->env_alias)))){source=&sources[i];break;}
    if(!source)return BII_PRODUCT;
    if(machids!=1&&!(!machids&&(policy->allow_missing_env_machid||runtime_platform_verified)))return BII_ENV_MACHID;
    if(cmdline){
        const char *p=cmdline;unsigned seen=0;
        while(*p){while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;if(!*p)break;
            const char *start=p;while(*p&&*p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n')p++;
            size_t n=(size_t)(p-start);
            if(n>=7&&!memcmp(start,"machid=",7)){
                char found[16],token[16];size_t m=n-7;
                if(++seen!=1||m>=sizeof(token)||copy_value(token,sizeof(token),(const uint8_t*)start+7,m)||parse_machid(token,found)||!eq(found,expected))return BII_CMDLINE_MACHID;
            }
        }
    }
    memcpy(result.board,policy->board,strlen(policy->board)+1);
    memcpy(result.machid,expected,strlen(expected)+1);
    result.trial_only=1;if(out)*out=result;return BII_OK;
}

bii_code bii_check_public_candidate(const char *board,const char *source_product,
                                   const char *official_source_sha256,
                                   const char *machid,const char *candidate_sha256,
                                   const uint8_t *env,size_t env_bytes,
                                   const char *cmdline,bii_result *out)
{
    return check_public_candidate(board,source_product,official_source_sha256,machid,
        candidate_sha256,env,env_bytes,cmdline,out,0);
}

/* The supplied RC R013L01/C stock ENV has no machid. Do not change the
 * candidate's missing-field policy: substitute only this exact source's live
 * C route, already bound by the bridge/source/dispatcher contracts. Neither
 * an absent route nor an A/B/D route grants this platform assertion. */
static unsigned rc_c_runtime_platform(const char *board,const char *product,
        const char *source,const char *machid,const char *candidate,
        const uint8_t *compatible,size_t bytes)
{
    static const uint8_t exact_route[]="qcom,ipq5018-mp03.1-c2\0qcom,ipq5018";
    char normalized[16];
    return eq(board,"C")&&eq(product,"RC3000")&&
        eq(source,"69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44")&&
        eq(candidate,"e768be512e985980070c811cea56f44a822bee3a938bb91f821454764e89cd84")&&
        !parse_machid(machid,normalized)&&eq(normalized,"08040101")&&
        compatible&&bytes==sizeof(exact_route)&&!memcmp(compatible,exact_route,sizeof(exact_route));
}

bii_code bii_check_public_candidate_runtime(const char *board,const char *source_product,
                                   const char *official_source_sha256,
                                   const char *machid,const char *candidate_sha256,
                                   const uint8_t *env,size_t env_bytes,
                                   const char *cmdline,const uint8_t *compatible,
                                   size_t compatible_bytes,bii_result *out)
{
    return check_public_candidate(board,source_product,official_source_sha256,machid,
        candidate_sha256,env,env_bytes,cmdline,out,
        rc_c_runtime_platform(board,source_product,official_source_sha256,machid,candidate_sha256,compatible,compatible_bytes));
}

int bii_return_env_product_allowed(const char *official_source_sha256,
                                   const char *source_product,
                                   const char *route,
                                   const char *observed_product)
{
    if(!sha256_text(official_source_sha256)||!source_product||!route||!observed_product)
        return 0;
    for(size_t i=0;i<sizeof(sources)/sizeof(sources[0]);i++){
        const source_policy *source=&sources[i];
        if(!eq(official_source_sha256,source->source_sha256)||
           !eq(source_product,source->source_product)||
           !(eq(observed_product,source->observed_product)||
             (source->env_alias&&eq(observed_product,source->env_alias))))continue;
        if(!(eq(route,"config@mp02.1")||eq(route,"config@mp03.1")||
             eq(route,"config@mp03.1-c2")))continue;
        /* NX and NX30_2i5x are D-only. The reviewed RX identity for the same
         * exact NX source is explicitly admitted on all four classes. */
        if(source->board_mask==BOARD_D&&!eq(route,"config@mp02.1"))continue;
        return 1;
    }
    return 0;
}

const char *bii_code_name(bii_code c)
{
    static const char *const names[]={"ok","policy","size","crc","format","product","env-machid","cmdline-machid","bootargs"};
    return (unsigned)c<sizeof(names)/sizeof(names[0])?names[c]:"unknown";
}
