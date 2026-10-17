/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef NX30_D_PROTECT_CORE_H
#define NX30_D_PROTECT_CORE_H
/* Register changes only. The array aperture is held closed until begin passes. */
struct nx30_d_protect_state { unsigned int original; int captured, changed, fault; };
struct nx30_d_protect_io {
 void *context;
 int (*get)(void *context, unsigned int *value);
 int (*set)(void *context, unsigned int value);
};
static int nx30_d_protect_end(struct nx30_d_protect_state *s,
                              const struct nx30_d_protect_io *io)
{
 unsigned int current=0; int ret=0;
 if (!s || !io || !io->get || !io->set) return -1;
 if (!s->captured) return s->fault ? -1 : 0;
 if (s->changed && io->set(io->context,s->original)) ret=-1;
 if (io->get(io->context,&current) || current!=s->original) ret=-1;
 s->captured=0; s->changed=0; s->original=0;
 if (ret) s->fault=1;
 return ret;
}
static int nx30_d_protect_begin(struct nx30_d_protect_state *s,
                               const struct nx30_d_protect_io *io,
                               unsigned int mask)
{
 unsigned int current=0,wanted;
 if (!s || !io || !io->get || !io->set || !mask || s->captured || s->fault) return -1;
 if (io->get(io->context,&current) || current>255U) { s->fault=1; return -1; }
 s->original=current; s->captured=1;
 if (!(current&mask)) return 0;
 wanted=current&~mask;
 /* A failed set may still have changed the register: restore exactly once. */
 s->changed=1;
 if (io->set(io->context,wanted) || io->get(io->context,&current) || current!=wanted) {
  nx30_d_protect_end(s,io);
  return -1;
 }
 return 0;
}
#endif
