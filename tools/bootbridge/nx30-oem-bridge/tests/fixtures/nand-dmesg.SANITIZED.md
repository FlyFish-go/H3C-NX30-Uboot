# Sanitized NAND/QPIC parser fixture

This fixture retains only five NAND/QPIC observation lines extracted from the original private capture (SHA-256 `33847d2346670b3021559f97c414b286c2582127e631cb5915e2ef895ca8103f`). The original byte-for-byte preimage is retained privately at `work/nand-oem-repair-20261017/private/fixture-maintenance-preimage/nand-dmesg.raw.original`.

Filtering retained QPIC controller version/serial-NAND capability and the NAND JEDEC ID, exact model, and geometry lines required by the production parsers. It removed shell commands/output, kernel command line and bootargs, restore marker, partition chatter, and raw write-progress lines. This is a parser fixture derived from a private observation; it does not establish hardware qualification.
