/* fake_dark.h - knobs of the fake roll / darkroom core (tests/ui/fake_dark.c) */
#ifndef FAKE_DARK_H
#define FAKE_DARK_H
#include "../../src/wf_dark.h"
void fake_dark_reset(void);
struct wf_roll *fake_roll(void);            /* write access (bumps the roll's gen) */
struct wf_dark_view *fake_view(void);       /* write access (bumps the view's gen) */
void fake_set_clock(int hm);
int  fake_load_print(const char *path);     /* WF_DR_PW x WF_DR_PH luma file */
int  fake_load_wire(int i, const char *path);
int  fake_req_film(void);
int  fake_req_exp(void);
int  fake_nstart(void);
int  fake_nstop(void);
void fake_set_locked(int k);                /* wf_roll_shutter_locked() */
int  fake_nswap(void);                      /* wf_roll_req_swap calls that closed + loaded */
#endif
