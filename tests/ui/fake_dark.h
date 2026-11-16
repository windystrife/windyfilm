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
void fake_set_date(int ymd);                /* wf_clock_ymd() (the Date row shows today in its format) */
void fake_set_dark(int d);                  /* a Screen Off run: the display reads dark (wf_dark_dark) until a key */
int  fake_nwake(void);                      /* keys that only woke it (wf_dark_wake = 1) */
void fake_set_shelf(int on);                /* wf_roll_shelved() (a roll set aside by the mode chip) */
void fake_set_rawb_can(int c);              /* wf_rawb_can(): RAW develop on this camera (wf_rawb in fake_dark.c) */
#endif
