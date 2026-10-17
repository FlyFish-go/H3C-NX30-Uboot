from pathlib import Path
import subprocess, tempfile

R = Path(__file__).resolve().parents[2]
src = (R / "u-boot-2016/drivers/mtd/ubi/io.c").read_text()
a = src.index("static int ubi_mtd_read_aligned(")
b = src.index("\nint ubi_io_read(", a)
fn = src[a:b]
harness = r"""
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t uint8_t;
#define GFP_KERNEL 0
#define ARCH_DMA_MINALIGN 64
#define do_div(n, base) ({ unsigned int __rem = (unsigned int)((n) % (base)); (n) /= (base); __rem; })
struct mtd_info { unsigned writesize; };
static unsigned char flash[32768];
static int injected_err;
static int short_read;
static void *memalign(size_t a, size_t n) {
 void *p = NULL;
 if (posix_memalign(&p, a, n)) return NULL;
 return p;
}
static int mtd_read(struct mtd_info *m, loff_t from, size_t len,
                    size_t *retlen, unsigned char *buf) {
 if (from < 0 || from % m->writesize || len % m->writesize ||
     ((uintptr_t)buf % 64) || (size_t)from + len > sizeof(flash))
  return -EINVAL;
 if (short_read) { memcpy(buf, flash + from, len / 2); *retlen = len / 2; return injected_err; }
 memcpy(buf, flash + from, len); *retlen = len; return injected_err;
}
""" + fn + r"""
int main(void) {
 struct mtd_info m = { 2048 }; unsigned char out[4145];
 size_t got; int rc; unsigned i;
 for (i = 0; i < sizeof(flash); i++) flash[i] = (unsigned char)(i * 37U + 11U);
 memset(out, 0, sizeof(out));
 rc = ubi_mtd_read_aligned(&m, out + 1, 0, 4144, &got);
 if (rc || got != 4144 || memcmp(out + 1, flash, 4144)) return 1;
 memset(out, 0, sizeof(out));
 rc = ubi_mtd_read_aligned(&m, out + 1, 4144, 4144, &got);
 if (rc || got != 4144 || memcmp(out + 1, flash + 4144, 4144)) return 2;
 rc = ubi_mtd_read_aligned(&m, out + 1, 2048, 2048, &got);
 if (rc || got != 2048 || memcmp(out + 1, flash + 2048, 2048)) return 3;
 injected_err = -EUCLEAN;
 rc = ubi_mtd_read_aligned(&m, out + 1, 4144, 4144, &got);
 if (rc != -EUCLEAN || got != 4144 || memcmp(out + 1, flash + 4144, 4144)) return 4;
 injected_err = 0; short_read = 1; memset(out, 0xA7, sizeof(out));
 rc = ubi_mtd_read_aligned(&m, out + 1, 4144, 4144, &got);
 if (rc != -EIO || got != 0) return 5;
 for (i = 0; i < sizeof(out); i++) if (out[i] != 0xA7) return 6;
 puts("PASS: production UBI helper handles offsets 0/4144, ECC, and short reads without copying or claiming a complete range");
 return 0;
}
"""
with tempfile.TemporaryDirectory() as d:
 c = Path(d) / "t.c"; exe = Path(d) / "t"
 c.write_text(harness)
 subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                 str(c), "-o", str(exe)], check=True)
 subprocess.run([str(exe)], check=True)
