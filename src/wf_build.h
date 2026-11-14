/* wf_build.h - the names of this build (WindyStrife).
 *
 * Two modules come out of this tree and they are built to be installed side by side on one card:
 *   windyflm.mo  the catalogue build (default, STAGE not set)
 *   windyfls.mo  the film-stage build (build.sh STAGE=1): the same engine with the chain-derived films, its own
 *                settings file and log so the two never read each other's state, its own menu entry, its own title and
 *                a rose accent instead of the amber one - a different machine at a glance, not a copy.
 * WF_STAGE comes from build.sh (-DWF_STAGE), never from a settings file: which module this is cannot be changed on
 * the camera. */
#ifndef WF_BUILD_H
#define WF_BUILD_H

#define WFSTR_(x) #x                          /* the module symbol as a string, for module_info's name check */
#define WFSTR(x) WFSTR_(x)

#ifdef WF_STAGE

#define WF_NAME         "WindyFilm Stage"           /* ML Modules menu */
#define WF_MENU_NAME    "Film Stage"                /* ML top-level menu (the catalogue build says "WindyFilm") */
#define WF_TITLE        "Film Stage"                /* browser header, EXIF UserComment prefix */
#define WF_CFG_PATH     "ML/SETTINGS/WINDYFLS.CFG"
#define WF_LOG_PATH     "ML/LOGS/WINDYFLS.LOG"
#define WF_LOG_OLD      "ML/LOGS/WINDYFLS.OLD"
#define WF_MODULE_SYM   windyfls                    /* MODULE_NAME, and the __module_*_<name> symbols */

#else

#define WF_NAME         "WindyFilm"
#define WF_MENU_NAME    "WindyFilm"
#define WF_TITLE        "WindyFilm"
#define WF_CFG_PATH     "ML/SETTINGS/WINDYFLM.CFG"
#define WF_LOG_PATH     "ML/LOGS/WINDYFLM.LOG"
#define WF_LOG_OLD      "ML/LOGS/WINDYFLM.OLD"
#define WF_MODULE_SYM   windyflm

#endif

#endif
