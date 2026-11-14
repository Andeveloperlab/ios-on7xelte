/*
 * Pixel change detection only: fingerprints are never persisted or used for security.
 * J7 Prime (on7xelte): the A50 port uses XXH3 here (third_party/xxhash, not carried over); this is
 * a plain four-lane multiply-xor hash over the whole buffer, its tail included.
 */
#include <string.h>

#include "surface_checksum.h"

uint64_t
hakios_surface_checksum(const void *pixels, size_t bytes)
{
	const unsigned char *p = pixels;
	uint64_t h[4] = { 0x9e3779b97f4a7c15ULL, 0xc2b2ae3d27d4eb4fULL, 0x165667b19e3779f9ULL, 0x27d4eb2f165667c5ULL };
	const uint64_t m = 0x100000001b3ULL * 0x9e3779b1ULL | 1;
	size_t at = 0;

	for (; at + 32 <= bytes; at += 32) {
		uint64_t w[4];

		memcpy(w, p + at, sizeof(w));
		for (int i = 0; i < 4; i++) {
			h[i] = (h[i] ^ w[i]) * m;
		}
	}
	for (; at < bytes; at++) {
		h[at & 3] = (h[at & 3] ^ p[at]) * m;
	}
	return (h[0] ^ (h[1] >> 17) ^ (h[2] << 13) ^ (h[3] >> 29)) * m ^ bytes;
}
