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
const int wf_t_card_fn = offsetof(struct card_info, file_number);
const int wf_t_card_size = sizeof(struct card_info);
