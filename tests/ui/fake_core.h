/* fake_core.h - knobs of the fake core (tests/ui/fake_core.c) */
#ifndef FAKE_CORE_H
#define FAKE_CORE_H
void fake_reset(void);
void fake_set_cam(int cam);
void fake_set_loaded(int id);
void fake_set_row(int row, int value);
void fake_set_grey(int mask);              /* bit r: row r has no effect (greyed) */
void fake_set_film_grey(int id, int mask); /* and for film id under the browser's cursor (not loaded): these too */
void fake_set_missing(int id);             /* film id not on the card */
void fake_set_status(const char *s);
void fake_set_films_state(int s);          /* wf_films_state(): 1 tables read, 0 starting, -1 worker failed, -2 no pack */
void fake_set_card(const char *dir);       /* root that ML/DATA/... paths are read from */
int  fake_row_value(int row);
void fake_card_busy(int n, int from_pos);  /* the next n card reads at pos >= from_pos fail as WF_RD_BUSY (-1: all) */
int  fake_card_reads(void);                /* wf_card_read calls so far */
void fake_log_clear(void);
int  fake_log_has(const char *s);          /* a wf_log line since fake_log_clear contains s */
#endif
