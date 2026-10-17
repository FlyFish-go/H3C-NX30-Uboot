#!/usr/bin/env python3
"""Host regression for the RT3000-B UARTDM zero-word recovery path.

This compiles the production static functions extracted verbatim from
 drivers/serial/qca_uart.c against mocked MMIO. It does not duplicate their
 control flow in the test.
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "drivers/serial/qca_uart.c"


def extract_function(source, name):
    start = source.index(name + "(")
    start = source.rfind("static unsigned int", 0, start)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise AssertionError(f"unterminated function {name}")


source = SOURCE.read_text()
functions = "\n\n".join(
    extract_function(source, name)
    for name in ("msm_boot_uart_dm_init_rx_transfer", "msm_boot_uart_dm_read")
)

harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define FIFO_DATA_SIZE 4
#define MSM_BOOT_UART_DM_E_SUCCESS 0
#define MSM_BOOT_UART_DM_E_INVAL 3
#define MSM_BOOT_UART_DM_E_RX_NOT_READY 5
#define MSM_BOOT_UART_DM_RXSTALE (1u << 3)
#define MSM_BOOT_UART_DM_SR_RXFULL (1u << 1)
#define MSM_BOOT_UART_DM_SR_UART_OVERRUN (1u << 4)
#define MSM_BOOT_UART_DM_RX_STALE_TIMEOUT (2u << 10)
#define MSM_BOOT_UART_DM_CMD_RESET_ERR_STAT 0x30
#define MSM_BOOT_UART_DM_CMD_RESET_RX 0x10
#define MSM_BOOT_UART_DM_CR_RX_ENABLE 0x01
#define MSM_BOOT_UART_DM_DMRX_DEF_VALUE 0x220
#define MSM_BOOT_UART_DM_CMD_RES_STALE_INT 0x80
#define MSM_BOOT_UART_DM_GCMD_ENA_STALE_EVT 0x500
#define MSM_BOOT_UART_DM_MISR(base) ((base) + 0x10)
#define MSM_BOOT_UART_DM_SR(base) ((base) + 0x08)
#define MSM_BOOT_UART_DM_CR(base) ((base) + 0x10)
#define MSM_BOOT_UART_DM_DMRX(base) ((base) + 0x34)
#define MSM_BOOT_UART_DM_RX_TOTAL_SNAP(base) ((base) + 0x38)
#define MSM_BOOT_UART_DM_RF(base, x) ((base) + 0x70 + (4 * (x)))
#define BASE 0x1000ul

static uint32_t misr, status, snap, rf_word;
static unsigned int writes[16], write_count, rf_reads, dmrx_write_count;
static uint32_t readl(unsigned long addr)
{
    if (addr == MSM_BOOT_UART_DM_MISR(BASE)) return misr;
    if (addr == MSM_BOOT_UART_DM_SR(BASE)) return status;
    if (addr == MSM_BOOT_UART_DM_RX_TOTAL_SNAP(BASE)) return snap;
    if (addr == MSM_BOOT_UART_DM_RF(BASE, 0)) { rf_reads++; return rf_word; }
    return 0;
}
static void writel(uint32_t value, unsigned long addr)
{
    if (addr == MSM_BOOT_UART_DM_CR(BASE)) {
        assert(write_count < sizeof(writes) / sizeof(writes[0]));
        writes[write_count++] = value;
        if (value == MSM_BOOT_UART_DM_CMD_RES_STALE_INT)
            misr &= ~MSM_BOOT_UART_DM_RXSTALE;
    } else if (addr == MSM_BOOT_UART_DM_DMRX(BASE)) {
        assert(value == MSM_BOOT_UART_DM_DMRX_DEF_VALUE);
        dmrx_write_count++;
        snap = 0;
    }
}
static void clear_writes(void) { write_count = 0; }
static int has_write(unsigned int value)
{
    unsigned int i;
    for (i = 0; i < write_count; i++) if (writes[i] == value) return 1;
    return 0;
}

__FUNCTIONS__

int main(void)
{
    (void)has_write;
    unsigned int data = 0;
    int count = 0;
    unsigned int ret;

    /* Stale transfer whose RF word is zero: discard and fully rearm RX. */
    misr = MSM_BOOT_UART_DM_RXSTALE;
    status = 0;
    snap = 1;
    rf_word = 0;
    clear_writes();
    ret = msm_boot_uart_dm_read(&data, &count, 0, BASE);
    assert(ret == MSM_BOOT_UART_DM_E_RX_NOT_READY);
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
    assert(has_write(MSM_BOOT_UART_DM_CMD_RESET_ERR_STAT));
    assert(has_write(MSM_BOOT_UART_DM_CMD_RESET_RX));
    assert(has_write(MSM_BOOT_UART_DM_CR_RX_ENABLE));
    assert(dmrx_write_count > 0);
    assert(has_write(MSM_BOOT_UART_DM_CMD_RES_STALE_INT));
    assert(has_write(MSM_BOOT_UART_DM_GCMD_ENA_STALE_EVT));
#else
    assert(write_count == 0);
#endif

    /* A subsequent one-byte Ctrl-C is seen as a fresh transfer. */
    misr = MSM_BOOT_UART_DM_RXSTALE;
    snap = 1;
    rf_word = 0x03;
    clear_writes();
    ret = msm_boot_uart_dm_read(&data, &count, 0, BASE);
    assert(ret == MSM_BOOT_UART_DM_E_SUCCESS);
    assert(data == 0x03 && count == 1);
    assert(dmrx_write_count > 0);

    /* A no-stale nonblocking poll remains a no-op. */
    misr = 0;
    status = 0;
    clear_writes();
    ret = msm_boot_uart_dm_read(&data, &count, 0, BASE);
    assert(ret == MSM_BOOT_UART_DM_E_RX_NOT_READY);
    assert(write_count == 0);

    /* Normal multiword transfers retain their count/rearm behavior. */
    misr = MSM_BOOT_UART_DM_RXSTALE;
    snap = 8;
    rf_word = 0x44332211;
    clear_writes();
    ret = msm_boot_uart_dm_read(&data, &count, 0, BASE);
    assert(ret == MSM_BOOT_UART_DM_E_SUCCESS && count == 4);
    assert(write_count == 0);
    misr = MSM_BOOT_UART_DM_RXSTALE;
    rf_word = 0x88776655;
    ret = msm_boot_uart_dm_read(&data, &count, 0, BASE);
    assert(ret == MSM_BOOT_UART_DM_E_SUCCESS && count == 4);
    assert(dmrx_write_count > 0);

    puts("qca_uart_rx_zero_test: PASS");
    return 0;
}
""".replace("__FUNCTIONS__", "static unsigned int msm_boot_uart_dm_init(unsigned long base) { (void)base; return MSM_BOOT_UART_DM_E_SUCCESS; }\n\n" + functions)

with tempfile.TemporaryDirectory() as temp:
    c_file = pathlib.Path(temp) / "qca_uart_rx_zero_test.c"
    c_file.write_text(harness)
    for target_macro in ("CONFIG_TARGET_IPQ5018_H3C_RT3000_B", None):
        exe = pathlib.Path(temp) / ("qca_uart_rx_zero_test_b" if target_macro else
                                    "qca_uart_rx_zero_test_generic")
        command = ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror"]
        if target_macro:
            command.append(f"-D{target_macro}")
        subprocess.run(command + [str(c_file), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
