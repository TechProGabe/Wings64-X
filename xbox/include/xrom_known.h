/* xrom_known.h - the known-good Pilotwings 64 dumps. The only place the hash
 * lives; there is no ROM data anywhere in the repository. SHA-1 is of the
 * 8 MB image in big-endian (.z64) byte order, which is what xhw_rom.c
 * normalises every accepted file to. */
#ifndef XROM_KNOWN_H
#define XROM_KNOWN_H

#include "xrom.h"

static const xrom_known xrom_known_table[] = {
    { "NPWE", 0, "ec771aedf54ee1b214c25404fb4ec51cfd43191a", "8b346182730ceaffe5e2ccf6d223c5ef", "US v1.0" },
};
#define XROM_KNOWN_COUNT ((int)(sizeof xrom_known_table / sizeof xrom_known_table[0]))

#endif
