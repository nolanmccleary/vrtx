#ifndef __QMETA_H__
#define __QMETA_H__

#include <stdint.h>
#include <stddef.h>

/*
 * Self-describing ELF layout metadata ("qmeta"). The image emits a small table
 * of region + struct-field descriptors, filled with link-time-constant addresses,
 * into dedicated LOADED sections (.qmeta_hdr / .qmeta_regions / .qmeta_fields).
 *
 * The host test arch reads these sections STATICALLY out of the .elf (objcopy
 * --dump-section) and thus learns where every region lives and how every result
 * struct is laid out -- no hardcoded symbol addresses, no hardcoded field offsets.
 * JTAG is then used only to read live values at the addresses the map hands back.
 *
 * Layout is fixed and padding-free so the host can unpack it with struct:
 *   hdr    : '<IIII'      (magic, version, region_stride, field_stride)   = 16 B
 *   region : '<16sIII'    (name[16], addr, size, flags)                   = 28 B
 *   field  : '<16s16sII'  (owner[16], name[16], offset, size)             = 40 B
 */

#define QMETA_MAGIC   0x514D4531u   /* "QME1" */
#define QMETA_VERSION 1

/* flags: low byte = region kind, high bits = attributes */
#define QMETA_KIND_MASK   0x000000FFu
#define QMETA_KIND_HEAP       1u
#define QMETA_KIND_TELEMETRY  2u
#define QMETA_KIND_RESULT     3u
#define QMETA_KIND_GLOBAL     4u
#define QMETA_KIND_STACK      5u
#define QMETA_KIND_BSS        6u
#define QMETA_KIND_DATA       7u

#define QMETA_F_COHERENT    0x00000100u  /* mdw phys reads are coherent here (non-cacheable mapping) */
#define QMETA_F_STACK_DESC  0x00000200u  /* addr is the stack TOP; region grows down by size */


typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t region_stride;   /* sizeof(qmeta_region_t) -- host validates its unpack */
    uint32_t field_stride;    /* sizeof(qmeta_field_t) */
} qmeta_hdr_t;

typedef struct {
    char     name[16];
    uint32_t addr;
    uint32_t size;
    uint32_t flags;
} qmeta_region_t;

typedef struct {
    char     owner[16];       /* struct this field belongs to, e.g. "edf_result" */
    char     name[16];
    uint32_t offset;
    uint32_t size;
} qmeta_field_t;


/* Section-placement helpers. `used` keeps them past --gc-sections; the linker
   script KEEP()s the sections too. Each TU may contribute region/field entries. */
#define QMETA_HDR      __attribute__((section(".qmeta_hdr"),    used))
#define QMETA_REGIONS  __attribute__((section(".qmeta_regions"),used, aligned(4)))
#define QMETA_FIELDS   __attribute__((section(".qmeta_fields"), used, aligned(4)))

#define QMETA_REGION(nm, a, sz, fl) \
    { .name = (nm), .addr = (uint32_t)(uintptr_t)(a), .size = (uint32_t)(sz), .flags = (fl) }

#define QMETA_FIELD(own, ty, fld) \
    { .owner = (own), .name = #fld, .offset = (uint32_t)offsetof(ty, fld), \
      .size = (uint32_t)sizeof(((ty*)0)->fld) }

#endif
