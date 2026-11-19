/* Explicit-endian readers and a small growable buffer. */
#ifndef TWTW_BYTES_H
#define TWTW_BYTES_H

#include <stddef.h>
#include <stdint.h>

static inline uint16_t rd_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t rd_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Read a whole host file into memory. Returns NULL on failure; *size receives the length. */
uint8_t *read_host_file(const char *path, size_t *size);

#endif
