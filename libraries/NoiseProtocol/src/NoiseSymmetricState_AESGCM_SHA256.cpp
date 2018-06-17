/*
 * Copyright (C) 2018 Southern Storm Software, Pty Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "NoiseSymmetricState_AESGCM_SHA256.h"
#include "NoiseCipherState_AESGCM.h"
#include "SHA256.h"
#include "Crypto.h"
#include <string.h>

/**
 * \class NoiseSymmetricState_AESGCM_SHA256 NoiseSymmetricState_AESGCM_SHA256.h <NoiseSymmetricState_AESGCM_SHA256.h>
 * \brief Noise symmetric state implementation using AES256, GCM, and SHA256.
 */

/**
 * \brief Constructs a new symmetric state using AES256, GCM, and SHA256.
 */
NoiseSymmetricState_AESGCM_SHA256::NoiseSymmetricState_AESGCM_SHA256()
{
    st.n = 0;
    st.hasKey = false;
}

/**
 * \brief Destroys this symmetric state object.
 */
NoiseSymmetricState_AESGCM_SHA256::~NoiseSymmetricState_AESGCM_SHA256()
{
    clean(st);
}

void NoiseSymmetricState_AESGCM_SHA256::initialize
    (const char *protocolName)
{
    size_t len = strlen(protocolName);
    if (len <= 32) {
        memcpy(st.h, protocolName, len);
        memset(st.h + len, 0, 32 - len);
    } else {
        SHA256 hash;
        hash.update(protocolName, len);
        hash.finalize(st.h, 32);
    }
    memcpy(st.ck, st.h, 32);
    st.hasKey = false;
}

bool NoiseSymmetricState_AESGCM_SHA256::hasKey() const
{
    return st.hasKey;
}

void NoiseSymmetricState_AESGCM_SHA256::mixKey
    (const void *data, size_t size)
{
    uint8_t key[32];
    hmac(key, st.ck, data, size, 0);
    hmac(st.ck, key, 0, 0, 1);
    hmac(key, key, st.ck, 32, 2);
    st.hasKey = true;
    st.n = 0;
    cipher.setKey(key, sizeof(key));
    clean(key);
}

void NoiseSymmetricState_AESGCM_SHA256::mixHash
    (const void *data, size_t size)
{
    SHA256 hash;
    hash.update(st.h, sizeof(st.h));
    hash.update(data, size);
    hash.finalize(st.h, sizeof(st.h));
}

void NoiseSymmetricState_AESGCM_SHA256::mixKeyAndHash
    (const void *data, size_t size)
{
    uint8_t key[32];
    uint8_t temph[32];
    hmac(key, st.ck, data, size, 0);
    hmac(st.ck, key, 0, 0, 1);
    hmac(temph, key, st.ck, 32, 2);
    hmac(key, key, temph, 32, 3);
    st.hasKey = true;
    st.n = 0;
    cipher.setKey(key, sizeof(key));
    mixHash(temph, sizeof(temph));
    clean(key);
    clean(temph);
}

void NoiseSymmetricState_AESGCM_SHA256::getHandshakeHash
    (void *data, size_t size)
{
    if (size <= 32) {
        memcpy(data, st.h, size);
    } else {
        memcpy(data, st.h, 32);
        memset(((uint8_t *)data) + 32, 0, size - 32);
    }
}

/**
 * \brief Formats the 12-byte IV for use with AESGCM according
 * to the requirements of the Noise specification.
 *
 * \param iv Returns the formatted IV.
 * \param n 64-bit nonce value for the packet.
 */
void noiseAESGCMFormatIV(uint8_t iv[12], uint64_t n)
{
    iv[0]  = 0;
    iv[1]  = 0;
    iv[2]  = 0;
    iv[3]  = 0;
    iv[4]  = (uint8_t)(n >> 56);
    iv[5]  = (uint8_t)(n >> 48);
    iv[6]  = (uint8_t)(n >> 40);
    iv[7]  = (uint8_t)(n >> 32);
    iv[8]  = (uint8_t)(n >> 24);
    iv[9]  = (uint8_t)(n >> 16);
    iv[10] = (uint8_t)(n >> 8);
    iv[11] = (uint8_t)n;
}

int NoiseSymmetricState_AESGCM_SHA256::encryptAndHash
    (uint8_t *output, size_t outputSize,
     const uint8_t *input, size_t inputSize)
{
    if (st.hasKey) {
        if (outputSize < 16 || (outputSize - 16) < inputSize)
            return -1;
        uint8_t iv[12];
        noiseAESGCMFormatIV(iv, st.n);
        cipher.setIV(iv, sizeof(iv));
        cipher.addAuthData(st.h, sizeof(st.h));
        cipher.encrypt(output, input, inputSize);
        cipher.computeTag(output + inputSize, 16);
        mixHash(output, inputSize + 16);
        ++st.n;
        return inputSize + 16;
    } else {
        if (outputSize < inputSize)
            return -1;
        memcpy(output, input, inputSize);
        mixHash(output, inputSize);
        return inputSize;
    }
}

int NoiseSymmetricState_AESGCM_SHA256::decryptAndHash
    (uint8_t *output, size_t outputSize,
     const uint8_t *input, size_t inputSize)
{
    if (st.hasKey) {
        if (inputSize < 16 || outputSize < (inputSize - 16))
            return -1;
        outputSize = inputSize - 16;
        uint8_t iv[12];
        noiseAESGCMFormatIV(iv, st.n);
        cipher.setIV(iv, sizeof(iv));
        cipher.addAuthData(st.h, sizeof(st.h));
        mixHash(input, inputSize);
        cipher.decrypt(output, input, outputSize);
        if (cipher.checkTag(input + outputSize, 16)) {
            ++st.n;
            return outputSize;
        }
        memset(output, 0, outputSize); // Destroy output if tag is incorrect.
        return -1;
    } else {
        if (outputSize < inputSize)
            return -1;
        mixHash(input, inputSize);
        memcpy(output, input, inputSize);
        return inputSize;
    }
}

void NoiseSymmetricState_AESGCM_SHA256::split
    (NoiseCipherState **c1, NoiseCipherState **c2)
{
    uint8_t k1[32];
    uint8_t k2[32];
    hmac(k2, st.ck, 0, 0, 0);
    hmac(k1, k2, 0, 0, 1);
    hmac(k2, k2, k1, 32, 2);
    if (c1)
        *c1 = new NoiseCipherState_AESGCM(k1);
    if (c2)
        *c2 = new NoiseCipherState_AESGCM(k2);
    clean(k1);
    clean(k2);
}

void NoiseSymmetricState_AESGCM_SHA256::clear()
{
    clean(st);
    st.n = 0;
    st.hasKey = false;
}

void NoiseSymmetricState_AESGCM_SHA256::hmac
    (uint8_t *output, const uint8_t *key,
     const void *data, size_t size, uint8_t tag)
{
    SHA256 hash;
    hash.resetHMAC(key, 32);
    hash.update(data, size);
    if (tag != 0)
        hash.update(&tag, 1);
    hash.finalizeHMAC(key, 32, output, 32);
}
