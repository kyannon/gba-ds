/* SHA-256 (FIPS 180-4), for checking downloaded updates. */
#ifndef GBADS_SHA256_H
#define GBADS_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint32_t h[8];
	uint64_t len;
	uint8_t buf[64];
	size_t n;
} Sha256;

void sha256_init(Sha256* s);
void sha256_update(Sha256* s, const void* data, size_t len);
void sha256_final(Sha256* s, uint8_t out[32]);
/* The file's hash as 64 lowercase hex digits; false if it can't be read. */
int sha256_file_hex(const char* path, char hex[65]);

#endif
