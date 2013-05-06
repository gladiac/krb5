/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* lib/crypto/builtin/enc_provider/aes.c */
/*
 * Copyright (C) 2003, 2007, 2008 by the Massachusetts Institute of Technology.
 * All rights reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 */

#include "crypto_int.h"
#include "aes.h"

#define CHECK_SIZES 0

/*
 * Private per-key data to cache after first generation.  We don't
 * want to mess with the imported AES implementation too much, so
 * we'll just use two copies of its context, one for encryption and
 * one for decryption, and use the #rounds field as a flag for whether
 * we've initialized each half.
 */
struct aes_key_info_cache {
    aes_ctx enc_ctx, dec_ctx;
};
#define CACHE(X) ((struct aes_key_info_cache *)((X)->cache))

static inline void
enc(unsigned char *out, const unsigned char *in, aes_ctx *ctx)
{
    if (aes_enc_blk(in, out, ctx) != aes_good)
        abort();
}

static inline void
dec(unsigned char *out, const unsigned char *in, aes_ctx *ctx)
{
    if (aes_dec_blk(in, out, ctx) != aes_good)
        abort();
}

static void
xorblock(unsigned char *out, const unsigned char *in)
{
    int z;
    for (z = 0; z < BLOCK_SIZE/4; z++) {
        unsigned char *outptr = &out[z*4];
        const unsigned char *inptr = &in[z*4];
        /*
         * Use unaligned accesses.  On x86, this will probably still be faster
         * than multiple byte accesses for unaligned data, and for aligned data
         * should be far better.  (One test indicated about 2.4% faster
         * encryption for 1024-byte messages.)
         *
         * If some other CPU has really slow unaligned-word or byte accesses,
         * perhaps this function (or the load/store helpers?) should test for
         * alignment first.
         *
         * If byte accesses are faster than unaligned words, we may need to
         * conditionalize on CPU type, as that may be hard to determine
         * automatically.
         */
        store_32_n (load_32_n(outptr) ^ load_32_n(inptr), outptr);
    }
}

krb5_error_code
krb5int_aes_encrypt(krb5_key key, const krb5_data *ivec, krb5_crypto_iov *data,
                    size_t num_data)
{
    unsigned char tmp[BLOCK_SIZE], tmp2[BLOCK_SIZE];
    size_t input_length, nblocks, blockno;
    struct iov_cursor cursor;

    if (key->cache == NULL) {
        key->cache = malloc(sizeof(struct aes_key_info_cache));
        if (key->cache == NULL)
            return ENOMEM;
        CACHE(key)->enc_ctx.n_rnd = CACHE(key)->dec_ctx.n_rnd = 0;
    }
    if (CACHE(key)->enc_ctx.n_rnd == 0) {
        if (aes_enc_key(key->keyblock.contents, key->keyblock.length,
                        &CACHE(key)->enc_ctx)
            != aes_good)
            abort();
    }
    if (ivec != NULL)
        memcpy(tmp, ivec->data, BLOCK_SIZE);
    else
        memset(tmp, 0, BLOCK_SIZE);

    k5_iov_cursor_init(&cursor, data, num_data, BLOCK_SIZE, FALSE);

    input_length = iov_total_length(data, num_data, FALSE);
    nblocks = (input_length + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (nblocks == 1) {
        k5_iov_cursor_get(&cursor, tmp);
        enc(tmp2, tmp, &CACHE(key)->enc_ctx);
        k5_iov_cursor_put(&cursor, tmp2);
    } else if (nblocks > 1) {
        unsigned char blockN2[BLOCK_SIZE];   /* second last */
        unsigned char blockN1[BLOCK_SIZE];   /* last block */

        for (blockno = 0; blockno < nblocks - 2; blockno++) {
            unsigned char block[BLOCK_SIZE];

            k5_iov_cursor_get(&cursor, block);
            xorblock(tmp, block);
            enc(block, tmp, &CACHE(key)->enc_ctx);
            k5_iov_cursor_put(&cursor, block);

            /* Set up for next block.  */
            memcpy(tmp, block, BLOCK_SIZE);
        }

        /* Do final CTS step for last two blocks (the second of which
           may or may not be incomplete).  */

        /* First, get the last two blocks */
        k5_iov_cursor_get(&cursor, blockN2);
        k5_iov_cursor_get(&cursor, blockN1);

        /* Encrypt second last block */
        xorblock(tmp, blockN2);
        enc(tmp2, tmp, &CACHE(key)->enc_ctx);
        memcpy(blockN2, tmp2, BLOCK_SIZE); /* blockN2 now contains first block */
        memcpy(tmp, tmp2, BLOCK_SIZE);

        /* Encrypt last block */
        xorblock(tmp, blockN1);
        enc(tmp2, tmp, &CACHE(key)->enc_ctx);
        memcpy(blockN1, tmp2, BLOCK_SIZE);

        /* Put the last two blocks back into the iovec (reverse order) */
        k5_iov_cursor_put(&cursor, blockN1);
        k5_iov_cursor_put(&cursor, blockN2);

        if (ivec != NULL)
            memcpy(ivec->data, blockN1, BLOCK_SIZE);
    }

    return 0;
}

krb5_error_code
krb5int_aes_decrypt(krb5_key key, const krb5_data *ivec, krb5_crypto_iov *data,
                    size_t num_data)
{
    unsigned char tmp[BLOCK_SIZE], tmp2[BLOCK_SIZE], tmp3[BLOCK_SIZE];
    size_t input_length, nblocks, blockno;
    struct iov_cursor cursor;

    CHECK_SIZES;

    if (key->cache == NULL) {
        key->cache = malloc(sizeof(struct aes_key_info_cache));
        if (key->cache == NULL)
            return ENOMEM;
        CACHE(key)->enc_ctx.n_rnd = CACHE(key)->dec_ctx.n_rnd = 0;
    }
    if (CACHE(key)->dec_ctx.n_rnd == 0) {
        if (aes_dec_key(key->keyblock.contents, key->keyblock.length,
                        &CACHE(key)->dec_ctx) != aes_good)
            abort();
    }

    if (ivec != NULL)
        memcpy(tmp, ivec->data, BLOCK_SIZE);
    else
        memset(tmp, 0, BLOCK_SIZE);

    k5_iov_cursor_init(&cursor, data, num_data, BLOCK_SIZE, FALSE);

    input_length = iov_total_length(data, num_data, FALSE);
    nblocks = (input_length + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (nblocks == 1) {
        k5_iov_cursor_get(&cursor, tmp);
        dec(tmp2, tmp, &CACHE(key)->dec_ctx);
        k5_iov_cursor_put(&cursor, tmp2);
    } else if (nblocks > 1) {
        unsigned char blockN2[BLOCK_SIZE];   /* second last */
        unsigned char blockN1[BLOCK_SIZE];   /* last block */

        for (blockno = 0; blockno < nblocks - 2; blockno++) {
            unsigned char block[BLOCK_SIZE];

            k5_iov_cursor_get(&cursor, block);
            memcpy(tmp2, block, BLOCK_SIZE);
            dec(block, block, &CACHE(key)->dec_ctx);
            xorblock(block, tmp);
            memcpy(tmp, tmp2, BLOCK_SIZE);
            k5_iov_cursor_put(&cursor, block);
        }

        /* Do last two blocks, the second of which (next-to-last block
           of plaintext) may be incomplete.  */

        /* First, get the last two encrypted blocks */
        k5_iov_cursor_get(&cursor, blockN2);
        k5_iov_cursor_get(&cursor, blockN1);

        if (ivec != NULL)
            memcpy(ivec->data, blockN2, BLOCK_SIZE);

        /* Decrypt second last block */
        dec(tmp2, blockN2, &CACHE(key)->dec_ctx);
        /* Set tmp2 to last (possibly partial) plaintext block, and
           save it.  */
        xorblock(tmp2, blockN1);
        memcpy(blockN2, tmp2, BLOCK_SIZE);

        /* Maybe keep the trailing part, and copy in the last
           ciphertext block.  */
        input_length %= BLOCK_SIZE;
        memcpy(tmp2, blockN1, input_length ? input_length : BLOCK_SIZE);
        dec(tmp3, tmp2, &CACHE(key)->dec_ctx);
        xorblock(tmp3, tmp);
        memcpy(blockN1, tmp3, BLOCK_SIZE);

        /* Put the last two blocks back into the iovec */
        k5_iov_cursor_put(&cursor, blockN1);
        k5_iov_cursor_put(&cursor, blockN2);
    }

    return 0;
}

static krb5_error_code
aes_init_state(const krb5_keyblock *key, krb5_keyusage usage,
               krb5_data *state)
{
    state->length = 16;
    state->data = malloc(16);
    if (state->data == NULL)
        return ENOMEM;
    memset(state->data, 0, state->length);
    return 0;
}

static void
aes_key_cleanup(krb5_key key)
{
    zapfree(key->cache, sizeof(struct aes_key_info_cache));
}

const struct krb5_enc_provider krb5int_enc_aes128 = {
    16,
    16, 16,
    krb5int_aes_encrypt,
    krb5int_aes_decrypt,
    NULL,
    aes_init_state,
    krb5int_default_free_state,
    aes_key_cleanup
};

const struct krb5_enc_provider krb5int_enc_aes256 = {
    16,
    32, 32,
    krb5int_aes_encrypt,
    krb5int_aes_decrypt,
    NULL,
    aes_init_state,
    krb5int_default_free_state,
    aes_key_cleanup
};
