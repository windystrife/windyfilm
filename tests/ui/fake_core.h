/* fake_core.h - knobs of the fake core (tests/ui/fake_core.c) */
#ifndef FAKE_CORE_H
#define FAKE_CORE_H
void fake_reset(void);
void fake_set_cam(int cam);
void fake_set_loaded(int id);
void fake_set_row(int row, int value);
void fake_set_grey(int mask);              /* bit r: row r has no effect (greyed) */
void fake_set_missing(int id);             /* film id not on the card */
void fake_set_status(const char *s);
void fake_set_card(const char *dir);       /* root that ML/DATA/... paths are read from */
int  fake_row_value(int row);
#endif
