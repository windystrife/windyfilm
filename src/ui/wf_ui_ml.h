/* wf_ui_ml.h - WindyFilm browser: the one place where the UI pulls in Magic Lantern's API.
 *
 * Camera build: ML's own headers (the EOS M card core tree /root/ml/expsim/mls, or the 5D2 q5d2 tree).
 * Host build (-DWF_HOST, tests/ui): tests/ui/host_ml.h declares the same names with a stub implementation.
 *
 * ML API the browser uses (every one must be in the camera's .sym; tests/ui/armcheck.sh checks it):
 *   bmp_vram_idle, bmp_idle_copy, bmp_lock, AcquireRecursiveLock, ReleaseRecursiveLock    drawing / copy
 *   LCD_Palette, EngDrvOut, shamem_read                                                  palette
 *   font_by_name, font_dynamic                                                           argnor23 / argnor28
 *   canon_gui_disable_front_buffer, canon_gui_enable_front_buffer                        own the screen
 *   task_create, msleep, get_ms_clock                                                    UI task
 *   gui_menu_shown, gui_stop_menu, gui_state, lv, display_is_on, __mem_malloc, __mem_free
 *   get_gui_mode, SetGUIRequestMode, display_on                                          5D2 without LiveView
 */
#ifndef WF_UI_ML_H
#define WF_UI_ML_H

#ifdef WF_HOST
#include "host_ml.h"
#else
#include <dryos.h>
#include <module.h>
#include <bmp.h>
#include <menu.h>
#include <propvalues.h>
#include <gui-common.h>
#include <powersave.h>
#include <timer.h>
#include <rbf_font.h>
#endif

#endif
