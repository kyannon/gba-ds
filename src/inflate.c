/* A small DEFLATE/zlib decoder (see inflate.h).
 *
 * Straight canonical-Huffman decoding, after Mark Adler's "puff" (public
 * domain): bit by bit, a length table per block, then the length/distance
 * pairs. Fast enough here — it runs once per font, on about 7 MB in all.
 */
#include "inflate.h"

#include <stdint.h>
#include <string.h>

typedef struct {
	const unsigned char* in;
	size_t len, pos;
	uint32_t bitbuf;
	int bitcnt;
} Bits;

/* The next n bits (n <= 16), LSB first; -1 when the stream ends. */
static int bits_get(Bits* s, int n) {
	uint32_t val = s->bitbuf;
	while (s->bitcnt < n) {
		if (s->pos >= s->len) return -1;
		val |= (uint32_t) s->in[s->pos++] << s->bitcnt;
		s->bitcnt += 8;
	}
	s->bitbuf = val >> n;
	s->bitcnt -= n;
	return (int) (val & ((1u << n) - 1u));
}

/* A byte of the stream, after the bits already read (stored blocks). */
static int byte_get(Bits* s) {
	if (s->bitcnt >= 8) {
		int b = (int) (s->bitbuf & 0xFFu);
		s->bitbuf >>= 8;
		s->bitcnt -= 8;
		return b;
	}
	if (s->pos >= s->len) return -1;
	return s->in[s->pos++];
}

typedef struct {
	short count[16];
	short symbol[288];
} Huff;

/* 0: no codes at all, 1: fine, -1: over-subscribed or incomplete */
static int huff_build(Huff* h, const unsigned char* lengths, int n) {
	memset(h->count, 0, sizeof(h->count));
	for (int i = 0; i < n; ++i) h->count[lengths[i]]++;
	if (h->count[0] == n) return 0;
	int left = 1;
	for (int len = 1; len < 16; ++len) {
		left <<= 1;
		left -= h->count[len];
		if (left < 0) return -1;
	}
	short offs[16];
	offs[0] = offs[1] = 0;
	for (int len = 1; len < 15; ++len) offs[len + 1] = (short) (offs[len] + h->count[len]);
	for (int i = 0; i < n; ++i)
		if (lengths[i]) h->symbol[offs[lengths[i]]++] = (short) i;
	return 1;
}

static int huff_decode(Bits* s, const Huff* h) {
	int code = 0, first = 0, index = 0;
	for (int len = 1; len < 16; ++len) {
		int b = bits_get(s, 1);
		if (b < 0) return -1;
		code |= b;
		int count = h->count[len];
		if (code - count < first) return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

static const short lenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short lenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short distBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                    6145, 8193, 12289, 16385, 24577 };
static const short distExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* Decodes one block's symbols into out. */
static bool codes(Bits* s, unsigned char* out, size_t* produced, size_t dstLen, const Huff* lit, const Huff* dist) {
	for (;;) {
		int sym = huff_decode(s, lit);
		if (sym < 0) return false;
		if (sym < 256) {
			if (*produced >= dstLen) return false;
			out[(*produced)++] = (unsigned char) sym;
		} else if (sym == 256) {
			return true;
		} else {
			sym -= 257;
			if (sym >= 29) return false;
			int len = lenBase[sym];
			int extra = lenExtra[sym];
			if (extra) {
				int v = bits_get(s, extra);
				if (v < 0) return false;
				len += v;
			}
			int dsym = huff_decode(s, dist);
			if (dsym < 0 || dsym >= 30) return false;
			size_t back = (size_t) distBase[dsym];
			int dextra = distExtra[dsym];
			if (dextra) {
				int v = bits_get(s, dextra);
				if (v < 0) return false;
				back += (size_t) v;
			}
			if (back > *produced || *produced + (size_t) len > dstLen) return false;
			const unsigned char* from = out + *produced - back;
			for (int i = 0; i < len; ++i) out[(*produced)++] = from[i]; /* may overlap: byte by byte */
		}
	}
}

static bool stored(Bits* s, unsigned char* out, size_t* produced, size_t dstLen) {
	int drop = s->bitcnt & 7; /* to a byte boundary */
	s->bitbuf >>= drop;
	s->bitcnt -= drop;
	int lo = bits_get(s, 16), hi = bits_get(s, 16);
	if (lo < 0 || hi < 0) return false;
	if ((lo ^ 0xFFFF) != hi) return false;
	if (*produced + (size_t) lo > dstLen) return false;
	for (int i = 0; i < lo; ++i) {
		int b = byte_get(s);
		if (b < 0) return false;
		out[(*produced)++] = (unsigned char) b;
	}
	return true;
}

static bool dynamic(Bits* s, unsigned char* out, size_t* produced, size_t dstLen) {
	static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	int hlit = bits_get(s, 5), hdist = bits_get(s, 5), hclen = bits_get(s, 4);
	if (hlit < 0 || hdist < 0 || hclen < 0) return false;
	int nlit = hlit + 257, ndist = hdist + 1, nclen = hclen + 4;
	if (nlit > 286 || ndist > 30) return false;
	unsigned char lengths[320];
	memset(lengths, 0, sizeof(lengths));
	for (int i = 0; i < nclen; ++i) {
		int v = bits_get(s, 3);
		if (v < 0) return false;
		lengths[order[i]] = (unsigned char) v;
	}
	Huff clen;
	if (huff_build(&clen, lengths, 19) != 1) return false;
	unsigned char all[320];
	int total = nlit + ndist, got = 0;
	while (got < total) {
		int sym = huff_decode(s, &clen);
		if (sym < 0) return false;
		if (sym < 16) {
			all[got++] = (unsigned char) sym;
		} else if (sym == 16) {
			if (!got) return false;
			int rep = 3 + (bits_get(s, 2));
			if (rep < 3) return false; /* bits_get failed */
			unsigned char prev = all[got - 1];
			while (rep-- && got < total) all[got++] = prev;
		} else if (sym == 17) {
			int rep = 3 + (bits_get(s, 3));
			if (rep < 3) return false;
			while (rep-- && got < total) all[got++] = 0;
		} else {
			int rep = 11 + (bits_get(s, 7));
			if (rep < 11) return false;
			while (rep-- && got < total) all[got++] = 0;
		}
	}
	Huff lit, dist;
	if (huff_build(&lit, all, nlit) != 1) return false;
	if (huff_build(&dist, all + nlit, ndist) < 0) return false;
	return codes(s, out, produced, dstLen, &lit, &dist);
}

static uint32_t adler32(const unsigned char* p, size_t n) {
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < n; ++i) {
		a = (a + p[i]) % 65521u;
		b = (b + a) % 65521u;
	}
	return (b << 16) | a;
}

bool zlib_inflate(const unsigned char* in, size_t inLen, unsigned char* out, size_t dstLen) {
	if (!in || !out || inLen < 6) return false;
	/* the zlib header: deflate, 32K window, no dictionary */
	int cmf = in[0], flg = in[1];
	if ((cmf & 0x0F) != 8) return false;
	if (((cmf << 8) | flg) % 31) return false;
	if (flg & 0x20) return false; /* a preset dictionary we don't have */
	Bits s = { in + 2, inLen - 2, 0, 0, 0 };
	size_t produced = 0;
	int last = 0;
	do {
		last = bits_get(&s, 1);
		int type = bits_get(&s, 2);
		if (last < 0 || type < 0) return false;
		bool ok = false;
		if (type == 0) {
			ok = stored(&s, out, &produced, dstLen);
		} else if (type == 1) {
			static Huff lit, dist;
			static bool built;
			if (!built) {
				unsigned char lengths[288];
				for (int i = 0; i < 144; ++i) lengths[i] = 8;
				for (int i = 144; i < 256; ++i) lengths[i] = 9;
				for (int i = 256; i < 280; ++i) lengths[i] = 7;
				for (int i = 280; i < 288; ++i) lengths[i] = 8;
				huff_build(&lit, lengths, 288);
				for (int i = 0; i < 30; ++i) lengths[i] = 5;
				huff_build(&dist, lengths, 30);
				built = true;
			}
			ok = codes(&s, out, &produced, dstLen, &lit, &dist);
		} else if (type == 2) {
			ok = dynamic(&s, out, &produced, dstLen);
		} else {
			return false;
		}
		if (!ok) return false;
	} while (!last);
	if (produced != dstLen) return false;
	/* Adler-32, after the last block */
	int drop = s.bitcnt & 7;
	s.bitbuf >>= drop;
	s.bitcnt -= drop;
	uint32_t want = 0;
	for (int i = 0; i < 4; ++i) {
		int b = byte_get(&s);
		if (b < 0) return false;
		want = (want << 8) | (uint32_t) b;
	}
	return want == adler32(out, dstLen);
}
