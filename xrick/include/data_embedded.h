/*
 * xrick/include/data_embedded.h
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * The data.zip archive (built from data/ by the Makefile), embedded
 * directly into the executable via #embed, so a normal run needs no
 * external data file at all.
 */

#ifndef _DATA_EMBEDDED_H
#define _DATA_EMBEDDED_H

extern const unsigned char data_embedded_zip[];
extern const unsigned long data_embedded_zip_len;

#endif

/* eof */
