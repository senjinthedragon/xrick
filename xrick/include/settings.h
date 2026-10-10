/*
 * xrick/include/settings.h
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * Persistent user settings (video, audio, controls), stored in a small
 * text file in the user's data directory.
 */

#ifndef _SETTINGS_H
#define _SETTINGS_H

/* read the file into the sysarg_args_* / key / button variables; call
 * before parsing the command line so command-line options win */
extern void settings_load(void);

/* apply the settings that can only be set once audio is up (volume, mute) */
extern void settings_apply(void);

/* write the current state to the file; call after any user-made change */
extern void settings_save(void);

#endif /* _SETTINGS_H */

/* eof */
