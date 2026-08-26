/*
 * memory_map.h — geometria della flash.
 *
 * Specifica: docs/03-memory-map.md
 *
 *   0x0800_0000  Bootloader        48 KB
 *   0x0800_C000  Esecuzione       100 KB   header 512 B + applicazione
 *   0x0802_5000  Staging          100 KB
 *   0x0803_E000  Metadati           8 KB
 */

#ifndef SBL_MEMORY_MAP_H
#define SBL_MEMORY_MAP_H

#include "image_header.h"

#define SBL_FLASH_BASE          0x08000000u
#define SBL_FLASH_SIZE          (256u * 1024u)
#define SBL_BANK_SIZE           (128u * 1024u)
#define SBL_BANK2_BASE          (SBL_FLASH_BASE + SBL_BANK_SIZE)

#define SBL_BOOT_BASE           SBL_FLASH_BASE
#define SBL_BOOT_SIZE           (48u * 1024u)

#define SBL_EXEC_BASE           0x0800C000u
#define SBL_EXEC_SIZE           (100u * 1024u)

#define SBL_STAGE_BASE          0x08025000u
#define SBL_STAGE_SIZE          (100u * 1024u)

#define SBL_META_BASE           0x0803E000u
#define SBL_META_SIZE           (8u * 1024u)

/* Spazio utile al payload, una volta tolto l'header. */
#define SBL_MAX_PAYLOAD         (SBL_EXEC_SIZE - SBL_HEADER_SIZE)

/* Vector table dell'applicazione: subito dopo l'header, allineata a 512. */
#define SBL_APP_VTOR            (SBL_EXEC_BASE + SBL_HEADER_SIZE)

_Static_assert(SBL_BOOT_SIZE + SBL_EXEC_SIZE + SBL_STAGE_SIZE + SBL_META_SIZE
               == SBL_FLASH_SIZE,
               "le regioni devono coprire esattamente la flash");
_Static_assert(SBL_EXEC_BASE == SBL_BOOT_BASE + SBL_BOOT_SIZE,
               "l'area di esecuzione deve seguire il bootloader senza buchi");
_Static_assert(SBL_STAGE_BASE == SBL_EXEC_BASE + SBL_EXEC_SIZE,
               "lo staging deve seguire l'area di esecuzione senza buchi");
_Static_assert(SBL_META_BASE == SBL_STAGE_BASE + SBL_STAGE_SIZE,
               "i metadati devono seguire lo staging senza buchi");
_Static_assert(SBL_STAGE_SIZE >= SBL_EXEC_SIZE,
               "lo staging deve poter contenere un'immagine intera");
_Static_assert((SBL_APP_VTOR & 0x1FFu) == 0u,
               "la vector table deve essere allineata a 512 byte per VTOR");

#endif /* SBL_MEMORY_MAP_H */
