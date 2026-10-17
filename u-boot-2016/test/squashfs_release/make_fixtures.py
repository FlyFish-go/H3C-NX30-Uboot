#!/usr/bin/env python3
import os
import pathlib
import sys
base = pathlib.Path(sys.argv[1])
long = base / "long-raw"
etc = long / "etc"
etc.mkdir(parents=True)
(etc / "openwrt_release").write_text("DISTRIB_ID=\x27OpenWrt\x27\nDISTRIB_RELEASE=\x2723.05.2\x27\nDISTRIB_REVISION=\x27r-test123\x27\nDISTRIB_DESCRIPTION=\x27OpenWrt Fixture 23.05.2\x27\nHOME_URL=\x27https://ignored.invalid/path?q=x#fragment\x27\n", encoding="ascii")
(etc / "os-release").symlink_to("../usr/lib/os-release")
for i in range(6000):
    (etc / ("f%05d" % i)).touch()
link = base / "symlink-only" / "etc"
link.mkdir(parents=True)
(link / "os-release").symlink_to("../usr/lib/os-release")
longreg = base / "long-regular"
etc = longreg / "etc"
etc.mkdir(parents=True)
release = etc / "openwrt_release"
release.write_text("DISTRIB_ID=OpenWrt\nDISTRIB_RELEASE=23.05.2\nDISTRIB_REVISION=r-hardlink\nDISTRIB_DESCRIPTION=OpenWrt LongRegular 23.05.2\n", encoding="ascii")
share = longreg / "usr" / "share"
share.mkdir(parents=True)
os.link(release, share / "release-copy")
