/*
 * Tiny PNG writer: see png_write.h. Uses stored (uncompressed) deflate
 * blocks, so the files are larger than usual but byte-for-byte deterministic.
 */
#include "png_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t s_crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        }
        s_crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc = s_crc_table[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
    }
    return crc;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static bool chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc_update(0xffffffffu, hdr + 4, 4);
    crc = crc_update(crc, data, len) ^ 0xffffffffu;
    uint8_t tail[4];
    be32(tail, crc);
    return fwrite(hdr, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len)
           && fwrite(tail, 1, 4, f) == 4;
}

bool png_write_rgb(const char *path, const uint8_t *rgb, int w, int h)
{
    crc_init();
    size_t row = (size_t)w * 3 + 1; /* filter byte + pixels */
    size_t raw_len = row * (size_t)h;
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_len);
    uint8_t *z = malloc(z_len);
    if (!raw || !z) {
        free(raw);
        free(z);
        return false;
    }
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * row] = 0;
        memcpy(raw + (size_t)y * row + 1, rgb + (size_t)y * (size_t)w * 3, (size_t)w * 3);
    }

    /* zlib stream of stored blocks, Adler-32 at the end. */
    size_t o = 0;
    z[o++] = 0x78;
    z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t pos = 0; pos < raw_len;) {
        size_t n = raw_len - pos < 65535 ? raw_len - pos : 65535;
        z[o++] = pos + n == raw_len ? 1 : 0;
        z[o++] = (uint8_t)n;
        z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n;
        z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + pos, n);
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[pos + i]) % 65521u;
            b = (b + a) % 65521u;
        }
        o += n;
        pos += n;
    }
    be32(z + o, (b << 16) | a);
    o += 4;

    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    FILE *f = fopen(path, "wb");
    bool ok = f != NULL;
    if (ok) {
        static const uint8_t SIG[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
        ok = fwrite(SIG, 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, 13) && chunk(f, "IDAT", z, (uint32_t)o)
             && chunk(f, "IEND", NULL, 0);
        ok = fclose(f) == 0 && ok;
    }
    free(raw);
    free(z);
    return ok;
}
