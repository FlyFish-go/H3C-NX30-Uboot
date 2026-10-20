"""Truthful immutable source expectations, not approval or BOOTCONFIG evidence."""
if not __debug__:
    raise RuntimeError('return source generation requires unoptimized Python')
import hashlib
import json
import re
import bridge_kernel
import vendor_fit

ROUTE = 'config@mp02.1'
COMPATIBLE = bridge_kernel.ROUTES[ROUTE][1]
NAND_ROOTS = ('/soc/qpic-nand@79b0000', '/soc/pinctrl@1000000/qspi_nand_pins')
NAND_NODES = frozenset((NAND_ROOTS[0], NAND_ROOTS[0] + '/nandcs@0', NAND_ROOTS[1],
    *(NAND_ROOTS[1] + '/' + n for n in ('qspi_clock', 'qspi_cs', 'qspi_data_0', 'qspi_data_1', 'qspi_data_2', 'qspi_data_3'))))
NAND_SHA256 = '8a59fdb5d6864a98fa833d0fa56aa615a13acc99038c459d7e0039fed460acfd'
PRODUCTS = {
    '61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922': 1,
    '69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44': 2,
    'cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b': 3,
    '5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5': 4,
}
HASH_FIELDS = ('official_sha256', 'stock_kernel_sha256', 'bridge_kernel_sha256',
               'compressed_kernel_sha256', 'nand_dt_sha256')
FIELDS = frozenset(('schema', *HASH_FIELDS, 'product', 'route', 'compatible'))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('ascii')


def nand_closure(nodes):
    selected = {path: {key: value.hex() for key, value in props.items()}
                for path, props in nodes.items()
                if any(path == root or path.startswith(root + '/') for root in NAND_ROOTS)}
    if set(selected) != NAND_NODES:
        raise ValueError('missing or unexpected NAND/pinctrl closure nodes')
    return canonical(selected)


def route_data(nodes, route):
    selected = bridge_kernel.ROUTES.get(route)
    if selected is None:
        raise ValueError('return route is not an exact reviewed bridge_kernel.ROUTES key')
    fdt_name, compatible, _ = selected
    config = nodes.get('/configurations/' + route)
    if (config is None or config.get('kernel') != b'kernel@1\0' or
            config.get('fdt') != fdt_name.encode('ascii') + b'\0'):
        raise ValueError('missing or conflicting selected return route')
    kernel = nodes.get('/images/kernel@1', {}).get('data')
    dt = nodes.get('/images/' + fdt_name, {}).get('data')
    if not isinstance(kernel, bytes) or not kernel or not isinstance(dt, bytes):
        raise ValueError('selected return route lacks internal kernel/DT bytes')
    dt_nodes = vendor_fit.fdt_props(dt)
    if dt_nodes.get('/', {}).get('compatible') != compatible.encode('ascii') + b'\0qcom,ipq5018\0':
        raise ValueError('selected return route compatible mismatch')
    return kernel, nand_closure(dt_nodes)


def metadata(stock_fit, bridge_fit, official_sha256, route=ROUTE):
    if not isinstance(route, str) or route not in bridge_kernel.ROUTES:
        raise ValueError('selected return route must be an exact bridge_kernel.ROUTES key')
    source = bridge_kernel.SOURCES.get(official_sha256)
    if source is None or official_sha256 not in PRODUCTS or sha(stock_fit) != source[1]:
        raise ValueError('return source does not match exact official stock FIT pin')
    stock = bridge_kernel._verify(stock_fit)
    bridge = bridge_kernel._verify(bridge_fit)
    expected_bridge, _ = bridge_kernel.transform_kernel(stock_fit, official_sha256)
    if bridge_fit != expected_bridge:
        raise ValueError('return source bridge FIT is not the exact reviewed transformation')
    original_kernel, original_nand = route_data(stock, route)
    transformed_kernel, transformed_nand = route_data(bridge, route)
    if original_kernel != transformed_kernel:
        raise ValueError('compressed kernel bytes changed')
    if original_nand != transformed_nand or sha(original_nand) != NAND_SHA256:
        raise ValueError('NAND/pinctrl closure changed or unreviewed')
    result = dict(schema=1, official_sha256=official_sha256, stock_kernel_sha256=sha(stock_fit),
                  bridge_kernel_sha256=sha(bridge_fit), compressed_kernel_sha256=sha(original_kernel),
                  nand_dt_sha256=sha(original_nand), product=PRODUCTS[official_sha256],
                  route=route, compatible=bridge_kernel.ROUTES[route][1])
    result['revision_sha256'] = sha(canonical(result))
    return result


def validate_metadata(value):
    if not isinstance(value, dict) or set(value) != FIELDS | {'revision_sha256'}:
        raise ValueError('exact return source metadata schema required')
    if type(value['schema']) is not int or value['schema'] != 1 or type(value['product']) is not int:
        raise ValueError('invalid source schema/product')
    for key in (*HASH_FIELDS, 'revision_sha256'):
        if not isinstance(value[key], str) or not re.fullmatch('[0-9a-f]{64}', value[key]) or value[key] == '0' * 64:
            raise ValueError('invalid exact source digest')
    route = value['route'] if isinstance(value['route'], str) else None
    if (value['official_sha256'] not in PRODUCTS or value['product'] != PRODUCTS[value['official_sha256']] or
        value['stock_kernel_sha256'] != bridge_kernel.SOURCES[value['official_sha256']][1] or
        value['nand_dt_sha256'] != NAND_SHA256 or route not in bridge_kernel.ROUTES or
        value['compatible'] != bridge_kernel.ROUTES[route][1]):
        raise ValueError('unreviewed source identity/route')
    if value['revision_sha256'] != sha(canonical({k: v for k, v in value.items() if k != 'revision_sha256'})):
        raise ValueError('return source revision mismatch')


def c_header(value):
    validate_metadata(value)
    mapping = {'official_sha256': 'OFFICIAL_SHA256', 'stock_kernel_sha256': 'STOCK_KERNEL_SHA256',
               'bridge_kernel_sha256': 'BRIDGE_KERNEL_SHA256', 'compressed_kernel_sha256': 'COMPRESSED_KERNEL_SHA256',
               'nand_dt_sha256': 'NAND_DT_SHA256', 'revision_sha256': 'REVISION_SHA256',
               'product': 'PRODUCT', 'route': 'ROUTE', 'compatible': 'COMPATIBLE'}
    lines = ['/* Source expectation only: not reviewed approval, BC evidence, or hardware identity. */',
             '#ifndef BOOTBRIDGE_RETURN_SOURCE_H', '#define BOOTBRIDGE_RETURN_SOURCE_H']
    for key, macro in mapping.items():
        text = str(value[key]) if key == 'product' else json.dumps(value[key], ensure_ascii=True)
        lines.append('#define BI_RETURN_SOURCE_' + macro + ' ' + text)
    return '\n'.join(lines + ['#endif', ''])


def c_registry_header(values, components_by_official):
    """Emit an immutable four-source registry for the standalone return service.

    Rows describe exact official/bridge source pairs. They carry no OEM pool,
    SBL, ENV, NAND write, or hardware qualification by themselves.
    """
    if not isinstance(values, (list, tuple)) or not 1 <= len(values) <= 3:
        raise ValueError('source-bound return helper requires one exact source and its reviewed routes')
    if not isinstance(components_by_official, dict):
        raise ValueError('exact official bootchain component records are required')
    for value in values:
        validate_metadata(value)
    sources = {v['official_sha256'] for v in values}
    if len(sources) != 1 or len({v['product'] for v in values}) != 1 or set(components_by_official) != sources or len({(v['official_sha256'],v['route']) for v in values}) != len(values):
        raise ValueError('registry must bind one official source and unique route rows')
    lines = [
        '/* Source-pair expectations only. This table grants no install or return authority. */',
        '#ifndef BOOTBRIDGE_RETURN_SOURCE_REGISTRY_H',
        '#define BOOTBRIDGE_RETURN_SOURCE_REGISTRY_H',
        '#include "bridge_oem_return.h"',
        'static const br_return_source_record bootbridge_return_sources[] = {'
    ]
    for value in sorted(values, key=lambda row:(row['product'],row['route'])):
        product = value['product']
        fields = [value[k] for k in ('official_sha256', 'stock_kernel_sha256',
                  'bridge_kernel_sha256', 'revision_sha256', 'compressed_kernel_sha256',
                  'nand_dt_sha256', 'route', 'compatible')]
        components = components_by_official[value['official_sha256']]
        if (not isinstance(components, dict) or
            not re.fullmatch('[0-9a-f]{64}', str(components.get('sbl_sha256', ''))) or
            not re.fullmatch('[0-9a-f]{64}', str(components.get('appsbl_padded_sha256', ''))) or
            type(components.get('sbl_valid_bytes')) is not int or not 0 < components['sbl_valid_bytes'] < 0x80000 or
            type(components.get('appsbl_valid_bytes')) is not int or not 0 < components['appsbl_valid_bytes'] <= 0x140000):
            raise ValueError('invalid source-bound SBL/APPSBL component record')
        fields += [components['sbl_sha256'], components['appsbl_padded_sha256']]
        lines.append('  {' + ','.join(json.dumps(x, ensure_ascii=True) for x in fields) +
                     f',{components["sbl_valid_bytes"]}u,{components["appsbl_valid_bytes"]}u,{product}u}},')
    lines += ['};', '#define BI_RETURN_SOURCES bootbridge_return_sources',
              '#define BI_RETURN_SOURCE_COUNT (sizeof(bootbridge_return_sources)/sizeof(bootbridge_return_sources[0]))',
              '#endif', '']
    return '\n'.join(lines)


def c_return_contract_header(values, components_by_official, catalog_profiles,
                             source_table_mask=0x11, exit_policy=1):
    """Build a source/profile return registry from exact package pins.

    Evidence digests identify the reproducible software contract only; they do
    not encode or imply any physical hardware pass.

    Policy 1 is the independent standalone OEM-return helper. Policy 2 is a
    normal install provider's source-bound return action, which additionally
    binds the installed candidate's OEM handoff contract.
    """
    if type(exit_policy) is not int or exit_policy not in (1, 2):
        raise ValueError('unsupported source-bound return exit policy')
    sources_header = c_registry_header(values, components_by_official)
    if not isinstance(catalog_profiles, list) or not catalog_profiles:
        raise ValueError('exact generated NAND catalog profiles are required')
    rows = []
    evidence_domain = ('BOOTBRIDGE-OEM-RETURN-SOFTWARE-CONTRACT-V1' if exit_policy == 1 else
                       'BOOTBRIDGE-OEM-RETURN-INSTALL-PROVIDER-SOFTWARE-CONTRACT-V2')
    for value in values:
        for profile in catalog_profiles:
            if not isinstance(profile, dict):
                raise ValueError('invalid NAND catalog profile row')
            if not (profile.get('appsbl_source_mask', 0) & source_table_mask):
                continue
            for ecc, source_key, mode_bit in ((4, 'appsbl_ecc4_source_mask', 1),
                                              (8, 'appsbl_ecc8_source_mask', 2)):
                if not (profile.get(source_key, 0) & source_table_mask):
                    continue
                if not (profile.get('kernel_ecc_mode_mask', 0) & mode_bit):
                    continue
                # Linux MTD OOB is pinned independently from physical raw
                # spare and QPIC-normalized per-codeword OOB. The catalog's
                # exact OEM field mask enumerates supported Linux values.
                for oob, oob_field in ((64, 1), (128, 2), (256, 4)):
                    if not (profile.get('kernel_oem_oob_field_mask', 0) & oob_field):
                        continue
                    contract = {
                        'domain': evidence_domain,
                        'source_revision_sha256': value['revision_sha256'],
                        'source_sha256': value['official_sha256'],
                        'product': value['product'], 'route': value['route'],
                        'compatible': value['compatible'], 'nand_profile': profile['profile_id'],
                        'ecc_bits': ecc, 'raw_spare_bytes': profile.get('raw_spare_bytes'),
                        'mtd_oob_bytes': oob,
                        'bc_write_policy': 'reviewed-linux-mtd-copy-preserving-v1',
                        'env_policy': 'crc-product-bootipq-no-root-override-v1',
                        'exit_policy': exit_policy,
                        'hardware_validation': 'NOT_EXECUTED'
                    }
                    evidence = sha(canonical(contract))
                    rows.append((value, profile, ecc, oob, evidence))
    if not rows or len(rows) > 2048:
        raise ValueError('return source/profile rule count is empty or exceeds 2048')
    lines = [sources_header.rstrip(), 'static const br_return_rule bootbridge_return_rules[] = {']
    for idx, (value, profile, ecc, oob, evidence) in enumerate(rows, 1):
        components = components_by_official[value['official_sha256']]
        fields = [value['official_sha256'], value['stock_kernel_sha256'],
                  value['bridge_kernel_sha256'], value['revision_sha256'],
                  value['route'], value['compatible'], profile['profile_id']]
        compressed = value['compressed_kernel_sha256']
        nand_dt = value['nand_dt_sha256']
        evidence_fields = [
            f'{idx}u', str(value['product']) + 'u',
            ('BR_RETURN_EXIT_SOURCE_BOOTIPQ_V1' if exit_policy == 1 else
             'BR_RETURN_EXIT_INSTALLED_CANDIDATE_BOOTIPQ_V2'),
            *[json.dumps(x, ensure_ascii=True) for x in fields],
            f'{ecc}u', f'{oob}u', 'BR_RETURN_BC_WRITE_REVIEWED_LINUX_MTD_V1',
            'BR_RETURN_ENV_EXPLICIT_BOOTIPQ_V1', json.dumps(compressed),
            json.dumps(nand_dt), json.dumps(evidence),
        ]
        lines.append('  {' + ','.join(evidence_fields) + '},')
    lines += ['};', '#define BI_RETURN_RULES bootbridge_return_rules',
              '#define BI_RETURN_RULE_COUNT (sizeof(bootbridge_return_rules)/sizeof(bootbridge_return_rules[0]))',
              '']
    return '\n'.join(lines)
