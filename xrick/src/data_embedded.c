/*
 * xrick/src/data_embedded.c
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * Embeds build/data.zip (produced by the Makefile from data/) directly
 * into the executable, so it can run with no separate data file. Requires
 * a C23 compiler for #embed; only this file needs that dialect.
 */

#include "data_embedded.h"

const unsigned char data_embedded_zip[] = {
#embed "../../build/data.zip"
};

const unsigned long data_embedded_zip_len = sizeof(data_embedded_zip);

/* eof */
