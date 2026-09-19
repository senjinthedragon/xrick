/*
 * xrick/include/config.h
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 *
 * SDL3 port and later modifications by Senjin the Dragon.
 */

#ifndef _CONFIG_H
#define _CONFIG_H

/* version */
#define VERSION "050500"

/* graphics (choose one) */
#define GFXST
#undef GFXPC

/* logging (write to console) */
#define ENABLE_LOG

/* gamepad support */
#define ENABLE_JOYSTICK

/* sound support */
#define ENABLE_SOUND

/* cheats support */
#define ENABLE_CHEATS

/* auto-defocus support */
/* does seem to cause all sorts of problems on BeOS, Windows... */
#undef ENABLE_FOCUS

/* development tools */
#undef ENABLE_DEVTOOLS
#undef DEBUG /* see include/debug.h -- turns on verbose per-subsystem   \
	      * logging (video/audio/ents/maps/scroller); leave off for \
	      * normal play */

/* zlib */
#ifndef NOZLIB
#define WITH_ZLIB
#endif

#endif

/* eof */
