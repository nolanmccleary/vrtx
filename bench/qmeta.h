#ifndef QMETA_H
#define QMETA_H

#include <stdint.h>
#include <stddef.h>


/*
 * Self-describing ELF region metadata.
 *
 * The image emits:
 *
 *     .qmeta_hdr
 *     .qmeta_regions
 *
 * containing link-time-constant region addresses/sizes.
 *
 * The host may parse these statically from the ELF and then use JTAG only
 * for reading/writing the corresponding live memory.
 *
 * Binary layout:
 *
 *     header : <III
 *              magic, version, region_stride
 *              12 bytes
 *
 *     region : <16sIII
 *              name[16], addr, size, flags
 *              28 bytes
 */


#define QMETA_MAGIC   0x514D4531u      /* "QME1" */
#define QMETA_VERSION 2u


/* Low byte = region kind. */
#define QMETA_KIND_MASK       0x000000FFu

#define QMETA_KIND_HEAP       1u
#define QMETA_KIND_TELEMETRY  2u
#define QMETA_KIND_GLOBAL     3u
#define QMETA_KIND_STACK      4u
#define QMETA_KIND_BSS        5u
#define QMETA_KIND_DATA       6u
#define QMETA_KIND_STATUS     7u


/* Region attributes. */
#define QMETA_F_COHERENT      0x00000100u
#define QMETA_F_STACK_DESC    0x00000200u


typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t region_stride;
} qmeta_hdr_t;


typedef struct
{
    char     name[16];
    uint32_t addr;
    uint32_t size;
    uint32_t flags;
} qmeta_region_t;


/*
 * Host binary decoder depends on these exact target layouts.
 */
_Static_assert(sizeof(qmeta_hdr_t)    == 12u, "qmeta_hdr_t layout changed");
_Static_assert(sizeof(qmeta_region_t) == 28u, "qmeta_region_t layout changed");


#define QMETA_HDR \
    __attribute__((section(".qmeta_hdr"), used, aligned(4)))

#define QMETA_REGIONS \
    __attribute__((section(".qmeta_regions"), used, aligned(4)))


#define QMETA_REGION(nm, a, sz, fl)                   \
    {                                                 \
        .name  = (nm),                                \
        .addr  = (uint32_t)(uintptr_t)(a),            \
        .size  = (uint32_t)(uintptr_t)(sz),           \
        .flags = (fl)                                 \
    }


#endif
