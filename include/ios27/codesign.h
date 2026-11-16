#ifndef IOS27_CODESIGN_H
#define IOS27_CODESIGN_H

#include <stddef.h>
#include <stdint.h>

/* Embedded code-signature layout from Apple xnu osfmk/kern/cs_blobs.h:
 * https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/kern/cs_blobs.h
 * Every multi-byte field in these blobs is big-endian. */
#define IOS27_CSMAGIC_REQUIREMENTS 0xfade0c01u
#define IOS27_CSMAGIC_CODEDIRECTORY 0xfade0c02u
#define IOS27_CSMAGIC_EMBEDDED_SIGNATURE 0xfade0cc0u
#define IOS27_CSMAGIC_EMBEDDED_ENTITLEMENTS 0xfade7171u
#define IOS27_CSMAGIC_EMBEDDED_DER_ENTITLEMENTS 0xfade7172u
#define IOS27_CSMAGIC_BLOBWRAPPER 0xfade0b01u

#define IOS27_CSSLOT_CODEDIRECTORY 0u
#define IOS27_CSSLOT_REQUIREMENTS 2u
#define IOS27_CSSLOT_ENTITLEMENTS 5u
#define IOS27_CSSLOT_DER_ENTITLEMENTS 7u
#define IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES 0x1000u
#define IOS27_CSSLOT_ALTERNATE_CODEDIRECTORY_MAX 5u
#define IOS27_CSSLOT_SIGNATURESLOT 0x10000u

/* CodeDirectory versions. Each version appends the fields that cs_blobs.h
 * lists under it. 0x20001 is the earliest version (Apple Security
 * framework, CodeDirectory::earliestVersion). This parser knows the layout
 * up to CS_SUPPORTSLINKAGE and rejects any newer version. */
#define IOS27_CS_VERSION_EARLIEST 0x20001u
#define IOS27_CS_SUPPORTSSCATTER 0x20100u
#define IOS27_CS_SUPPORTSTEAMID 0x20200u
#define IOS27_CS_SUPPORTSCODELIMIT64 0x20300u
#define IOS27_CS_SUPPORTSEXECSEG 0x20400u
#define IOS27_CS_SUPPORTSRUNTIME 0x20500u
#define IOS27_CS_SUPPORTSLINKAGE 0x20600u

#define IOS27_CS_HASHTYPE_SHA1 1u
#define IOS27_CS_HASHTYPE_SHA256 2u
#define IOS27_CS_HASHTYPE_SHA256_TRUNCATED 3u
#define IOS27_CS_HASHTYPE_SHA384 4u

/* Limits of this parser, not of the format. */
#define IOS27_CODESIGN_MAX_BLOBS 32u
#define IOS27_CODESIGN_MAX_DIRECTORIES (1u + IOS27_CSSLOT_ALTERNATE_CODEDIRECTORY_MAX)

typedef struct {
    uint32_t slot;          /* 0, or 0x1000 to 0x1004 for an alternate */
    uint32_t version;
    uint32_t flags;         /* CS_* setup and mode flags, not interpreted */
    uint32_t special_slots; /* nSpecialSlots */
    uint32_t code_slots;    /* nCodeSlots */
    uint8_t hash_type;      /* IOS27_CS_HASHTYPE_* */
    uint8_t hash_size;      /* bytes per hash, checked against hash_type */
    uint8_t page_size_log2; /* log2 of the page size; 0 means infinite */
    uint8_t platform;       /* zero unless a platform binary */
    unsigned has_exec_seg : 1; /* version >= IOS27_CS_SUPPORTSEXECSEG */
    uint64_t exec_seg_flags;   /* CS_EXECSEG_*; 0 without has_exec_seg */
} ios27_code_directory;

typedef struct {
    uint32_t length;        /* SuperBlob length, at most datasize */
    uint32_t count;         /* SuperBlob index entries */
    size_t directory_count; /* slot 0 first, then alternates by slot */
    ios27_code_directory directories[IOS27_CODESIGN_MAX_DIRECTORIES];
    unsigned has_requirements : 1;     /* slot 2 */
    unsigned has_entitlements : 1;     /* slot 5; contents never read */
    unsigned has_der_entitlements : 1; /* slot 7; contents never read */
    unsigned has_signature : 1;        /* slot 0x10000, the CMS wrapper */
} ios27_codesign;

/* Read-only, bounded parser for the embedded signature that
 * LC_CODE_SIGNATURE places at image[dataoff, dataoff + datasize). It checks
 * the SuperBlob and the bounds of every blob, reports CodeDirectory facts
 * and reports slots 2, 5, 7 and 0x10000 as present or absent. It reads no
 * entitlement contents, verifies no hash or signature, maps and executes
 * nothing and does not touch the filesystem.
 * Return 0 on success with an empty error. Return -1 on malformed or
 * unsupported input, with *out zeroed and, when error_capacity > 0, a
 * NUL-terminated error. */
int ios27_codesign_parse(const uint8_t *image, size_t size,
                         uint32_t dataoff, uint32_t datasize,
                         ios27_codesign *out,
                         char *error, size_t error_capacity);

#endif
