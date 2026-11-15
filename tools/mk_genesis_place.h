/* mk_genesis_place.h - host only: where the chain films' placement puts 18 % grey (declared exception, DESIGN 1a).
 *
 * Shared by tools/mk_genesis_cube.c (the shipped tables) and tools/mk_wfstage.c (the film-stage pairs), which must
 * place a pair the same way: `mk_wfstage dat` checks the default composition against the shipped tables.
 *
 * The camera's JPEG holds 18 % grey at its sRGB code, 0.4614 (117.6) - the code the catalogue films are built around
 * (DESIGN A1). The cube's input is that display code decoded with the chain's own curve (gl_disp_to_lin, gamma 2.4) and
 * the chain's output is display-encoded with the same curve, so the levelled output is a display CODE too. The
 * placement fits the exposure so the grey code comes out on itself: a grey patch at 118 stays at 118.
 * Until 15/11/2026 the target was 0.18 - a linear value used as a code - with linear 0.18 as the probe, which put
 * grey near code 46: about two stops dark next to the catalogue films (owner's sweep IMG_6844 / IMG_6856: Y p50
 * 27..34 against 92..118). tools/check_chain_grey.py (test.sh GREY) pins the result.
 */
#ifndef MK_GENESIS_PLACE_H
#define MK_GENESIS_PLACE_H
#include <math.h>

#define GENESIS_GREY_CODE (1.055 * pow(0.18, 1.0 / 2.4) - 0.055)   /* sRGB code of 18 % grey: in, and out */

#endif
