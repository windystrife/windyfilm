/* wf_cam.h - per-camera build switches of the shared look / effect code. build.sh CAM=EOSM (default) or CAM=5D2
 * (-DWF_BUILD_5D2). Frame geometry, hook sites and Canon addresses are NOT here: they live in the camera input layer
 * (wf_cam_eosm.c / wf_cam_5d2.c) only. What is here is what the effects need to size their state. */
#ifndef WF_CAM_H
#define WF_CAM_H

#define WF_CHUNK 288                     /* words (576 px) per burst through the cached scratch (wf_shot.c) */

#ifdef WF_BUILD_5D2
#define WF_PV_CAN               1        /* LiveView preview: ML's 5D2 display filter hands 720x480 UYVY like the EOS M */
#define WF_GRAIN_CAN            1
#define WF_HALO_CAN             1
#define WF_MIST_CAN             1        /* halves: the TOP's map is kept for its BOT (wf_mist.h MIST_TOP / MIST_BOTTOM) */
#define WF_MIST_SCALE           277      /* PSF scaled to the frame width: 5616 / 5184 * 256 */
#define WF_GRAIN_DEF_CAM        0        /* grain / halation / bloom cost seconds per half on DIGIC 4: default Off */
#define WF_HALO_DEF_CAM         0
#define WF_BLOOM_DEF_CAM        0
/* the halation grid: the 5D2's TOP / BOT seam (frame row 1872 = 117 x 16) is aligned to the 1/16 level only, so the grid
 * is the frame moved down 48 rows (zero light above the picture): 3792 rows, the seam at 1920 = 30 x 64 */
#define WF_HALO_W               5616
#define WF_HALO_H               3792
#define WF_HALO_Y0              48
#define WF_HALO_SEAM            1920
#define WF_GRAIN_W              5616     /* image width of the grain pitch: 36000 / 5616 = 6.41 um */
#define WF_EXP_CAN              0        /* Preview exposure: EOS M only (property 0x80000034, frame parameters at 0x404C8) */
#define WF_PRB_CAN              1        /* RAW probe P0 at the TOP + BOT hooks (wf_cam_5d2.c + wf_probe.c) */
#define WF_RAWB_CAN             1        /* RAW develop "B": TOP + BOT halves, MEM1 raw (not probed on the camera yet) */
#else
#define WF_PV_CAN               1
#define WF_GRAIN_CAN            1
#define WF_HALO_CAN             1
#define WF_MIST_CAN             1
#define WF_MIST_SCALE           256
#define WF_GRAIN_DEF_CAM        2
#define WF_HALO_DEF_CAM         1        /* Wide + Bloom Normal = the old default "Wide + Glow" */
#define WF_BLOOM_DEF_CAM        2
#define WF_HALO_W               5184
#define WF_HALO_H               3456
#define WF_HALO_Y0              0
#define WF_HALO_SEAM            (WF_HALO_H / 2)
#define WF_GRAIN_W              WF_HALO_W
#define WF_EXP_CAN              1
#define WF_PRB_CAN              1        /* RAW probe P0 at the NN hook (wf_cam_eosm.c + wf_probe.c) */
#define WF_RAWB_CAN             1        /* RAW develop "B" at the NN hook (wf_cam_eosm.c names the raw, wf_rawb.c) */
#endif

#endif
