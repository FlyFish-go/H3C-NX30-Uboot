enum rt3_layout_mode { RT3_LAYOUT_UNSUPPORTED=0,RT3_LAYOUT_DUAL,RT3_LAYOUT_SINGLE };
struct rt3_layout { enum rt3_layout_mode mode; unsigned int fingerprint; };
int rt3_layout_read(struct rt3_layout *);
int rt3_layout_resolve_raw(const void *, size_t, struct rt3_layout *);
