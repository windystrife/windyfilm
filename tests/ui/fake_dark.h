/* fake_dark.h - knobs of the fake roll / darkroom core (tests/ui/fake_dark.c) */
#ifndef FAKE_DARK_H
#define FAKE_DARK_H
#include "../../src/wf_dark.h"
void fake_dark_reset(void);
struct wf_roll *fake_roll(void);            /* write access (bumps the roll's gen) */
struct wf_dark_view *fake_view(void);       /* write access (bumps the view's gen) */
void fake_set_clock(int hm);
void fake_set_batt(int pct);                /* wf_battery_pct() (the film OS top bar; -1 unknown) */
void fake_set_lv(const struct wfos_lv *v);  /* wf_osvf_values() (the 5D2's Top plate) */
int  fake_plate_dials(int dial);            /* wf_plate_dial calls for dial 0 (main) / 1 (rear) */
int  fake_plate_dial_dir(void);             /* the last call's direction */
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
int  fake_nkeep(void);                      /* the Darkroom tick's Screen On keep-alive calls (wf_dark_keep, ui 1) */
int  fake_nwake(void);                      /* keys that only woke it (wf_dark_wake = 1) */
void fake_set_shelf(int on);                /* wf_roll_shelved() (a roll set aside by the mode chip) */
void fake_set_rawb_can(int c);              /* wf_rawb_can(): RAW develop on this camera (wf_rawb in fake_dark.c) */
/* the shelf (owner 11-17): wf_shelf_now's list, a roll put on it (newest first), the Darkroom's asks */
struct wf_shelf *fake_shelf(void);          /* write access (bumps its gen) */
void fake_shelf_add(const struct wf_roll *r);
int  fake_nrescan(void);                    /* wf_shelf_rescan calls (the Darkroom asks on entry) */
int  fake_start_num(void);                  /* the last start: 0 the camera's roll, else wf_dark_start_roll's num */
int  fake_start_again(void);
int  fake_start_film(void);
int  fake_nswap_devd(void);                 /* close-and-loads of a roll developed at the shot */
/* the Darkroom's photos (RAWB28): real CR2s read from dir through wf_photo.c (frame i of roll num = its (i + num x 5)th
 * CR2, the camera's roll from the first); none set = no photos (the sketch) */
void fake_set_photo_dir(const char *dir);
void fake_photo_partial(int nread, int none);   /* frames >= nread not read yet (-1 = all read), frame none has none */
void fake_photo_again(void);               /* the next wf_dark_photos assembles them again */
void fake_photo_reset(void);               /* wf_dk_photo on, all read */
const struct wf_dark_photos *fake_photos(void);
#endif
