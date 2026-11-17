/* wf_expoff.c - TEST ONLY (test.sh, not in the module): the layout of ML's struct lens_info and struct card_info as
 * the camera build sees them (compiled with ML's own headers), so the host shim (host_ml.c) can fill the bytes the
 * module objects read. */
#include <dryos.h>
#include <lens.h>
#include <fio-ml.h>
#include <stddef.h>

const int wf_t_li_size = sizeof(struct lens_info);
const int wf_t_li_shutter = offsetof(struct lens_info, raw_shutter);
const int wf_t_li_aperture = offsetof(struct lens_info, raw_aperture);
const int wf_t_li_iso = offsetof(struct lens_info, raw_iso);
const int wf_t_li_iso_auto = offsetof(struct lens_info, raw_iso_auto);
const int wf_t_li_ae = offsetof(struct lens_info, ae);
const int wf_t_li_job = offsetof(struct lens_info, job_state);
const int wf_t_li_wb_mode = offsetof(struct lens_info, wb_mode);
const int wf_t_li_kelvin = offsetof(struct lens_info, kelvin);
const int wf_t_li_wbs_gm = offsetof(struct lens_info, wbs_gm);
const int wf_t_li_wbs_ba = offsetof(struct lens_info, wbs_ba);
const int wf_t_li_picstyle = offsetof(struct lens_info, raw_picstyle);
const int wf_t_li_isov = offsetof(struct lens_info, iso);             /* the ISO number (RAW develop's white per ISO) */
const int wf_t_li_isoa = offsetof(struct lens_info, iso_auto);
const int wf_t_li_name = offsetof(struct lens_info, name);            /* QKEYS: name[0] = a lens with contacts */
const int wf_t_li_amin = offsetof(struct lens_info, raw_aperture_min);
const int wf_t_li_amax = offsetof(struct lens_info, raw_aperture_max);
const int wf_t_card_fn = offsetof(struct card_info, file_number);
const int wf_t_card_folder = offsetof(struct card_info, folder_number);   /* the roll's frame numbers (wf_card_nums) */
const int wf_t_card_size = sizeof(struct card_info);
/* struct menu_entry (ROLL mode: finds an entry of the module's menu by name and calls its select, as ML does on SET) */
#include <menu.h>
const int wf_t_me_size = sizeof(struct menu_entry);
const int wf_t_me_name = offsetof(struct menu_entry, name);
const int wf_t_me_priv = offsetof(struct menu_entry, priv);
const int wf_t_me_select = offsetof(struct menu_entry, select);
const int wf_t_me_children = offsetof(struct menu_entry, children);
const int wf_t_me_help = offsetof(struct menu_entry, help);           /* MIST:levels: the Mist entry's help line */
int wf_t_me_shidden(const void *e) { return ((const struct menu_entry *)e)->shidden; }   /* a bit field: no offsetof */
