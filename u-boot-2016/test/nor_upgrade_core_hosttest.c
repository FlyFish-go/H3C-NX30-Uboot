/* SPDX-License-Identifier: GPL-2.0 */
/* Compile with: cc -std=c99 -Wall -Wextra -DNX30_NOR_HOST_TEST -I failsafe/modules -o /tmp/nor-core-test test/nor_upgrade_core_hosttest.c */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "../failsafe/modules/nor_upgrade_core.c"

#define TEST_PACKAGE_SIZE (NX30_NOR_WRAP_HEADER_SIZE + 256U)
#define TEST_FLASH_SIZE NX30_NOR_FLASH_SIZE

struct mock_flash {
	uint8_t bytes[TEST_FLASH_SIZE];
	unsigned int verify_calls;
	unsigned int erase_calls;
	unsigned int write_calls;
	unsigned int read_calls;
	unsigned int fail_erase_call;
	unsigned int fail_write_call;
	unsigned int fail_read_call;
	int layout_error;
	uint32_t actual_size;
	uint32_t requested_size;
	uint32_t corrupt_readback_offset;
	uint8_t bootconfig_state[64];
	uint8_t bootconfig_before[64];
	uint32_t erased[4];
};

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = v; p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void fake_digest(const void *data, u32 size, u8 digest[32])
{
	const u8 *p = data;
	u32 i;
	memset(digest, 0, 32);
	for (i = 0; i < size; i++)
		digest[i % 32] = (u8)(digest[i % 32] + p[i] + (u8)i);
}

static int verify_layout(void *ctx, u32 requested_nor_size)
{
	struct mock_flash *m = ctx;
	m->verify_calls++;
	m->requested_size = requested_nor_size;
	if (m->layout_error)
		return m->layout_error;
	return m->actual_size == requested_nor_size ? 0 : -EXDEV;
}

static int erase_slot(void *ctx, u32 offset, u32 size)
{
	struct mock_flash *m = ctx;
	unsigned int call = ++m->erase_calls;
	if (call <= 4) m->erased[call - 1] = offset;
	if (m->fail_erase_call == call) return -EIO;
	if (size != NX30_NOR_SLOT_SIZE) return -EINVAL;
	memset(m->bytes + offset, 0xff, size);
	return 0;
}

static int write_bytes(void *ctx, u32 offset, const void *data, u32 size)
{
	struct mock_flash *m = ctx;
	if (++m->write_calls == m->fail_write_call) return -EIO;
	memcpy(m->bytes + offset, data, size);
	return 0;
}

static int read_bytes(void *ctx, u32 offset, void *data, u32 size)
{
	struct mock_flash *m = ctx;
	unsigned int call = ++m->read_calls;
	if (call == m->fail_read_call) return -EIO;
	memcpy(data, m->bytes + offset, size);
	if (m->corrupt_readback_offset == offset)
		((u8 *)data)[0] ^= 0x80;
	return 0;
}

static const struct nx30_nor_flash_ops mock_ops = {
	.verify_layout_and_bootconfig = verify_layout,
	.erase = erase_slot,
	.write = write_bytes,
	.read = read_bytes,
};

static void make_package(u8 package[TEST_PACKAGE_SIZE])
{
	u8 *elf = package + NX30_NOR_WRAP_HEADER_SIZE;
	u8 digest[32];
	memset(package, 0, TEST_PACKAGE_SIZE);
	memcpy(package, "NX30NOR1", 8);
	put32(package + 8, 1);
	put32(package + 12, NX30_NOR_WRAP_HEADER_SIZE);
	put32(package + 16, TEST_PACKAGE_SIZE - NX30_NOR_WRAP_HEADER_SIZE);
	put32(package + 20, NX30_NOR_LAYOUT_ID);
	put32(package + 24, NX30_NOR_PRIMARY_OFFSET);
	put32(package + 28, NX30_NOR_BACKUP_OFFSET);
	put32(package + 32, NX30_NOR_SLOT_SIZE);
	put32(package + 36, NX30_NOR_FLASH_SIZE_16M);
	memcpy(elf, "\177ELF", 4);
	elf[4] = 1; elf[5] = 1; elf[6] = 1;
	put16(elf + 16, 2); put16(elf + 18, 40); put32(elf + 20, 1);
	put32(elf + 24, 0x4a920001U); put32(elf + 28, 52);
	put16(elf + 40, 52); put16(elf + 42, 32); put16(elf + 44, 1);
	put32(elf + 52, 1); put32(elf + 56, 0); put32(elf + 60, 0x4a920000U);
	put32(elf + 64, 0x4a920000U); put32(elf + 68, 256); put32(elf + 72, 256);
	put32(elf + 76, 5); put32(elf + 80, 4);
	memcpy(elf + 96, NX30_NOR_MARKER, NX30_NOR_MARKER_SIZE);
	fake_digest(elf, 256, digest);
	memcpy(package + 40, digest, 32);
}

static void check_outside_slots_unchanged(const struct mock_flash *m, uint8_t expected)
{
	u32 i;
	for (i = 0; i < TEST_FLASH_SIZE; i++) {
		if ((i >= NX30_NOR_BACKUP_OFFSET &&
		     i < NX30_NOR_BACKUP_OFFSET + NX30_NOR_SLOT_SIZE) ||
		    (i >= NX30_NOR_PRIMARY_OFFSET &&
		     i < NX30_NOR_PRIMARY_OFFSET + NX30_NOR_SLOT_SIZE))
			continue;
		if (m->bytes[i] != expected) {
			fprintf(stderr, "FAIL: out-of-slot byte changed at 0x%x\n", i);
			exit(1);
		}
	}
}

static void check(int ok, const char *name)
{
	if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
}

int main(void)
{
	u8 package[TEST_PACKAGE_SIZE];
	struct nx30_nor_image image;
	struct mock_flash *m = calloc(1, sizeof(*m));
	int ret;
	check(m != NULL, "allocate mock flash");
	make_package(package);
	check(!nx30_nor_validate_package(package, sizeof(package), fake_digest, &image), "valid package");
	check(image.payload_size == 256, "payload length");
	check(image.nor_size == NX30_NOR_FLASH_SIZE_16M, "requested NOR size");
	put32(package + 36, 0x00c00000U);
	check(nx30_nor_validate_package(package, sizeof(package), fake_digest, &image) != 0,
	      "reject unsupported NOR capacity");
	put32(package + 36, NX30_NOR_FLASH_SIZE_16M);
	put32(package + 36, NX30_NOR_FLASH_SIZE_8M);
	check(!nx30_nor_validate_package(package, sizeof(package), fake_digest, &image) &&
	      image.nor_size == NX30_NOR_FLASH_SIZE_8M, "accept supported 8 MiB capacity");
	put32(package + 36, NX30_NOR_FLASH_SIZE_16M);
	check(!nx30_nor_validate_package(package, sizeof(package), fake_digest, &image),
	      "restore valid 16 MiB package");

	package[20] ^= 1;
	check(nx30_nor_validate_package(package, sizeof(package), fake_digest, &image) != 0, "reject wrong layout id");
	package[20] ^= 1;
	package[75] = 1;
	check(nx30_nor_validate_package(package, sizeof(package), fake_digest, &image) != 0, "reject nonzero reserved byte");
	package[75] = 0;
	package[NX30_NOR_WRAP_HEADER_SIZE + 96] ^= 1;
	check(nx30_nor_validate_package(package, sizeof(package), fake_digest, &image) == -EBADMSG, "reject bad payload hash");
	package[NX30_NOR_WRAP_HEADER_SIZE + 96] ^= 1;
	package[NX30_NOR_WRAP_HEADER_SIZE + 96] = 'X';
	fake_digest(package + NX30_NOR_WRAP_HEADER_SIZE, 256, package + 40);
	check(nx30_nor_validate_package(package, sizeof(package), fake_digest, &image) != 0, "reject foreign marker");
	make_package(package);
	check(!nx30_nor_validate_package(package, sizeof(package), fake_digest, &image), "valid package restored");

	memset(m, 0, sizeof(*m));
	m->actual_size = NX30_NOR_FLASH_SIZE_16M;
	memset(m->bytes, 0x5a, sizeof(m->bytes));
	memset(m->bootconfig_state, 0xc3, sizeof(m->bootconfig_state));
	memcpy(m->bootconfig_before, m->bootconfig_state, sizeof(m->bootconfig_state));
	ret = nx30_nor_write_both(&image, &mock_ops, m);
	check(!ret && m->verify_calls == 1, "complete update");
	check(m->requested_size == NX30_NOR_FLASH_SIZE_16M, "passes requested NOR size to backend");
	check(m->erase_calls == 2 && m->write_calls == 2, "two erase/write operations");
	check(m->erased[0] == NX30_NOR_BACKUP_OFFSET && m->erased[1] == NX30_NOR_PRIMARY_OFFSET, "backup then primary order");
	check(!memcmp(m->bytes + NX30_NOR_BACKUP_OFFSET, m->bytes + NX30_NOR_PRIMARY_OFFSET, NX30_NOR_SLOT_SIZE), "identical padded copies");
	check_outside_slots_unchanged(m, 0x5a);
	check(!memcmp(m->bootconfig_state, m->bootconfig_before, sizeof(m->bootconfig_state)), "BOOTCONFIG remains unchanged");

	memset(m, 0, sizeof(*m)); m->actual_size = NX30_NOR_FLASH_SIZE_8M; memset(m->bytes, 0x5a, sizeof(m->bytes));
	ret = nx30_nor_write_both(&image, &mock_ops, m);
	check(ret == -EXDEV && m->verify_calls == 1 && !m->erase_calls && !m->write_calls,
	      "actual chip capacity mismatch writes nothing");
	check_outside_slots_unchanged(m, 0x5a);
	memset(m, 0, sizeof(*m)); m->actual_size = NX30_NOR_FLASH_SIZE_16M; memset(m->bytes, 0x5a, sizeof(m->bytes)); m->layout_error = -EXDEV;
	ret = nx30_nor_write_both(&image, &mock_ops, m);
	check(ret == -EXDEV && !m->erase_calls && !m->write_calls, "layout failure writes nothing");
	check_outside_slots_unchanged(m, 0x5a);
	memset(m, 0, sizeof(*m)); m->actual_size = NX30_NOR_FLASH_SIZE_16M; memset(m->bytes, 0x5a, sizeof(m->bytes)); m->fail_erase_call = 1;
	ret = nx30_nor_write_both(&image, &mock_ops, m);
	check(ret == -EIO && m->erase_calls == 1 && !m->write_calls, "backup erase failure stops");
	check_outside_slots_unchanged(m, 0x5a);
	memset(m, 0, sizeof(*m)); m->actual_size = NX30_NOR_FLASH_SIZE_16M; memset(m->bytes, 0x5a, sizeof(m->bytes)); m->fail_write_call = 1;
	ret = nx30_nor_write_both(&image, &mock_ops, m);
	check(ret == -EIO && m->write_calls == 1 && m->erase_calls == 1, "backup write failure stops");
	check_outside_slots_unchanged(m, 0x5a);
	memset(m, 0, sizeof(*m)); m->actual_size = NX30_NOR_FLASH_SIZE_16M; memset(m->bytes, 0x5a, sizeof(m->bytes)); m->corrupt_readback_offset = NX30_NOR_BACKUP_OFFSET;
	ret = nx30_nor_write_both(&image, &mock_ops, m);
	check(ret == -EIO && m->erase_calls == 1 && m->write_calls == 1, "backup readback mismatch stops before primary");
	check_outside_slots_unchanged(m, 0x5a);
	memset(m, 0, sizeof(*m)); m->actual_size = NX30_NOR_FLASH_SIZE_16M; memset(m->bytes, 0x5a, sizeof(m->bytes)); m->fail_read_call = 1;
	ret = nx30_nor_write_both(&image, &mock_ops, m);
	check(ret == -EIO && m->erase_calls == 1 && m->write_calls == 1, "read error stops before primary");
	check_outside_slots_unchanged(m, 0x5a);
	memset(m, 0, sizeof(*m)); m->actual_size = NX30_NOR_FLASH_SIZE_16M; memset(m->bytes, 0x5a, sizeof(m->bytes)); m->fail_erase_call = 2;
	ret = nx30_nor_write_both(&image, &mock_ops, m);
	check(ret == -EIO && m->erase_calls == 2 && m->write_calls == 1, "primary erase failure stops after verified backup");
	check(!memcmp(m->bytes + NX30_NOR_BACKUP_OFFSET, image.payload, image.payload_size), "backup remains valid after primary erase failure");
	check_outside_slots_unchanged(m, 0x5a);
	memset(m, 0, sizeof(*m)); m->actual_size = NX30_NOR_FLASH_SIZE_16M; memset(m->bytes, 0x5a, sizeof(m->bytes)); m->fail_write_call = 2;
	ret = nx30_nor_write_both(&image, &mock_ops, m);
	check(ret == -EIO && m->erase_calls == 2 && m->write_calls == 2, "primary write failure stops");
	check_outside_slots_unchanged(m, 0x5a);
	memset(m, 0, sizeof(*m)); m->actual_size = NX30_NOR_FLASH_SIZE_16M; memset(m->bytes, 0x5a, sizeof(m->bytes)); m->corrupt_readback_offset = NX30_NOR_PRIMARY_OFFSET;
	ret = nx30_nor_write_both(&image, &mock_ops, m);
	check(ret == -EIO && m->erase_calls == 2 && m->write_calls == 2, "primary readback mismatch stops");
	check_outside_slots_unchanged(m, 0x5a);
	free(m);
	puts("NOR_UPGRADE_CORE_HOSTTEST_PASS");
	return 0;
}
