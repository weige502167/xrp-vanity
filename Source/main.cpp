/**
 * (c) Ripple
 * (c) https://github.com/samr7/vanitygen
 * (c) Bitcoin
 */
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/ec.h>
#include <openssl/ripemd.h>
#include <openssl/rand.h>

#include "stdafx.hpp"

#include <stdio.h>

#ifdef _MSC_VER
FILE _iob[] = { *stdin, *stdout, *stderr };
extern "C" FILE * __cdecl __iob_func(void) {
    return _iob;
}
#endif

const unsigned int TOKEN_ACCOUNT_ID = 0;
const unsigned int TOKEN_FAMILY_SEED = 33;
const std::string  g_RippleAlphabet = "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";

EC_GROUP const* g_CurveGroup = EC_GROUP_new_by_curve_name(NID_secp256k1);
BIGNUM*         g_CurveOrder = BN_new();

BIGNUM  g_Base;

std::string baseEncode(std::uint8_t type, unsigned char* token, std::size_t size, BN_CTX* pCtx) {
    unsigned char hash1[32];
    int zpfx, d;
    BIGNUM bnrem;
    BIGNUM *bn, *bndiv, *bnptmp;
    size_t p;

    std::array<std::uint8_t, 40>    Address;

    BN_init(&bnrem);

    token[0] = type;

    // Hash the hash
    SHA256(token, size, hash1);
    SHA256(hash1, sizeof(hash1), hash1);

    // Write Check code
    memcpy((void*)&token[size], hash1, 4);

    bn = BN_new();
    bndiv = BN_new();

    BN_bin2bn(token, size + 4, bn);

    /* Compute the complete encoded address */
    for (zpfx = 0; zpfx < size + 4 && token[zpfx] == 0; zpfx++);
    p = Address.size() - 1;
    Address[p] = '\0';
    while (!BN_is_zero(bn)) {
        BN_div(bndiv, &bnrem, bn, &g_Base, pCtx);
        bnptmp = bn;
        bn = bndiv;
        bndiv = bnptmp;
        d = BN_get_word(&bnrem);
        Address[--p] = g_RippleAlphabet[d];
    }
    while (zpfx--) {
        Address[--p] = g_RippleAlphabet[0];
    }

    BN_free(bn);
    BN_free(bndiv);
    BN_free(&bnrem);

    return std::string(Address.begin() + p, Address.end());
}

void writeBE(uint8_t *pBuffer, std::uint32_t pValue) {

    *pBuffer++ = (pValue >> 24) & 0xFF;
    *pBuffer++ = (pValue >> 16) & 0xFF;
    *pBuffer++ = (pValue >> 8) & 0xFF;
    *pBuffer = (pValue >> 0) & 0xFF;
}

void findkey( const std::string pFindPrefix = "r" ) {
    std::array<std::uint8_t, 64> WorkBuffer;
    std::array<std::uint8_t, 64> WorkBufferPub;
    std::array<std::uint8_t, 21> SeedBuffer = { 0 };

    BN_CTX* Ctx = BN_CTX_new();

    BIGNUM* bnPrivateKey = BN_new();

    EC_POINT* ptRoot = EC_POINT_new(g_CurveGroup);
    EC_POINT* ptPublic = EC_POINT_new(g_CurveGroup);

    std::uint32_t seq, subSeq;

    // Ensure valid prefix pattern
    for ( auto ch : pFindPrefix) {

        if (g_RippleAlphabet.find(ch, 0) == std::string::npos) {
            std::cout << "Impossible pattern; Character: '" << ch << "'\n";
            exit(1);
        }
    }

    do {
        seq = 0;
        subSeq = 0;

        // Get some randoms
        if (!RAND_bytes(&SeedBuffer[1], 16)) {
            std::cout << "RAND_bytes failure\n";
            exit(1);
        }

        // generateRootDeterministicKey
        do {
            writeBE(&SeedBuffer[17], seq++);

            // SHA512-Half
            SHA512(&SeedBuffer[1], 20, &WorkBuffer[0]);
            BN_bin2bn(WorkBuffer.data(), 32, bnPrivateKey);

            // Valid Key?
        } while (BN_is_zero(bnPrivateKey) || BN_cmp(bnPrivateKey, g_CurveOrder) >= 0);

        // generateRootDeterministicPublicKey
        {
            EC_POINT_mul(g_CurveGroup, ptRoot, bnPrivateKey, nullptr, nullptr, Ctx);
            EC_POINT_point2oct(g_CurveGroup, ptRoot,
                POINT_CONVERSION_COMPRESSED,
                &WorkBuffer[0],
                33,
                Ctx);
        }

        // generatePublicDeterministicKey
        {
            BIGNUM* bnHash = BN_new();
            
            writeBE(&WorkBuffer[33], 0);
            do
            {
                writeBE(&WorkBuffer[37], subSeq++);

                SHA512(&WorkBuffer[0], 41, &WorkBufferPub[0]);
                BN_bin2bn(&WorkBufferPub[0], 32, bnHash);

            } while (BN_is_zero(bnHash) || BN_cmp(bnHash, g_CurveOrder) >= 0);

            EC_POINT_mul(g_CurveGroup, ptPublic, bnHash, nullptr, nullptr, Ctx);
            EC_POINT_add(g_CurveGroup, ptPublic, ptRoot, ptPublic, Ctx);
            BN_free(bnHash);

            EC_POINT_point2oct(g_CurveGroup, ptPublic,
                POINT_CONVERSION_COMPRESSED,
                &WorkBuffer[0],
                33,
                Ctx);
        }

        // Account ID
        SHA256(&WorkBuffer[0], 33, &WorkBuffer[0]);
        RIPEMD160(&WorkBuffer[0], 32, &WorkBuffer[1]);

        // Test pattern
        auto account = baseEncode(TOKEN_ACCOUNT_ID, &WorkBuffer[0], 21, Ctx);
        if (account.compare(0, pFindPrefix.length(), pFindPrefix) == 0) {

            auto t = std::time(nullptr);
            auto tm = *std::localtime(&t);
            std::cout << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S] ");

            std::cout << account << " => " << baseEncode(TOKEN_FAMILY_SEED, &SeedBuffer[0], 17, Ctx) << "\n";
        }

    } while (1);

    BN_free(bnPrivateKey);
    EC_POINT_free(ptRoot);
    EC_POINT_free(ptPublic);
}

int main(int argc, char *argv[])
{
    std::vector<std::thread> workers;
    BN_CTX*         Ctx = BN_CTX_new();

    BN_set_word(&g_Base, 58);
    EC_GROUP_get_order(g_CurveGroup, g_CurveOrder, Ctx);

    for (int i = 0; i < 1; i++) {
        workers.push_back(std::thread(findkey, "rob"));
    }

    std::for_each(workers.begin(), workers.end(), [](std::thread &t)
    {
        t.join();
    });
    return 1;
}
