/**
 * tcu_comm process implementation
 * BY ZF
 */

#include "comm_process.h"
#include "../../base/common/message_queue.h"
#include "../../base/cjson/include/cjson/cJSON.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/asn1.h>

#ifndef NID_sm2
#ifdef NID_sm2p256v1
#define NID_sm2 NID_sm2p256v1
#endif
#endif

namespace {
    void feedDaemonWatchdog()
    {
        // BY ZF: 通过守护进程看门狗消息队列上报 tcu_comm 存活状态。
        static MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
        static int queueReady = -1;
        if (queueReady == -1) {
            queueReady = watchdogQueue.open() ? 1 : (watchdogQueue.create() ? 1 : 0);
        }
        if (queueReady == 1) {
            const char* processName = "tcu_comm";
            watchdogQueue.send(MSG_WATCHDOG_FEED, processName, strlen(processName));
        }
    }

    // BY ZF: NYC南瑞平台帧类型（固定值，不走配置）。
    // BY ZF: 握手交互。
    static const uint8_t kCmdRequestRsaPublicKey = 0x01;
    static const uint8_t kCmdDeviceAuth = 0x02;
    static const uint8_t kCmdHeartbeat = 0x07;
    static const uint8_t kCmdTimeSync = 0x0A;

    // BY ZF: 监测数据。
    static const uint8_t kCmdTotalCall = 0x10;
    static const uint8_t kCmdAllTelemetry = 0x11;
    static const uint8_t kCmdAllTelesignal = 0x12;
    static const uint8_t kCmdChangeTelesignal = 0x16;
    static const uint8_t kCmdChangeTelemetry = 0x17;
    static const uint8_t kCmdAlarm = 0x18;

    // BY ZF: 高级应用管理。
    static const uint8_t kCmdBmsInfo = 0x21;
    static const uint8_t kCmdChargeStageEvent = 0x22;
    static const uint8_t kCmdRawMessagePassThrough = 0x23;
    static const uint8_t kCmdChargeCurveCall = 0x24;

    // BY ZF: 充电过程控制。
    static const uint8_t kCmdRemoteStartCmd = 0x41;
    static const uint8_t kCmdUploadTradeRecord = 0x42;
    static const uint8_t kCmdRemoteStopCmd = 0x43;
    static const uint8_t kCmdCardChargeReq = 0x44;
    static const uint8_t kCmdPlugChargeAuth = 0x45;
    static const uint8_t kCmdGenericRemoteAdjust = 0x46;

    // BY ZF: 档案管理。
    static const uint8_t kCmdPileArchiveCall = 0x71;
    static const uint8_t kCmdFirmwarePush = 0x72;
    static const uint8_t kCmdFeeModelReq = 0x73;
    static const uint8_t kCmdFeeModelPush = 0x74;

    // BY ZF: NYC加密方式定义。
    static const uint8_t kEncryptPlain = 0x00;
    static const uint8_t kEncryptAes = 0x01;
    static const uint8_t kEncryptRsaPublic = 0x02;
    static const uint8_t kEncryptRsaPrivate = 0x03;

    //固定充电桩/枪类型
    static const uint8_t kFixedChargerType = 0x01;
    static const uint8_t kFixedGunType = 0x01;

    // BY ZF: 登录秘钥(8字节ASCII)生成（优先使用loginId，回退到cdzNo）。
    // BY ZF: 允许字母+数字，不再仅保留数字。
    static std::string __attribute__((unused)) deriveLoginSecret8(const std::string& seedId)
    {
        std::string text;
        for (size_t i = 0; i < seedId.size(); ++i) {
            if ((seedId[i] >= '0' && seedId[i] <= '9') ||
                (seedId[i] >= 'A' && seedId[i] <= 'Z') ||
                (seedId[i] >= 'a' && seedId[i] <= 'z')) {
                text.push_back(seedId[i]);
            }
        }
        if (text.size() > 8) {
            text = text.substr(text.size() - 8);
        }
        if (text.size() < 8) {
            text.insert(text.begin(), 8 - text.size(), '0');
        }
        return text;
    }

    // BY ZF: 桩编码 -> 7字节BCD，不足7字节补0（取数字字符）。
    static void appendPileCodeBcd7(std::vector<uint8_t>& out, const std::string& cdzNo)
    {
        std::string digits;
        for (size_t i = 0; i < cdzNo.size(); ++i) {
            if (cdzNo[i] >= '0' && cdzNo[i] <= '9') {
                digits.push_back(cdzNo[i]);
            }
        }
        if (digits.size() > 14) {
            digits = digits.substr(digits.size() - 14);
        }
        if (digits.size() < 14) {
            digits.insert(digits.begin(), 14 - digits.size(), '0');
        }
        for (size_t i = 0; i < 14; i += 2) {
            const uint8_t hi = static_cast<uint8_t>(digits[i] - '0');
            const uint8_t lo = static_cast<uint8_t>(digits[i + 1] - '0');
            out.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
    }

    // BY ZF: Base64编码（无换行）。
    static std::string base64Encode(const std::vector<uint8_t>& data)
    {
        static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((data.size() + 2U) / 3U) * 4U);
        size_t i = 0;
        while (i + 3 <= data.size()) {
            const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                               (static_cast<uint32_t>(data[i + 1]) << 8) |
                               static_cast<uint32_t>(data[i + 2]);
            out.push_back(tbl[(v >> 18) & 0x3F]);
            out.push_back(tbl[(v >> 12) & 0x3F]);
            out.push_back(tbl[(v >> 6) & 0x3F]);
            out.push_back(tbl[v & 0x3F]);
            i += 3;
        }
        const size_t rem = data.size() - i;
        if (rem == 1) {
            const uint32_t v = static_cast<uint32_t>(data[i]) << 16;
            out.push_back(tbl[(v >> 18) & 0x3F]);
            out.push_back(tbl[(v >> 12) & 0x3F]);
            out.push_back('=');
            out.push_back('=');
        } else if (rem == 2) {
            const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                               (static_cast<uint32_t>(data[i + 1]) << 8);
            out.push_back(tbl[(v >> 18) & 0x3F]);
            out.push_back(tbl[(v >> 12) & 0x3F]);
            out.push_back(tbl[(v >> 6) & 0x3F]);
            out.push_back('=');
        }
        return out;
    }

    static bool hexToBytes(const std::string& in, std::vector<uint8_t>& out)
    {
        std::string s;
        s.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (std::isxdigit(static_cast<unsigned char>(in[i])) != 0) {
                s.push_back(in[i]);
            }
        }
        if ((s.size() % 2U) != 0U) {
            return false;
        }
        out.clear();
        out.reserve(s.size() / 2U);
        for (size_t i = 0; i < s.size(); i += 2U) {
            const char hc = s[i];
            const char lc = s[i + 1U];
            const int hi = (hc >= '0' && hc <= '9') ? (hc - '0') :
                           (hc >= 'A' && hc <= 'F') ? (hc - 'A' + 10) :
                           (hc >= 'a' && hc <= 'f') ? (hc - 'a' + 10) : -1;
            const int lo = (lc >= '0' && lc <= '9') ? (lc - '0') :
                           (lc >= 'A' && lc <= 'F') ? (lc - 'A' + 10) :
                           (lc >= 'a' && lc <= 'f') ? (lc - 'a' + 10) : -1;
            if (hi < 0 || lo < 0) {
                return false;
            }
            out.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
        return !out.empty();
    }

    static RSA* loadRsaPublicKey(const std::string& rsaPubKey)
    {
        if (rsaPubKey.empty()) {
            return nullptr;
        }

        BIO* mem = BIO_new_mem_buf(rsaPubKey.data(), static_cast<int>(rsaPubKey.size()));
        if (mem) {
            // BY ZF: 旧实现 wc_RsaPublicKeyDecode 对应 PKCS#1 RSAPublicKey，优先按该格式解析。
            RSA* rsa = PEM_read_bio_RSAPublicKey(mem, nullptr, nullptr, nullptr);
            BIO_free(mem);
            if (rsa) {
                return rsa;
            }
        }

        mem = BIO_new_mem_buf(rsaPubKey.data(), static_cast<int>(rsaPubKey.size()));
        if (mem) {
            RSA* rsa = PEM_read_bio_RSA_PUBKEY(mem, nullptr, nullptr, nullptr);
            BIO_free(mem);
            if (rsa) {
                return rsa;
            }
        }

        std::vector<uint8_t> raw;
        if (!hexToBytes(rsaPubKey, raw)) {
            return nullptr;
        }

        const unsigned char* p = raw.data();
        RSA* rsa = d2i_RSAPublicKey(nullptr, &p, static_cast<long>(raw.size()));
        if (rsa) {
            return rsa;
        }

        p = raw.data();
        rsa = d2i_RSA_PUBKEY(nullptr, &p, static_cast<long>(raw.size()));
        if (rsa) {
            return rsa;
        }

        if (raw.size() == 128U || raw.size() == 256U) {
            rsa = RSA_new();
            BIGNUM* n = BN_bin2bn(raw.data(), static_cast<int>(raw.size()), nullptr);
            BIGNUM* e = BN_new();
            if (rsa && n && e && BN_set_word(e, RSA_F4) == 1 && RSA_set0_key(rsa, n, e, nullptr) == 1) {
                return rsa;
            }
            if (n) {
                BN_free(n);
            }
            if (e) {
                BN_free(e);
            }
            if (rsa) {
                RSA_free(rsa);
            }
        }
        return nullptr;
    }

    static bool rsaPublicEncryptPkcs1(const std::string& rsaPubKey,
                                      const std::vector<uint8_t>& plain,
                                      std::vector<uint8_t>& out)
    {
        RSA* rsa = loadRsaPublicKey(rsaPubKey);
        if (!rsa) {
            return false;
        }
        const int rsaSize = RSA_size(rsa);
        const int maxBlock = rsaSize - 11;
        if (rsaSize <= 0 || maxBlock <= 0) {
            RSA_free(rsa);
            return false;
        }

        out.clear();
        for (size_t off = 0; off < plain.size(); off += static_cast<size_t>(maxBlock)) {
            const size_t chunk = std::min(static_cast<size_t>(maxBlock), plain.size() - off);
            std::vector<uint8_t> block(static_cast<size_t>(rsaSize), 0U);
            const int n = RSA_public_encrypt(static_cast<int>(chunk),
                                             plain.data() + off,
                                             block.data(),
                                             rsa,
                                             RSA_PKCS1_PADDING);
            if (n <= 0) {
                RSA_free(rsa);
                return false;
            }
            out.insert(out.end(), block.begin(), block.begin() + n);
        }
        RSA_free(rsa);
        return true;
    }

    static bool rsaPublicDecryptPkcs1(const std::string& rsaPubKey,
                                      const uint8_t* in,
                                      size_t inLen,
                                      std::vector<uint8_t>& out)
    {
        RSA* rsa = loadRsaPublicKey(rsaPubKey);
        if (!rsa) {
            return false;
        }
        const int rsaSize = RSA_size(rsa);
        if (rsaSize <= 0 || inLen == 0U || (inLen % static_cast<size_t>(rsaSize)) != 0U) {
            RSA_free(rsa);
            return false;
        }

        out.clear();
        for (size_t off = 0; off < inLen; off += static_cast<size_t>(rsaSize)) {
            std::vector<uint8_t> block(static_cast<size_t>(rsaSize), 0U);
            const int n = RSA_public_decrypt(rsaSize,
                                             in + off,
                                             block.data(),
                                             rsa,
                                             RSA_PKCS1_PADDING);
            if (n < 0) {
                RSA_free(rsa);
                return false;
            }
            out.insert(out.end(), block.begin(), block.begin() + n);
        }
        RSA_free(rsa);
        return true;
    }

    static EVP_PKEY* loadSm2PublicKey(const std::string& sm2PubKey)
    {
        if (sm2PubKey.empty()) {
            return nullptr;
        }

        BIO* mem = BIO_new_mem_buf(sm2PubKey.data(), static_cast<int>(sm2PubKey.size()));
        if (mem) {
            EVP_PKEY* pkey = PEM_read_bio_PUBKEY(mem, nullptr, nullptr, nullptr);
            BIO_free(mem);
            if (pkey) {
#ifdef EVP_PKEY_SM2
                // BY ZF: OpenSSL 1.1.1 上将 EC 公钥别名为 SM2，确保 EVP_PKEY_encrypt 可用。
                EVP_PKEY_set_alias_type(pkey, EVP_PKEY_SM2);
#endif
                return pkey;
            }
        }

        std::vector<uint8_t> raw;
        if (!hexToBytes(sm2PubKey, raw)) {
            return nullptr;
        }
        if (raw.size() == 64U) {
            raw.insert(raw.begin(), 0x04U); // BY ZF: 兼容仅X||Y格式公钥。
        }
        if (raw.size() < 65U || raw[0] != 0x04U) {
            return nullptr;
        }

        EC_KEY* ec = EC_KEY_new_by_curve_name(NID_sm2);
        if (!ec) {
            return nullptr;
        }
        const EC_GROUP* group = EC_KEY_get0_group(ec);
        EC_POINT* point = EC_POINT_new(group);
        BN_CTX* bctx = BN_CTX_new();
        if (!point || !bctx ||
            EC_POINT_oct2point(group, point, raw.data(), raw.size(), bctx) != 1 ||
            EC_KEY_set_public_key(ec, point) != 1) {
            if (point) {
                EC_POINT_free(point);
            }
            if (bctx) {
                BN_CTX_free(bctx);
            }
            EC_KEY_free(ec);
            return nullptr;
        }

        EC_POINT_free(point);
        BN_CTX_free(bctx);

        EVP_PKEY* pkey = EVP_PKEY_new();
        if (!pkey || EVP_PKEY_assign_EC_KEY(pkey, ec) != 1) {
            if (pkey) {
                EVP_PKEY_free(pkey);
            } else {
                EC_KEY_free(ec);
            }
            return nullptr;
        }
#ifdef EVP_PKEY_SM2
        // BY ZF: OpenSSL 1.1.1 上将 EC 公钥别名为 SM2，确保 EVP_PKEY_encrypt 可用。
        EVP_PKEY_set_alias_type(pkey, EVP_PKEY_SM2);
#endif
        return pkey;
    }

    // BY ZF: SM2加密（输出Base64 ASCII）。
    static std::string __attribute__((unused)) sm2EncryptToAscii(const std::vector<uint8_t>& plain,
                                                                 const std::string& sm2PubKey)
    {
        if (plain.empty()) {
            return std::string();
        }
        EVP_PKEY* pkey = loadSm2PublicKey(sm2PubKey);
        if (!pkey) {
            return std::string();
        }

        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new(pkey, nullptr);
        if (!pctx) {
            EVP_PKEY_free(pkey);
            return std::string();
        }

        std::string out;
        if (EVP_PKEY_encrypt_init(pctx) == 1) {
            size_t outLen = 0;
            if (EVP_PKEY_encrypt(pctx, nullptr, &outLen, plain.data(), plain.size()) == 1 && outLen > 0U) {
                std::vector<uint8_t> derCipher(outLen);
                if (EVP_PKEY_encrypt(pctx, derCipher.data(), &outLen, plain.data(), plain.size()) == 1 && outLen > 0U) {
                    derCipher.resize(outLen);

                    // BY ZF: OpenSSL SM2默认产物是ASN.1 DER，这里转换为平台要求的C1C3C2再做Base64。
                    const unsigned char* pp = derCipher.data();
                    STACK_OF(ASN1_TYPE)* seq = d2i_ASN1_SEQUENCE_ANY(nullptr, &pp, static_cast<long>(derCipher.size()));
                    if (seq && sk_ASN1_TYPE_num(seq) == 4) {
                        ASN1_TYPE* t0 = sk_ASN1_TYPE_value(seq, 0); // x
                        ASN1_TYPE* t1 = sk_ASN1_TYPE_value(seq, 1); // y
                        ASN1_TYPE* t2 = sk_ASN1_TYPE_value(seq, 2); // c3
                        ASN1_TYPE* t3 = sk_ASN1_TYPE_value(seq, 3); // c2
                        if (t0 && t1 && t2 && t3 &&
                            t0->type == V_ASN1_INTEGER &&
                            t1->type == V_ASN1_INTEGER &&
                            t2->type == V_ASN1_OCTET_STRING &&
                            t3->type == V_ASN1_OCTET_STRING) {
                            std::vector<uint8_t> raw;
                            raw.reserve(1U + 32U + 32U + static_cast<size_t>(t2->value.octet_string->length) +
                                        static_cast<size_t>(t3->value.octet_string->length));
                            raw.push_back(0x04U); // C1未压缩点前缀

                            BIGNUM* bx = ASN1_INTEGER_to_BN(t0->value.integer, nullptr);
                            BIGNUM* by = ASN1_INTEGER_to_BN(t1->value.integer, nullptr);
                            uint8_t xy[64] = {0};
                            const int okx = bx ? BN_bn2binpad(bx, xy, 32) : -1;
                            const int oky = by ? BN_bn2binpad(by, xy + 32, 32) : -1;
                            if (bx) BN_free(bx);
                            if (by) BN_free(by);
                            if (okx == 32 && oky == 32) {
                                raw.insert(raw.end(), xy, xy + 64); // C1

                                const unsigned char* c3 = ASN1_STRING_get0_data(t2->value.octet_string);
                                const int c3len = ASN1_STRING_length(t2->value.octet_string);
                                const unsigned char* c2 = ASN1_STRING_get0_data(t3->value.octet_string);
                                const int c2len = ASN1_STRING_length(t3->value.octet_string);
                                if (c3 && c2 && c3len > 0 && c2len >= 0) {
                                    raw.insert(raw.end(), c3, c3 + c3len); // C3
                                    raw.insert(raw.end(), c2, c2 + c2len); // C2
                                    out = base64Encode(raw);
                                }
                            }
                        }
                    }
                    if (seq) {
                        sk_ASN1_TYPE_pop_free(seq, ASN1_TYPE_free);
                    }
                }
            }
        }

        EVP_PKEY_CTX_free(pctx);
        EVP_PKEY_free(pkey);
        return out;
    }

    static bool __attribute__((unused)) sm4CbcEncryptPkcs7(const uint8_t key[16], const std::vector<uint8_t>& plain, std::vector<uint8_t>& out)
    {
        out.clear();
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            return false;
        }
        int ok = EVP_EncryptInit_ex(ctx, EVP_sm4_cbc(), nullptr, key, key);
        if (ok != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        std::vector<uint8_t> buf(plain.size() + 32U, 0U);
        int outLen1 = 0;
        ok = EVP_EncryptUpdate(ctx, buf.data(), &outLen1, plain.data(), static_cast<int>(plain.size()));
        if (ok != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        int outLen2 = 0;
        ok = EVP_EncryptFinal_ex(ctx, buf.data() + outLen1, &outLen2);
        EVP_CIPHER_CTX_free(ctx);
        if (ok != 1) {
            return false;
        }
        buf.resize(static_cast<size_t>(outLen1 + outLen2));
        out.swap(buf);
        return true;
    }

    static bool __attribute__((unused)) sm4CbcDecryptPkcs7(const uint8_t key[16], const uint8_t* in, size_t inLen, std::vector<uint8_t>& out)
    {
        out.clear();
        if (!in || inLen == 0U) {
            return true;
        }
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            return false;
        }
        int ok = EVP_DecryptInit_ex(ctx, EVP_sm4_cbc(), nullptr, key, key);
        if (ok != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        std::vector<uint8_t> buf(inLen + 32U, 0U);
        int outLen1 = 0;
        ok = EVP_DecryptUpdate(ctx, buf.data(), &outLen1, in, static_cast<int>(inLen));
        if (ok != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        int outLen2 = 0;
        ok = EVP_DecryptFinal_ex(ctx, buf.data() + outLen1, &outLen2);
        EVP_CIPHER_CTX_free(ctx);
        if (ok != 1) {
            return false;
        }
        buf.resize(static_cast<size_t>(outLen1 + outLen2));
        out.swap(buf);
        return true;
    }

    static bool aes128EcbEncryptPkcs5(const uint8_t key[16], const std::vector<uint8_t>& plain, std::vector<uint8_t>& out)
    {
        out.clear();
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            return false;
        }
        int ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key, nullptr);
        if (ok != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        std::vector<uint8_t> buf(plain.size() + 16U, 0U);
        int outLen1 = 0;
        ok = EVP_EncryptUpdate(ctx, buf.data(), &outLen1, plain.data(), static_cast<int>(plain.size()));
        if (ok != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        int outLen2 = 0;
        ok = EVP_EncryptFinal_ex(ctx, buf.data() + outLen1, &outLen2);
        EVP_CIPHER_CTX_free(ctx);
        if (ok != 1) {
            return false;
        }
        buf.resize(static_cast<size_t>(outLen1 + outLen2));
        out.swap(buf);
        return true;
    }

    static bool aes128EcbDecryptPkcs5(const uint8_t key[16], const uint8_t* in, size_t inLen, std::vector<uint8_t>& out)
    {
        out.clear();
        if (!in || inLen == 0U) {
            return true;
        }
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            return false;
        }
        int ok = EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key, nullptr);
        if (ok != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        std::vector<uint8_t> buf(inLen + 16U, 0U);
        int outLen1 = 0;
        ok = EVP_DecryptUpdate(ctx, buf.data(), &outLen1, in, static_cast<int>(inLen));
        if (ok != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        int outLen2 = 0;
        ok = EVP_DecryptFinal_ex(ctx, buf.data() + outLen1, &outLen2);
        EVP_CIPHER_CTX_free(ctx);
        if (ok != 1) {
            return false;
        }
        buf.resize(static_cast<size_t>(outLen1 + outLen2));
        out.swap(buf);
        return true;
    }

    // BY ZF: 按帧类型判断消息体是否需要加密。
    // BY ZF: NYC握手/心跳/对时帧先按明文处理，后续按加密章节细化认证后的数据帧。
    static bool shouldEncryptBody(uint8_t cmd)
    {
        switch (cmd) {
        case kCmdRequestRsaPublicKey:
        case kCmdHeartbeat:
        case kCmdTimeSync:
        case kCmdAllTelemetry:
        case kCmdAllTelesignal:
        case kCmdChangeTelemetry:
            return false;
        default:
            return true;
        }
    }


    static bool encryptBodyByCmd(uint8_t cmd,
                                 const std::vector<uint8_t>& plain,
                                 const std::array<uint8_t, 16>& key,
                                 bool keyReady,
                                 const std::string& rsaPubKey,
                                 std::vector<uint8_t>& out,
                                 uint8_t& encryptFlag)
    {
        if (cmd == kCmdDeviceAuth) {
            encryptFlag = kEncryptRsaPublic;
            return rsaPublicEncryptPkcs1(rsaPubKey, plain, out);
        }
        if (!shouldEncryptBody(cmd)) {
            encryptFlag = kEncryptPlain;
            out = plain;
            return true;
        }
        if (!keyReady) {
            return false;
        }
        encryptFlag = kEncryptAes;
        return aes128EcbEncryptPkcs5(key.data(), plain, out);
    }

    static bool decryptBodyByFlag(uint8_t encryptFlag,
                                  const uint8_t* body,
                                  size_t bodyLen,
                                  const std::array<uint8_t, 16>& key,
                                  bool keyReady,
                                  const std::string& rsaPubKey,
                                  std::vector<uint8_t>& out)
    {
        if (!body || bodyLen == 0U) {
            out.clear();
            return true;
        }
        if (encryptFlag == kEncryptPlain) {
            out.assign(body, body + bodyLen);
            return true;
        }
        if (encryptFlag == kEncryptAes) {
            if (!keyReady) {
                return false;
            }
            return aes128EcbDecryptPkcs5(key.data(), body, bodyLen, out);
        }
        if (encryptFlag == kEncryptRsaPrivate) {
            return rsaPublicDecryptPkcs1(rsaPubKey, body, bodyLen, out);
        }
        if (encryptFlag == kEncryptRsaPublic) {
            return false;
        }
        return false;
    }

    static std::vector<std::string> split(const std::string& s, char ch)
    {
        std::vector<std::string> out;
        std::string cur;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == ch) {
                out.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(s[i]);
            }
        }
        out.push_back(cur);
        return out;
    }

    // BY ZF: 任意位数字字符串按固定BCD字节长度写入（左侧补0，超长截断高位）。
    static void appendBcdFixed(std::vector<uint8_t>& out, const std::string& text, size_t bcdBytes)
    {
        std::string digits;
        digits.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] >= '0' && text[i] <= '9') {
                digits.push_back(text[i]);
            }
        }
        const size_t needDigits = bcdBytes * 2U;
        if (digits.size() > needDigits) {
            digits = digits.substr(digits.size() - needDigits);
        }
        if (digits.size() < needDigits) {
            digits.insert(digits.begin(), needDigits - digits.size(), '0');
        }
        for (size_t i = 0; i < needDigits; i += 2) {
            const uint8_t hi = static_cast<uint8_t>(digits[i] - '0');
            const uint8_t lo = static_cast<uint8_t>(digits[i + 1] - '0');
            out.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
    }

    // BY ZF: JSON 字段读取工具。
    static bool jsonGetNumber(cJSON* obj, const char* key, double& out)
    {
        cJSON* n = cJSON_GetObjectItem(obj, key);
        if (n && cJSON_IsNumber(n)) {
            out = n->valuedouble;
            return true;
        }
        return false;
    }

    static bool jsonGetInt(cJSON* obj, const char* key, int& out)
    {
        cJSON* n = cJSON_GetObjectItem(obj, key);
        if (n && cJSON_IsNumber(n)) {
            out = n->valueint;
            return true;
        }
        return false;
    }

    static std::string jsonGetString(cJSON* obj, const char* key)
    {
        cJSON* s = cJSON_GetObjectItem(obj, key);
        if (s && cJSON_IsString(s) && s->valuestring) {
            return std::string(s->valuestring);
        }
        return std::string();
    }

    static std::string sanitizeVin17(const std::string& vinText)
    {
        std::string vin;
        vin.reserve(17U);
        for (size_t i = 0; i < vinText.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(vinText[i]);
            if (std::isalnum(ch)) {
                vin.push_back(static_cast<char>(std::toupper(ch)));
            }
            if (vin.size() >= 17U) {
                break;
            }
        }
        return vin;
    }

    static std::string jsonGetStringCompat(cJSON* obj, const char* key1, const char* key2)
    {
        std::string text = jsonGetString(obj, key1);
        if (text.empty() && key2) {
            text = jsonGetString(obj, key2);
        }
        return text;
    }

    // BY ZF: 浮点缩放并四舍五入到无符号整数。
    static uint32_t scaleToU32(double v, double scale)
    {
        if (v <= 0.0) {
            return 0U;
        }
        return static_cast<uint32_t>(v * scale + 0.5);
    }

    // BY ZF: 小端序追加工具（中石化BIN字段按小端组织）。
    static void appendU16LE(std::vector<uint8_t>& out, uint16_t v)
    {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }

    static void appendU32LE(std::vector<uint8_t>& out, uint32_t v)
    {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }

    static uint32_t readU32LE(const uint8_t* p)
    {
        if (!p) {
            return 0U;
        }
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }

    // BY ZF: 平台命令字转名称，便于TCP调试输出。
    static const char* platformCmdName(uint8_t cmd)
    {
        switch (cmd) {
        case kCmdRequestRsaPublicKey: return "request_rsa_public_key";
        case kCmdDeviceAuth: return "device_auth";
        case kCmdHeartbeat: return "heartbeat";
        case kCmdTimeSync: return "time_sync";
        case kCmdTotalCall: return "total_call";
        case kCmdAllTelemetry: return "all_telemetry";
        case kCmdAllTelesignal: return "all_telesignal";
        case kCmdChangeTelesignal: return "change_telesignal";
        case kCmdChangeTelemetry: return "change_telemetry";
        case kCmdAlarm: return "alarm";
        case kCmdBmsInfo: return "bms_info";
        case kCmdChargeStageEvent: return "charge_stage_event";
        case kCmdRawMessagePassThrough: return "raw_message_pass_through";
        case kCmdChargeCurveCall: return "charge_curve_call";
        case kCmdRemoteStartCmd: return "remote_start_cmd";
        case kCmdUploadTradeRecord: return "upload_trade_record";
        case kCmdRemoteStopCmd: return "remote_stop_cmd";
        case kCmdCardChargeReq: return "card_charge_req";
        case kCmdPlugChargeAuth: return "plug_charge_auth";
        case kCmdGenericRemoteAdjust: return "generic_remote_adjust";
        case kCmdPileArchiveCall: return "pile_archive_call";
        case kCmdFirmwarePush: return "firmware_push";
        case kCmdFeeModelReq: return "fee_model_req";
        case kCmdFeeModelPush: return "fee_model_push";
        default: return "unknown";
        }
    }
}

CommProcess::CommProcess()
    : BaseProcess(PROC_COMMUNICATION, "tcu_comm")
    , m_logSender("tcu_comm")
    , m_seq(0)
    , m_platformConnected(false)
    , m_tcpFd(-1)
    , m_loginState(LOGIN_IDLE)
    , m_lastChargeInfoReport(std::chrono::steady_clock::now())
    , m_lastTelemetryReport(std::chrono::steady_clock::now())
    , m_lastPeriodicSetConfigPublish(std::chrono::steady_clock::now())
    , m_heartbeatCounter(0)
    , m_aesSessionKeyReady(false)
    , m_loginCryptoPrepared(false)
    , m_platformOnlineEventActive(false)
{
    m_aesSessionKey.fill(0);
}

CommProcess::~CommProcess()
{
    doCleanup();
}

bool CommProcess::doInitialize()
{
    if (!loadConfig()) {
        return false;
    }
    if (m_config.offlineRunMode) {
        // BY ZF: 离线模式下平台链路状态对外恒定为在线。
        m_platformOnlineEventActive = true;
    }
    if (!initMqtt()) {
        return false;
    }
    m_lastSetConfigPublishByGun.assign(static_cast<size_t>(m_config.gunCount),
                                       std::chrono::steady_clock::now() - std::chrono::seconds(3600));
    m_lastSetConfigPayloadByGun.assign(static_cast<size_t>(m_config.gunCount), std::string());
    m_lastChargeInfoReportByGun.assign(static_cast<size_t>(m_config.gunCount), std::chrono::steady_clock::now());
    m_runtimeChangedByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_lastTelesignalValuesByGun.assign(static_cast<size_t>(m_config.gunCount), std::array<uint8_t, 6>{{0}});
    m_lastTelesignalValidByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_lastFaultActiveByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_lastFaultCodeByGun.assign(static_cast<size_t>(m_config.gunCount), 0U);
    m_lastTelemetryReport = std::chrono::steady_clock::now();
    m_logSender.info("init_completed", std::string("gun_count=") + std::to_string(m_config.gunCount));
    return true;
}

void CommProcess::doRun()
{
    m_running = true;
    // BY ZF: 本地/daemon 喂狗频率统一控制为 5 秒一次。
    auto lastFeedTime = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    while (m_running.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastFeedTime >= std::chrono::seconds(5)) {
            feedWatchdog();
            feedDaemonWatchdog();
            lastFeedTime = now;
        }
        // BY ZF: 运行期间每30秒补发一次每枪 setConfig（retain），便于订阅端周期获取最新配置。
        if (now - m_lastPeriodicSetConfigPublish >= std::chrono::seconds(30)) {
            publishInitialSetConfig();
            m_lastPeriodicSetConfigPublish = now;
        }
        // BY ZF: 平台 TCP 链路维护与登录流程状态机驱动。
        maintainPlatformTcp();
        usleep(10000);
    }
}

void CommProcess::doCleanup()
{
    m_running = false;
    closePlatformTcp();
    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
}

bool CommProcess::loadConfig()
{
    ConfigManagerLite& cfg = getConfig();
    const std::string section = "Comm";

    m_config.gunCount = static_cast<uint8_t>(cfg.getInt(section, "gun_count", 2));
    m_config.mqttHost = cfg.getString(section, "mqtt_host", "127.0.0.1");
    m_config.mqttPort = cfg.getInt(section, "mqtt_port", 1883);
    m_config.mqttKeepalive = cfg.getInt(section, "mqtt_keepalive", 60);
    m_config.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_comm");
    m_config.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");
    m_config.biasNo = cfg.getInt(section, "bias_no", 0);
    m_config.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_config.mqttPassword = cfg.getString(section, "mqtt_password", "");
    m_config.masterHost = cfg.getString(section, "master_host", "127.0.0.1");
    m_config.masterPort = cfg.getInt(section, "master_port", 9000);
    m_config.cdzNo = cfg.getString(section, "cdz_no", "CDZ000001");
    const int chargerAddr = cfg.getInt(section, "charger_addr", 1);
    if (chargerAddr < 0 || chargerAddr > 0xFF) {
        m_logSender.warn("invalid_charger_addr", std::to_string(chargerAddr));
        m_config.chargerAddress = 0x01;
    } else {
        m_config.chargerAddress = static_cast<uint8_t>(chargerAddr);
    }
    m_config.loginId = cfg.getString(section, "login_id", "");
    m_config.assetCode = cfg.getString(section, "asset_code", "");
    if (m_config.assetCode.empty()) {
        m_config.assetCode = m_config.cdzNo; // BY ZF: NYC资产码未配置时回退到桩编号，保证01/02可组帧。
    }
    m_config.macAddr = cfg.getString(section, "mac_addr", "");
    m_config.factoryCreditCode = cfg.getString(section, "factory_credit_code", "");
    m_config.rsaPublicKey = cfg.getString(section, "rsa_public_key", "");
    m_config.sm2PublicKey = cfg.getString(section, "sm2_public_key", "");
    m_config.tcpReconnectSec = cfg.getInt(section, "tcp_reconnect_sec", 3);
    // BY ZF: NYC 协议要求集中器每20秒发送一次心跳测试。
    m_config.tcpHeartbeatSec = 20;
    m_config.loginRetrySec = cfg.getInt(section, "login_retry_sec", 10);
    m_config.offlineRunMode = (cfg.getInt(section, "offline_run_mode", 0) != 0);
    m_config.debugTcp = (cfg.getInt(section, "debug", 0) != 0);

    m_config.gunIdList.clear();
    m_config.gunTypeList.clear();
    m_config.gunQrCodeList.clear();
    m_gunRuntimeData.clear();
    m_feeModelByGun.clear();
    m_config.gunIdList.reserve(m_config.gunCount);
    m_config.gunTypeList.reserve(m_config.gunCount);
    m_config.gunQrCodeList.reserve(m_config.gunCount);
    m_gunRuntimeData.reserve(m_config.gunCount);
    m_feeModelByGun.reserve(m_config.gunCount);
    m_config.chargerType = kFixedChargerType;


    for (uint8_t i = 0; i < m_config.gunCount; ++i) {
        std::ostringstream idKey;
        idKey << "gun" << static_cast<int>(i + 1) << "_id";
        std::ostringstream qrKey;
        qrKey << "gun" << static_cast<int>(i + 1) << "_qrcode";

        const std::string idText = cfg.getString(section, idKey.str(), "0xFFFFFFFF");
        const uint32_t gunId = static_cast<uint32_t>(std::strtoul(idText.c_str(), nullptr, 0));
        m_config.gunIdList.push_back(gunId);
        m_config.gunTypeList.push_back(kFixedGunType);
        m_config.gunQrCodeList.push_back(cfg.getString(section, qrKey.str(), ""));
        m_gunRuntimeData.push_back(GunRuntimeData());
        m_feeModelByGun.push_back(FeeModel());
    }

    if (!m_config.macAddr.empty() && !isHexString(m_config.macAddr, 24)) {
        m_logSender.warn("invalid_mac_addr", m_config.macAddr);
    }
    if (!m_config.factoryCreditCode.empty() && m_config.factoryCreditCode.size() > 32) {
        m_logSender.warn("factory_credit_code_too_long", m_config.factoryCreditCode);
    }
    if (m_config.rsaPublicKey.empty()) {
        m_config.rsaPublicKey = m_config.sm2PublicKey; // BY ZF: 兼容旧配置键，NYC后续统一切到rsa_public_key。
    }
    if (m_config.rsaPublicKey.empty()) {
        m_logSender.warn("rsa_public_key_empty", "rsa_public_key not configured");
    }
    m_rsaPublicKeyActive = m_config.rsaPublicKey;
    m_sm2PublicKeyActive = m_config.sm2PublicKey;

    return true;
}

bool CommProcess::initMqtt()
{
    if (!m_mqtt.init(m_config.mqttClientId, true)) {
        std::cerr << "[Comm] mqtt init failed" << std::endl;
        return false;
    }

    if (!m_config.mqttUsername.empty()) {
        m_mqtt.setUsernamePassword(m_config.mqttUsername, m_config.mqttPassword);
    }

    m_mqtt.setConnectHandler([this](int rc) { onMqttConnected(rc); });
    m_mqtt.setMessageHandler([this](const std::string& topic, const std::string& payload) {
        onMqttMessage(topic, payload);
    });

    if (!m_mqtt.connect(m_config.mqttHost, m_config.mqttPort, m_config.mqttKeepalive)) {
        std::cerr << "[Comm] mqtt connect failed " << m_config.mqttHost << ":" << m_config.mqttPort << std::endl;
        return false;
    }
    if (!m_mqtt.loopStart()) {
        std::cerr << "[Comm] mqtt loop start failed" << std::endl;
        return false;
    }
    return true;
}

void CommProcess::onMqttConnected(int rc)
{
    if (rc != 0) {
        std::cerr << "[Comm] mqtt reconnect rc=" << rc << std::endl;
        return;
    }

    const std::string p = m_config.mqttTopicPrefix;
    m_mqtt.subscribe(p + "/logic/+/event", 1);
    m_mqtt.subscribe(p + "/logic/+/feeData", 1);
    m_mqtt.subscribe(p + "/pile/+/data", 0);
    m_mqtt.subscribe(p + "/pile/+/event", 1);
    m_mqtt.subscribe(p + "/meter/+/data", 0);
    // BY ZF: MQTT重连后主动发布一次每枪setConfig（retain）。
    publishInitialSetConfig();
    // BY ZF: 仅在已确认平台在线时补发在线事件，避免把“尚未完成登录”误判成平台离线。
    if (m_platformOnlineEventActive || m_config.offlineRunMode) {
        publishPlatformLinkEvent(true, m_config.offlineRunMode ? "offline_mode" : "login_ready");
    }
}

void CommProcess::onMqttMessage(const std::string& topic, const std::string& payload)
{
    std::string module;
    std::string leaf;
    uint8_t gun = 0;
    if (!parseTopic(topic, module, gun, leaf)) {
        return;
    }

    if (module == "logic" && leaf == "event") {
        handleLogicEventForPlatform(gun, payload);
        return;
    }
    if (module == "logic" && leaf == "feeData") {
        handleLogicFeeForPlatform(gun, payload);
        return;
    }
    if (module == "pile" && leaf == "data") {
        handlePileDataForPlatform(gun, payload);
        return;
    }
    if (module == "pile" && leaf == "event") {
        handlePileEventForPlatform(gun, payload);
        return;
    }
    if (module == "meter" && leaf == "data") {
        handleMeterDataForPlatform(gun, payload);
        return;
    }
}

bool CommProcess::parseTopic(const std::string& topic, std::string& module, uint8_t& gun, std::string& leaf) const
{
    const std::vector<std::string> seg = split(topic, '/');
    if (seg.size() != 4) {
        return false;
    }
    if (seg[0] != m_config.mqttTopicPrefix) {
        return false;
    }
    module = seg[1];
    const int externalGun = std::atoi(seg[2].c_str());
    const int localGun = externalGun - m_config.biasNo;
    if (localGun < 0 || localGun > 255) {
        return false;
    }
    gun = static_cast<uint8_t>(localGun);
    leaf = seg[3];
    return true;
}

bool CommProcess::publishPlatCmd(uint8_t gun, const std::string& payload)
{
    const std::string outTopic = buildTopic("plat", gun, "cmd");
    // BY ZF: NYC 平台命令 topic 统一按 QoS 2 发布，确保业务命令可靠投递。
    return m_mqtt.publish(outTopic, ensureGunField(payload, gun), 2, false);
}

bool CommProcess::handleLogicEventForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun < m_gunRuntimeData.size()) {
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root && cJSON_IsObject(root)) {
            cJSON* evt = cJSON_GetObjectItem(root, "event");
            cJSON* data = cJSON_GetObjectItem(root, "data");
            //更新充电状态
            if (cJSON_IsString(evt) && std::strcmp(evt->valuestring, "state_change") == 0 && cJSON_IsObject(data)) {
                cJSON* to = cJSON_GetObjectItem(data, "to");
                if (cJSON_IsString(to) && to->valuestring) {
                    uint8_t mappedStatus = 0x02;
                    // BY ZF: tcu_logic 状态 -> 平台枪状态
                    // BY ZF: IDLE=02, PREPARE=02, STARTING=03, CHARGING=03, STOPPING=03, ERROR=01, STOPPED=04
                    if (std::strcmp(to->valuestring, "IDLE") == 0) {
                        mappedStatus = 0x02;
                    } else if (std::strcmp(to->valuestring, "PREPARE") == 0) {
                        mappedStatus = 0x02;
                    } else if (std::strcmp(to->valuestring, "STARTING") == 0) {
                        mappedStatus = 0x03;
                    } else if (std::strcmp(to->valuestring, "CHARGING") == 0) {
                        mappedStatus = 0x03;
                    } else if (std::strcmp(to->valuestring, "STOPPING") == 0) {
                        mappedStatus = 0x03;
                    } else if (std::strcmp(to->valuestring, "ERROR") == 0) {
                        mappedStatus = 0x01;
                    } else if (std::strcmp(to->valuestring, "STOPPED") == 0) {
                        mappedStatus = 0x04;
                    }
                    printf("mappedStatus: %d\n", mappedStatus);
                    if (m_gunRuntimeData[gun].gunStatus != mappedStatus) {
                        m_gunRuntimeData[gun].gunStatus = mappedStatus;
                        if (gun < m_runtimeChangedByGun.size()) {
                            m_runtimeChangedByGun[gun] = 1;
                        }
                    }
                }
            }
            // BY ZF: 充电记录上报事件 -> 平台0x3D上传交易记录。
            if (cJSON_IsString(evt) &&
                std::strcmp(evt->valuestring, "update_record") == 0 &&
                cJSON_IsObject(data)) {
                std::vector<uint8_t> body;
                if (buildChargeRecordBodyFromUpdateRecord(gun, data, body) && !body.empty()) {
                    sendPlatformFrameEx(kCmdUploadTradeRecord, body, -1, 0x03,
                                        static_cast<int>(gun) + 1, 0x01);
                }
            }
            // BY ZF: 即插即充鉴权请求 -> 平台0x45 车辆即插即充认证。
            if (cJSON_IsString(evt) &&
                std::strcmp(evt->valuestring, "plug_and_charge_auth_request") == 0 &&
                cJSON_IsObject(data)) {
                std::vector<uint8_t> body = buildPlugChargeAuthBody(gun, data);
                if (!body.empty()) {
                    (void)sendPlatformFrameEx(kCmdPlugChargeAuth, body, -1, 0x03,
                                              static_cast<int>(gun) + 1, 0x01);
                } else {
                    m_logSender.warn("platform_plug_charge_auth", "build_body_fail");
                }
            }
            cJSON_Delete(root);
        } else if (root) {
            cJSON_Delete(root);
        }
    }
    // BY ZF: 平台上报发送链路后续在此扩展。
    return true;
}

bool CommProcess::handleLogicFeeForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun < m_gunRuntimeData.size()) {
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root && cJSON_IsObject(root)) {
            cJSON* data = cJSON_GetObjectItem(root, "data");
            if (cJSON_IsObject(data)) {
                cJSON* totalEnergy = cJSON_GetObjectItem(data, "totalEnergy");
                if (cJSON_IsNumber(totalEnergy)) {
                    m_gunRuntimeData[gun].totalEnergy = totalEnergy->valuedouble;
                }
                cJSON* totalAmount = cJSON_GetObjectItem(data, "totalAmount");
                if (cJSON_IsNumber(totalAmount)) {
                    m_gunRuntimeData[gun].totalAmount = totalAmount->valuedouble;
                }
                cJSON* electricAmount = cJSON_GetObjectItem(data, "electicAmount");
                if (!cJSON_IsNumber(electricAmount)) {
                    electricAmount = cJSON_GetObjectItem(data, "electricAmount");
                }
                if (cJSON_IsNumber(electricAmount)) {
                    m_gunRuntimeData[gun].electricAmount = electricAmount->valuedouble;
                }
                cJSON* serviceAmount = cJSON_GetObjectItem(data, "serviceAmount");
                if (cJSON_IsNumber(serviceAmount)) {
                    m_gunRuntimeData[gun].serviceAmount = serviceAmount->valuedouble;
                }
                cJSON* chargedTime = cJSON_GetObjectItem(data, "chargedTime");
                if (!cJSON_IsNumber(chargedTime)) {
                    chargedTime = cJSON_GetObjectItem(data, "chargeTime");
                }
                if (cJSON_IsNumber(chargedTime)) {
                    m_gunRuntimeData[gun].chargedTime = chargedTime->valuedouble;
                }

                cJSON* feeModelId = cJSON_GetObjectItem(data, "feeModelId");
                if (cJSON_IsString(feeModelId) && feeModelId->valuestring) {
                    m_gunRuntimeData[gun].feeModelId = feeModelId->valuestring;
                }

                cJSON* timeNum = cJSON_GetObjectItem(data, "timeNum");
                if (!cJSON_IsNumber(timeNum)) {
                    timeNum = cJSON_GetObjectItem(data, "sgemtentNum");
                }
                if (cJSON_IsNumber(timeNum)) {
                    m_gunRuntimeData[gun].feeTimeNum = timeNum->valueint;
                }

                cJSON* segments = cJSON_GetObjectItem(data, "segmentsAmount");
                if (cJSON_IsArray(segments)) {
                    m_gunRuntimeData[gun].feeSegments.clear();
                    const int cnt = cJSON_GetArraySize(segments);
                    if (cnt > 0) {
                        m_gunRuntimeData[gun].feeSegments.reserve(static_cast<size_t>(cnt));
                    }
                    for (int i = 0; i < cnt; ++i) {
                        cJSON* item = cJSON_GetArrayItem(segments, i);
                        if (!cJSON_IsObject(item)) {
                            continue;
                        }
                        FeeSegmentData seg;
                        cJSON* startTs = cJSON_GetObjectItem(item, "startTs");
                        if (cJSON_IsString(startTs) && startTs->valuestring) {
                            seg.startTs = startTs->valuestring;
                        }
                        cJSON* endTs = cJSON_GetObjectItem(item, "endTs");
                        if (cJSON_IsString(endTs) && endTs->valuestring) {
                            seg.endTs = endTs->valuestring;
                        }
                        cJSON* energyKwh = cJSON_GetObjectItem(item, "energyKwh");
                        if (cJSON_IsNumber(energyKwh)) {
                            seg.energyKwh = energyKwh->valuedouble;
                        }
                        cJSON* segElectric = cJSON_GetObjectItem(item, "electicAmount");
                        if (!cJSON_IsNumber(segElectric)) {
                            segElectric = cJSON_GetObjectItem(item, "electricAmount");
                        }
                        if (cJSON_IsNumber(segElectric)) {
                            seg.electricAmount = segElectric->valuedouble;
                        }
                        cJSON* segService = cJSON_GetObjectItem(item, "serviceAmount");
                        if (cJSON_IsNumber(segService)) {
                            seg.serviceAmount = segService->valuedouble;
                        }
                        m_gunRuntimeData[gun].feeSegments.push_back(seg);
                    }
                }
            }
            cJSON_Delete(root);
        } else if (root) {
            cJSON_Delete(root);
        }
    }
    // BY ZF: 预留平台上报发送链路。
    return true;
}

bool CommProcess::handlePileDataForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun < m_gunRuntimeData.size()) {
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root && cJSON_IsObject(root)) {
            cJSON* type = cJSON_GetObjectItem(root, "type");
            cJSON* data = cJSON_GetObjectItem(root, "data");
            if (cJSON_IsString(type) && std::strcmp(type->valuestring, "yx") == 0 && cJSON_IsObject(data)) {
                GunRuntimeData& rd = m_gunRuntimeData[gun];
                bool changed = false;

                // BY ZF: 遥信字段更新辅助：仅当新值与缓存值不同才写入，并标记changed=true用于触发“变位即送”。
                auto updateU8 = [data, &changed](const char* key, uint8_t& dst) {
                    cJSON* v = cJSON_GetObjectItem(data, key);
                    if (v && cJSON_IsNumber(v)) {
                        int n = v->valueint;
                        if (n < 0) n = 0;
                        if (n > 255) n = 255;
                        const uint8_t nv = static_cast<uint8_t>(n);
                        if (dst != nv) {
                            dst = nv;
                            changed = true;
                        }
                    }
                };
                auto updateU16 = [data, &changed](const char* key, uint16_t& dst) {
                    cJSON* v = cJSON_GetObjectItem(data, key);
                    if (v && cJSON_IsNumber(v)) {
                        int n = v->valueint;
                        if (n < 0) n = 0;
                        if (n > 0xFFFF) n = 0xFFFF;
                        const uint16_t nv = static_cast<uint16_t>(n);
                        if (dst != nv) {
                            dst = nv;
                            changed = true;
                        }
                    }
                };

                
                updateU8("workStatus", rd.yxWorkStatus);
                updateU8("totalErr", rd.yxTotalFault);
                updateU8("totalFault", rd.yxTotalFault);
                updateU8("totalAlarm", rd.yxTotalAlarm);
                updateU8("emergencyStopFault", rd.yxEmergencyStopFault);
                updateU8("vehicleConnectStatus", rd.yxVehicleConnectStatus);
                updateU8("vinReq", rd.yxVinReq);
                updateU8("gunSeatStatus", rd.yxGunSeatStatus);
                updateU8("electronicLockStatus", rd.yxElectronicLockStatus);
                updateU8("dcContactorStatus", rd.yxDcContactorStatus);
                updateU16("otherFault", rd.yxOtherFault);
                if (changed && gun < m_runtimeChangedByGun.size()) {
                    m_runtimeChangedByGun[gun] = 1;
                }
            }
            // BY ZF: 缓存遥测电压/电流（来自 yc）。
            if (cJSON_IsString(type) && std::strcmp(type->valuestring, "yc") == 0 && cJSON_IsObject(data)) {
                GunRuntimeData& rd = m_gunRuntimeData[gun];
                auto updateNum = [data](const char* key, double& dst) {
                    cJSON* v = cJSON_GetObjectItem(data, key);
                    if (v && cJSON_IsNumber(v)) {
                        dst = v->valuedouble;
                    }
                };
                auto updateInt = [data](const char* key, int& dst) {
                    cJSON* v = cJSON_GetObjectItem(data, key);
                    if (v && cJSON_IsNumber(v)) {
                        dst = v->valueint;
                    }
                };

                updateNum("outputVoltage", rd.voltage);
                updateNum("outputCurrent", rd.current);
                updateNum("soc", rd.soc);
                updateNum("batteryMinTemp", rd.batteryMinTemp);
                updateNum("batteryMaxTemp", rd.batteryMaxTemp);
                updateNum("cellMaxVoltage", rd.cellMaxVoltage);
                updateNum("cellMinVoltage", rd.cellMinVoltage);
                updateNum("pileEnvTemp", rd.pileEnvTemp);
                updateNum("guideVoltage", rd.guideVoltage);
                updateNum("bmsReqVoltage", rd.bmsReqVoltage);
                updateNum("bmsReqCurrent", rd.bmsReqCurrent);
                updateInt("chargeMode", rd.ycChargeMode);
                updateNum("bmsMeasuredVoltage", rd.bmsMeasuredVoltage);
                updateNum("bmsMeasuredCurrent", rd.bmsMeasuredCurrent);
                updateNum("estimatedRemainTime", rd.estimatedRemainTime);
                updateNum("interfaceTemp1", rd.interfaceTemp1);
                updateNum("interfaceTemp2", rd.interfaceTemp2);
                updateNum("interfaceTemp3", rd.interfaceTemp3);
                updateNum("interfaceTemp4", rd.interfaceTemp4);
                updateInt("maxVoltageCellNo", rd.maxVoltageCellNo);
                updateInt("maxTempPointNo", rd.maxTempPointNo);
                updateInt("minTempPointNo", rd.minTempPointNo);
                updateNum("inletTemp", rd.inletTemp);
                updateNum("outletTemp", rd.outletTemp);
                updateNum("envHumidity", rd.envHumidity);
            }
            cJSON_Delete(root);
        } else if (root) {
            cJSON_Delete(root);
        }
    }
    // BY ZF: 平台上报发送链路后续在此扩展。
    return true;
}

bool CommProcess::handleMeterDataForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun >= m_gunRuntimeData.size()) {
        return false;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        return false;
    }

    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        cJSON* n = cJSON_GetObjectItem(data, "totalEnergy");
        if (n && cJSON_IsNumber(n)) {
            rd.meterEnergy = n->valuedouble;
        }
        n = cJSON_GetObjectItem(data, "voltage");
        if (n && cJSON_IsNumber(n)) {
            rd.meterVoltage = n->valuedouble;
        }
        n = cJSON_GetObjectItem(data, "current");
        if (n && cJSON_IsNumber(n)) {
            rd.meterCurrent = n->valuedouble;
        }
    }

    cJSON_Delete(root);
    return true;
}

bool CommProcess::handlePileEventForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun >= m_gunRuntimeData.size()) {
        return false;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        return false;
    }

    cJSON* type = cJSON_GetObjectItem(root, "type");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsString(type) && type->valuestring &&
        std::strcmp(type->valuestring, "start_complete") == 0 &&
        cJSON_IsObject(data)) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        auto getU8 = [data](const char* key, uint8_t defVal) -> uint8_t {
            cJSON* n = cJSON_GetObjectItem(data, key);
            if (!n || !cJSON_IsNumber(n)) {
                return defVal;
            }
            int v = n->valueint;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            return static_cast<uint8_t>(v);
        };
        auto getU16 = [data](const char* key, uint16_t defVal) -> uint16_t {
            cJSON* n = cJSON_GetObjectItem(data, key);
            if (!n || !cJSON_IsNumber(n)) {
                return defVal;
            }
            int v = n->valueint;
            if (v < 0) v = 0;
            if (v > 0xFFFF) v = 0xFFFF;
            return static_cast<uint16_t>(v);
        };
        auto getAscii = [data](const char* key) -> std::string {
            cJSON* s = cJSON_GetObjectItem(data, key);
            if (s && cJSON_IsString(s) && s->valuestring) {
                return std::string(s->valuestring);
            }
            return std::string();
        };
        auto getBytes = [data](const char* key, uint8_t* outBytes, size_t len) {
            std::memset(outBytes, 0, len);
            cJSON* arr = cJSON_GetObjectItem(data, key);
            if (!arr || !cJSON_IsArray(arr)) {
                return;
            }
            const int n = cJSON_GetArraySize(arr);
            for (size_t i = 0; i < len && static_cast<int>(i) < n; ++i) {
                cJSON* it = cJSON_GetArrayItem(arr, static_cast<int>(i));
                if (!it || !cJSON_IsNumber(it)) {
                    continue;
                }
                int v = it->valueint;
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                outBytes[i] = static_cast<uint8_t>(v);
            }
        };

        rd.startCompleteData.successFlag = getU8("successFlag", 1);
        rd.startCompleteData.failReason = getU8("chargeFailReason", 0);
        getBytes("pileBmsVersion", rd.startCompleteData.pileBmsVersion.data(), rd.startCompleteData.pileBmsVersion.size());
        rd.startCompleteData.batteryType = getU8("batteryType", 0);
        rd.startCompleteData.ratedCapacity = getU16("ratedCapacity", 0);
        rd.startCompleteData.ratedTotalVoltage = getU16("ratedTotalVoltage", 0);
        rd.startCompleteData.cellMaxChargeVoltage = getU16("cellMaxChargeVoltage", 0);
        rd.startCompleteData.bmsMaxChargeVoltage = getU16("bmsMaxChargeVoltage", 0);
        rd.startCompleteData.maxAllowChargeCurrent = getU16("maxAllowChargeCurrent", 0);
        rd.startCompleteData.currentTotalVoltage = getU16("currentTotalVoltage", 0);
        rd.startCompleteData.maxAllowTemp = getU8("maxAllowTemp", 0);
        rd.startCompleteData.pileMaxOutputVoltage = getU16("pileMaxOutputVoltage", 0);
        rd.startCompleteData.pileMinOutputVoltage = getU16("pileMinOutputVoltage", 0);
        rd.startCompleteData.pileMaxOutputCurrent = getU16("pileMaxOutputCurrent", 0);
        rd.startCompleteData.pileMinOutputCurrent = getU16("pileMinOutputCurrent", 0);
        rd.startCompleteData.batteryManufacturer = getAscii("batteryManufacturer");
        getBytes("batterySerial", rd.startCompleteData.batterySerial.data(), rd.startCompleteData.batterySerial.size());
        rd.startCompleteData.batteryPropertyFlag = getU8("batteryPropertyFlag", 0);
        rd.startCompleteData.batteryProdYear = getU8("batteryProdYear", 0);
        rd.startCompleteData.batteryProdMonth = getU8("batteryProdMonth", 0);
        rd.startCompleteData.batteryProdDay = getU8("batteryProdDay", 0);
        getBytes("batteryChargeCount", rd.startCompleteData.batteryChargeCount.data(), rd.startCompleteData.batteryChargeCount.size());
        rd.startCompleteData.nominalEnergy = getU8("nominalEnergy", 0);
        rd.startCompleteData.soc = getU8("soc", 0);
        rd.startCompleteData.vin = getAscii("vin");
        getBytes("bmsSoftwareVersion", rd.startCompleteData.bmsSoftwareVersion.data(), rd.startCompleteData.bmsSoftwareVersion.size());
        rd.startCompleteData.insulationFault = getU8("insulationMonitorFault", 0);

        const std::vector<uint8_t> body = buildStartChargeResultBody(gun);
        if (!body.empty()) {
            (void)sendPlatformFrame(kCmdChargeStageEvent, body);
        }
        // BY ZF: 启动完成事件到达后，任何结果都上送 0x15 BRM（字段来自运行态缓存）。
        const std::vector<uint8_t> brmBody = buildBrmBody(gun);
        if (!brmBody.empty()) {
            (void)sendPlatformFrame(kCmdBmsInfo, brmBody);
        }
        // BY ZF: 与0x15同位置，上送0x17 BCP参数配置报文。
        const std::vector<uint8_t> bcpBody = buildBcpBody(gun);
        if (!bcpBody.empty()) {
            (void)sendPlatformFrame(kCmdChangeTelemetry, bcpBody);
        }
    } else if (cJSON_IsString(type) && type->valuestring &&
               std::strcmp(type->valuestring, "stop_complete") == 0 &&
               cJSON_IsObject(data)) {
        // BY ZF: 停止完成后补送0x19结束阶段报文（BSD/CSD汇总）。
        const std::vector<uint8_t> endStageBody = buildChargeEndStageBody(gun, data);
        if (!endStageBody.empty()) {
            (void)sendPlatformFrame(kCmdChargeStageEvent, endStageBody);
        }
        // BY ZF: 停止完成后上送0x1D BST报文。
        const std::vector<uint8_t> bstBody = buildBstBody(gun, data);
        if (!bstBody.empty()) {
            (void)sendPlatformFrame(kCmdChargeStageEvent, bstBody);
        }
        // BY ZF: 停止完成后上送0x21 CST报文（当前中止原因字段固定0x00）。
        const std::vector<uint8_t> cstBody = buildCstBody(gun);
        if (!cstBody.empty()) {
            (void)sendPlatformFrame(kCmdBmsInfo, cstBody);
        }
    }

    cJSON_Delete(root);
    return true;
}

bool CommProcess::buildChargeRecordBodyFromUpdateRecord(uint8_t gun, cJSON* data, std::vector<uint8_t>& body)
{
    if (!data || gun >= m_gunRuntimeData.size()) {
        return false;
    }

    const std::string tradeNo = jsonGetString(data, "tradeNo");
    const std::string vinCode = jsonGetString(data, "vinCode");
    std::string cardNumber = jsonGetString(data, "cardNumber");
    if (cardNumber.empty()) {
        cardNumber = m_gunRuntimeData[gun].chargeUserNo;
    }
    int startType = 1;
    (void)jsonGetInt(data, "startType", startType);
    const double totalElect = [&](){ double v = 0.0; jsonGetNumber(data, "totalElect", v); return v; }();
    const double totalCost = [&](){ double v = 0.0; jsonGetNumber(data, "totalCost", v); return v; }();
    const double sumStart = [&](){ double v = 0.0; jsonGetNumber(data, "sumStart", v); return v; }();
    const double sumEnd = [&](){ double v = 0.0; jsonGetNumber(data, "sumEnd", v); return v; }();
    const int reason = [&](){ int v = 0; jsonGetInt(data, "reason", v); return v; }();
    const uint64_t startTime = [&](){ cJSON* n = cJSON_GetObjectItem(data, "chargeStartTime"); return (n && cJSON_IsNumber(n)) ? static_cast<uint64_t>(n->valuedouble) : 0ULL; }();
    const uint64_t endTime = [&](){ cJSON* n = cJSON_GetObjectItem(data, "chargeEndTime"); return (n && cJSON_IsNumber(n)) ? static_cast<uint64_t>(n->valuedouble) : 0ULL; }();
    int startSoc = 0;
    int endSoc = 0;
    (void)jsonGetInt(data, "startSoc", startSoc);
    (void)jsonGetInt(data, "endSoc", endSoc);
    const uint8_t platformStopReason = static_cast<uint8_t>(reason & 0xFF);
    const std::string stopReasonText = jsonGetString(data, "stopReasonText");

    auto appendYmdHms6 = [&body](uint64_t ymdhms) {
        char buf[15] = {0};
        std::snprintf(buf, sizeof(buf), "%014llu", static_cast<unsigned long long>(ymdhms));
        auto d2 = [](char a, char b) -> int {
            if (a < '0' || a > '9' || b < '0' || b > '9') {
                return 0;
            }
            return (a - '0') * 10 + (b - '0');
        };
        const int year = d2(buf[2], buf[3]);
        const int month = d2(buf[4], buf[5]);
        const int day = d2(buf[6], buf[7]);
        const int hour = d2(buf[8], buf[9]);
        const int minute = d2(buf[10], buf[11]);
        const int second = d2(buf[12], buf[13]);
        body.push_back(static_cast<uint8_t>(second & 0xFF));
        body.push_back(static_cast<uint8_t>(minute & 0xFF));
        body.push_back(static_cast<uint8_t>(hour & 0xFF));
        body.push_back(static_cast<uint8_t>(day & 0xFF));
        body.push_back(static_cast<uint8_t>(month & 0xFF));
        body.push_back(static_cast<uint8_t>(year & 0xFF));
    };
    auto appendString8 = [&body](const std::string& text) {
        const size_t textLen = std::min<size_t>(text.size(), 255U);
        body.push_back(static_cast<uint8_t>(textLen));
        body.insert(body.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(textLen));
    };
    auto appendDateTime6ByHhmm = [&body](uint64_t baseYmdHms, const std::string& hhmm) {
        char buf[15] = {0};
        std::snprintf(buf, sizeof(buf), "%014llu", static_cast<unsigned long long>(baseYmdHms));
        auto d2 = [](char a, char b) -> int {
            if (a < '0' || a > '9' || b < '0' || b > '9') {
                return 0;
            }
            return (a - '0') * 10 + (b - '0');
        };
        const int year = d2(buf[2], buf[3]);
        const int month = d2(buf[4], buf[5]);
        const int day = d2(buf[6], buf[7]);
        int hour = 0;
        int minute = 0;
        if (hhmm.size() >= 4U) {
            hour = d2(hhmm[0], hhmm[1]);
            minute = d2(hhmm[2], hhmm[3]);
        }
        body.push_back(0x00);
        body.push_back(static_cast<uint8_t>(minute & 0xFF));
        body.push_back(static_cast<uint8_t>(hour & 0xFF));
        body.push_back(static_cast<uint8_t>(day & 0xFF));
        body.push_back(static_cast<uint8_t>(month & 0xFF));
        body.push_back(static_cast<uint8_t>(year & 0xFF));
    };
    auto mapStartReason = [this, gun, startType]() -> uint8_t {
        const uint8_t remoteStartReason = m_gunRuntimeData[gun].remoteStartReason;
        if (remoteStartReason != 0U) {
            return remoteStartReason;
        }
        if (startType == 4) {
            return 0x00;
        }
        if (startType == 2) {
            return 0x10;
        }
        return 0x01;
    };

    // BY ZF: NYC 0x42 充电记录简化组帧。
    appendYmdHms6(startTime);
    appendYmdHms6(endTime);
    appendU32LE(body, scaleToU32(totalElect, 1000.0));
    appendU32LE(body, scaleToU32(totalCost, 10000.0));
    appendU32LE(body, scaleToU32(sumStart, 1000.0));
    appendU32LE(body, scaleToU32(sumEnd, 1000.0));
    body.push_back(static_cast<uint8_t>(std::max(0, std::min(100, startSoc))));
    body.push_back(static_cast<uint8_t>(std::max(0, std::min(100, endSoc))));
    appendU32LE(body, 0U);
    appendU32LE(body, 0U);

    cJSON* partElectArr = cJSON_GetObjectItem(data, "partElect");
    cJSON* chargeFeeArr = cJSON_GetObjectItem(data, "chargeFee");
    cJSON* serviceFeeArr = cJSON_GetObjectItem(data, "serviceFee");
    const std::string feeModelIdFromRecord = jsonGetString(data, "feeModelId");

    int timeNum = 0;
    jsonGetInt(data, "timeNum", timeNum);
    int periodCount = std::max(0, timeNum);
    if (cJSON_IsArray(partElectArr)) {
        periodCount = std::min(periodCount, cJSON_GetArraySize(partElectArr));
    }
    if (cJSON_IsArray(chargeFeeArr)) {
        periodCount = std::min(periodCount, cJSON_GetArraySize(chargeFeeArr));
    }
    if (cJSON_IsArray(serviceFeeArr)) {
        periodCount = std::min(periodCount, cJSON_GetArraySize(serviceFeeArr));
    }
    if (periodCount < 0) periodCount = 0;
    if (periodCount > 48) periodCount = 48;

    const FeeModel* matchedFeeModel = nullptr;
    if (gun < m_feeModelByGun.size()) {
        const FeeModel& localFeeModel = m_feeModelByGun[gun];
        if (!feeModelIdFromRecord.empty() &&
            !localFeeModel.feeModelId.empty() &&
            feeModelIdFromRecord == localFeeModel.feeModelId &&
            static_cast<int>(localFeeModel.timeNum) == periodCount &&
            localFeeModel.timeSeg.size() >= static_cast<size_t>(periodCount) &&
            localFeeModel.chargeFee.size() >= static_cast<size_t>(periodCount) &&
            localFeeModel.serviceFee.size() >= static_cast<size_t>(periodCount)) {
            matchedFeeModel = &localFeeModel;
        }
    }
    auto getSegmentNumber = [](cJSON* arr, int index) -> double {
        if (!cJSON_IsArray(arr)) {
            return 0.0;
        }
        cJSON* item = cJSON_GetArrayItem(arr, index);
        return (item && cJSON_IsNumber(item)) ? item->valuedouble : 0.0;
    };

    uint32_t energyBySeg[5] = {0U};
    uint32_t chargeFeeBySeg[5] = {0U};
    uint32_t serviceFeeBySeg[5] = {0U};
    if (matchedFeeModel && matchedFeeModel->segFlag.size() >= static_cast<size_t>(periodCount)) {
        for (int i = 0; i < periodCount; ++i) {
            const unsigned int segFlag = matchedFeeModel->segFlag[static_cast<size_t>(i)];
            if (segFlag < 1U || segFlag > 4U) {
                continue;
            }
            energyBySeg[segFlag] += scaleToU32(getSegmentNumber(partElectArr, i), 1000.0);
            chargeFeeBySeg[segFlag] += scaleToU32(getSegmentNumber(chargeFeeArr, i), 10000.0);
            serviceFeeBySeg[segFlag] += scaleToU32(getSegmentNumber(serviceFeeArr, i), 10000.0);
        }
    }

    appendU32LE(body, energyBySeg[1]);
    appendU32LE(body, energyBySeg[2]);
    appendU32LE(body, energyBySeg[3]);
    appendU32LE(body, energyBySeg[4]);
    appendU32LE(body, chargeFeeBySeg[1]);
    appendU32LE(body, chargeFeeBySeg[2]);
    appendU32LE(body, chargeFeeBySeg[3]);
    appendU32LE(body, chargeFeeBySeg[4]);
    appendU32LE(body, serviceFeeBySeg[1]);
    appendU32LE(body, serviceFeeBySeg[2]);
    appendU32LE(body, serviceFeeBySeg[3]);
    appendU32LE(body, serviceFeeBySeg[4]);

    body.push_back(mapStartReason());
    body.push_back(platformStopReason);
    appendString8(cardNumber);
    appendString8(stopReasonText);
    body.push_back(0x01);
    appendString8(vinCode);

    uint8_t orderPeriodCount = 0U;
    if (matchedFeeModel &&
        matchedFeeModel->timeSeg.size() >= static_cast<size_t>(periodCount) &&
        matchedFeeModel->segFlag.size() >= static_cast<size_t>(periodCount)) {
        orderPeriodCount = static_cast<uint8_t>(std::min(periodCount, 255));
    }
    body.push_back(orderPeriodCount);
    for (uint8_t i = 0; i < orderPeriodCount; ++i) {
        appendDateTime6ByHhmm(startTime, matchedFeeModel->timeSeg[static_cast<size_t>(i)]);
        body.push_back(static_cast<uint8_t>(matchedFeeModel->segFlag[static_cast<size_t>(i)] & 0xFFU));
        appendU32LE(body, scaleToU32(getSegmentNumber(partElectArr, i), 1000.0));
        appendU32LE(body, scaleToU32(getSegmentNumber(chargeFeeArr, i), 10000.0));
        appendU32LE(body, scaleToU32(getSegmentNumber(serviceFeeArr, i), 10000.0));
    }

    appendString8(tradeNo);

    m_gunRuntimeData[gun].pendingRecordTradeNo = tradeNo;
    return true;
}

uint16_t CommProcess::mapTradeStopReasonToPlatform(int mqttReason) const
{
    // BY ZF: 这里预留“MQTT reason -> 平台停机原因点表”映射入口。
    // BY ZF: 后续和平台点表对齐后，在这里补充 switch/case 或查表逻辑即可。
    // BY ZF: 当前默认保持原值透传，仅做协议范围兜底。
    if (mqttReason < 0) {
        return 0U;
    }

    // BY ZF: 统一维护“MQTT reason -> 平台停机原因”映射。
    switch (mqttReason) {
    // BY ZF: 启动阶段映射，同步自 doc/faulttemp。
    case 0x1000F: return 0x0102U;
    case 0x10008: return 0x0103U;
    case 0x10009: return 0x0103U;
    case 0x1000A: return 0x0103U;
    case 0x1000B: return 0x0105U;
    case 0x1000C: return 0x0105U;
    case 0x10018: return 0x0105U;
    case 0x10019: return 0x0105U;
    case 0x1003A: return 0x0105U;
    case 0x1001A: return 0x0106U;
    case 0x10003: return 0x011DU;
    case 0x10001: return 0x011EU;
    case 0x10004: return 0x0123U;
    case 0x1002B: return 0x0123U;
    case 0x1002C: return 0x0126U;
    case 0x1002D: return 0x0126U;
    case 0x10006: return 0x0127U;
    case 0x1002A: return 0x2007U;
    case 0x10022: return 0x0202U;
    case 0x10023: return 0x0202U;
    case 0x10024: return 0x0203U;
    case 0x10025: return 0x0203U;
    case 0x10026: return 0x0204U;
    case 0x10027: return 0x0205U;
    case 0x10029: return 0x0206U;
    case 0x1001C: return 0x0315U;
    case 0x1001D: return 0x0316U;
    case 0x1001E: return 0x0317U;
    case 0x1001F: return 0x0318U;
    case 0x10020: return 0x0319U;
    case 0x10021: return 0x031AU;
    case 0x10032: return 0x0325U;
    case 0x10033: return 0x0326U;
    case 0x10034: return 0x0326U;
    case 0x10035: return 0x0326U;
    case 0x10036: return 0x0326U;
    case 0x1003E: return 0x0328U;
    case 0x1003F: return 0x0328U;
    case 0x10042: return 0x0328U;
    case 0x10043: return 0x0328U;
    case 0x10031: return 0x03F1U;
    case 0x1003D: return 0x0401U;
    case 0x10017: return 0x0402U;
    case 0x1001B: return 0x0402U;
    case 0x1003C: return 0x0402U;
    case 0x1000E: return 0x0405U;
    case 0x10002: return 0x0406U;
    case 0x10010: return 0x0406U;
    case 0x10011: return 0x0406U;
    case 0x10012: return 0x0406U;
    case 0x10013: return 0x0406U;
    case 0x10014: return 0x0406U;
    case 0x10015: return 0x0406U;
    case 0x10041: return 0x0406U;
    case 0x10102: return 0x0406U;
    case 0x10104: return 0x0406U;
    case 0x10037: return 0x0407U;
    case 0x10038: return 0x0407U;
    case 0x10039: return 0x0407U;
    case 0x10101: return 0x0408U;
    case 0x10005: return 0x0409U;
    case 0x1000D: return 0x040AU;
    case 0x10007: return 0x040BU;
    case 0x1002E: return 0x0420U;
    case 0x1002F: return 0x0420U;
    case 0x10030: return 0x0420U;
    case 0x10040: return 0x04F0U;
    case 0x1F001: return 0x04FFU;
    case 0x1003B: return 0xFF3BU;
    case 0x10028: return 0xFFFFU;
    case 0x100FF: return 0xFFFFU;

    // BY ZF: 充电中阶段映射，沿用当前已整理内容。
    case 0x2000C:
    case 0x2000D:
    case 0x2000E:
    case 0x2000F:
        return 0x0103U;
    case 0x20013:
    case 0x20014:
    case 0x20015:
    case 0x20016:
    case 0x20017:
    case 0x20018:
    case 0x2001E:
    case 0x2002C:
        return 0x0104U;
    case 0x20010:
    case 0x20011:
    case 0x2001B:
    case 0x2001C:
    case 0x20034:
        return 0x0105U;
    case 0x2001D:
    case 0x20031:
        return 0x0106U;
    case 0x20002:
        return 0x010BU;
    case 0x20036:
        return 0x0112U;
    case 0x20037:
        return 0x0113U;
    case 0x20038:
        return 0x0114U;
    case 0x2001A:
        return 0x0119U;
    case 0x20008:
        return 0x011DU;
    case 0x20005:
    case 0x20019:
    case 0x2002E:
    case 0x2002F:
    case 0x20030:
        return 0x011EU;
    case 0x2000A:
        return 0x0120U;
    case 0x20012:
        return 0x0122U;
    case 0x20009:
        return 0x0123U;
    case 0x2000B:
        return 0x0127U;
    case 0x2001F:
        return 0x0206U;
    case 0x20020:
        return 0x0207U;
    case 0x20021:
        return 0x0208U;
    case 0x20022:
        return 0x0301U;
    case 0x20023:
        return 0x0302U;
    case 0x20024:
        return 0x0303U;
    case 0x20025:
        return 0x0304U;
    case 0x20026:
        return 0x0305U;
    case 0x20027:
        return 0x0306U;
    case 0x20028:
        return 0x0307U;
    case 0x20029:
        return 0x0308U;
    case 0x2002B:
        return 0x0313U;
    case 0x20035:
        return 0x032CU;
    case 0x20004:
        return 0x04FFU;
    case 0x2003D:
        return 0x0501U;
    case 0x20039:
        return 0x0510U;
    case 0x2003A:
        return 0x0511U;
    case 0x2003B:
        return 0x0512U;
    case 0x2003C:
        return 0x0513U;
    case 0x20102:
        return 0x0514U;
    case 0x20101:
        return 0x0515U;
    case 0x20003:
    case 0x20006:
    case 0x20104:
        return 0x0516U;
    case 0x20000:
        return 0x0701U;
    // BY ZF: 原始整理内容里出现了重复 0x20023 -> 0x0707，当前先保留 0x0302 映射，待你确认后再修正。
    case 0x20007:
        return 0x07FFU;
    case 0x20032:
        return 0xFF32U;
    case 0x20033:
        return 0xFF33U;
    case 0x2002D:
    case 0x200FF:
        return 0xFFFFU;
    default:
        break;
    }

    if (mqttReason > 0xFFFF) {
        return 0xFFFFU;
    }
    return static_cast<uint16_t>(mqttReason);
}

std::string CommProcess::ensureGunField(const std::string& payload, uint8_t gun) const
{
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        return payload;
    }

    cJSON* gunItem = cJSON_GetObjectItem(root, "gun");
    if (!gunItem || !cJSON_IsNumber(gunItem)) {
        cJSON_AddNumberToObject(root, "gun", gun);
    }

    char* out = cJSON_PrintUnformatted(root);
    std::string text = payload;
    if (out) {
        text = out;
        free(out);
    }
    cJSON_Delete(root);
    return text;
}

std::string CommProcess::buildTopic(const char* module, uint8_t gun, const char* leaf) const
{
    std::ostringstream oss;
    oss << m_config.mqttTopicPrefix << "/" << module << "/"
        << (static_cast<int>(gun) + m_config.biasNo) << "/" << leaf;
    return oss.str();
}

bool CommProcess::connectPlatformTcp()
{
    closePlatformTcp();

    m_tcpFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_tcpFd < 0) {
        return false;
    }

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(m_tcpFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(m_tcpFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    bool connected = false;

    // BY ZF: 优先按 IPv4 字面量处理，兼容现有配置。
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(m_config.masterPort));
    if (::inet_pton(AF_INET, m_config.masterHost.c_str(), &addr.sin_addr) == 1) {
        connected = (::connect(m_tcpFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    } else {
        // BY ZF: master_host 支持域名，解析成功后按 IPv4 地址逐个尝试连接。
        struct addrinfo hints;
        struct addrinfo* result = NULL;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        const int gaiRet = ::getaddrinfo(m_config.masterHost.c_str(), NULL, &hints, &result);
        if (gaiRet == 0 && result) {
            for (struct addrinfo* rp = result; rp != NULL; rp = rp->ai_next) {
                if (!rp->ai_addr || rp->ai_addrlen < static_cast<socklen_t>(sizeof(struct sockaddr_in))) {
                    continue;
                }
                struct sockaddr_in resolvedAddr;
                std::memcpy(&resolvedAddr, rp->ai_addr, sizeof(resolvedAddr));
                resolvedAddr.sin_port = htons(static_cast<uint16_t>(m_config.masterPort));
                if (::connect(m_tcpFd, reinterpret_cast<struct sockaddr*>(&resolvedAddr), sizeof(resolvedAddr)) == 0) {
                    connected = true;
                    break;
                }
            }
        }
        if (result) {
            ::freeaddrinfo(result);
        }
    }

    if (!connected) {
        closePlatformTcp();
        return false;
    }

    m_platformConnected = true;
    m_loginState = LOGIN_IDLE;
    m_lastLoginAction = std::chrono::steady_clock::now();
    m_lastHeartbeat = std::chrono::steady_clock::now();
    m_lastHeartbeatRecv = std::chrono::steady_clock::now();
    m_lastChargeInfoReport = std::chrono::steady_clock::now();
    m_lastTelemetryReport = std::chrono::steady_clock::now();
    m_lastChargeInfoReportByGun.assign(static_cast<size_t>(m_config.gunCount), m_lastChargeInfoReport);
    m_runtimeChangedByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_lastTelesignalValuesByGun.assign(static_cast<size_t>(m_config.gunCount), std::array<uint8_t, 6>{{0}});
    m_lastTelesignalValidByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_lastFaultActiveByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_lastFaultCodeByGun.assign(static_cast<size_t>(m_config.gunCount), 0U);
    m_tcpRxCache.clear();
    resetCryptoSession();
    m_logSender.info("platform_tcp_connected", m_config.masterHost + ":" + std::to_string(m_config.masterPort));
    return true;
}

void CommProcess::closePlatformTcp()
{
    if (m_tcpFd >= 0) {
        // BY ZF: 主动双向关闭平台 TCP，避免异常恢复时旧连接残留在半开状态影响重连。
        ::shutdown(m_tcpFd, SHUT_RDWR);
        ::close(m_tcpFd);
        m_tcpFd = -1;
    }
    m_platformConnected = false;
    m_loginState = LOGIN_IDLE;
    m_tcpRxCache.clear();
    // BY ZF: 返回登录流程时增加 10 秒限流，避免链路异常时连续刷 0x01 登录请求。
    m_nextLoginAllowedTime = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    // BY ZF: TCP断开不直接判平台通信故障，统一由心跳接收超时触发 offline 事件。
    resetCryptoSession();
}

bool CommProcess::sendPlatformText(const std::string& text)
{
    if (m_tcpFd < 0) {
        return false;
    }
    ssize_t n = ::send(m_tcpFd, text.data(), text.size(), 0);
    if (n < 0) {
        return false;
    }
    return true;
}

uint16_t CommProcess::calcCrc16Modbus(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint8_t CommProcess::bcdByte(int value)
{
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

std::array<uint8_t, 6> CommProcess::buildTelesignalValues(const GunRuntimeData& rd)
{
    std::array<uint8_t, 6> values = {{0, 0, 0, 0, 0, 0}};
    values[0] = (rd.yxWorkStatus != 0U) ? 0x01U : 0x00U; // BY ZF: 充电状态
    values[1] = (rd.yxTotalFault != 0U || rd.gunStatus == 0x01) ? 0x01U : 0x00U; // BY ZF: 故障状态
    values[2] = 0x00U; // BY ZF: 检修状态当前无对应输入。
    values[3] = (rd.yxVehicleConnectStatus != 0U) ? 0x01U : 0x00U; // BY ZF: 充电插头连接状态
    values[4] = 0x01U; // BY ZF: 桩直连主站状态始终为1。
    values[5] = (rd.yxEmergencyStopFault != 0U) ? 0x01U : 0x00U; // BY ZF: 急停按钮状态
    return values;
}

void CommProcess::appendU16BE(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void CommProcess::appendU32BE(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void CommProcess::appendU64BE(std::vector<uint8_t>& out, uint64_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 56) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 48) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 40) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 32) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

uint16_t CommProcess::readU16BE(const uint8_t* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t CommProcess::readU32BE(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

std::string CommProcess::toHex(const uint8_t* data, size_t len)
{
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0x0F]);
        out.push_back(kHex[data[i] & 0x0F]);
    }
    return out;
}

std::string CommProcess::bcdToDigitString(const uint8_t* data, size_t len)
{
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(static_cast<char>('0' + ((data[i] >> 4) & 0x0F)));
        out.push_back(static_cast<char>('0' + (data[i] & 0x0F)));
    }
    return out;
}

std::string CommProcess::bcdHourToHhmm(uint8_t bcdHour)
{
    const int hh = (((bcdHour >> 4) & 0x0F) * 10) + (bcdHour & 0x0F);
    char buf[5] = {0};
    std::snprintf(buf, sizeof(buf), "%02d00", hh);
    return std::string(buf);
}

uint8_t CommProcess::hhmmToBcdHour(const std::string& hhmm)
{
    if (hhmm.size() < 2) {
        return 0x00;
    }
    const int h0 = hhmm[0] - '0';
    const int h1 = hhmm[1] - '0';
    if (h0 < 0 || h0 > 9 || h1 < 0 || h1 > 9) {
        return 0x00;
    }
    int hour = h0 * 10 + h1;
    if (hour < 0 || hour > 23) {
        hour = 0;
    }
    return static_cast<uint8_t>(((hour / 10) << 4) | (hour % 10));
}

void CommProcess::appendOrderIdBcd10(std::vector<uint8_t>& out, const std::string& orderNo)
{
    std::string digits;
    digits.reserve(orderNo.size());
    for (size_t i = 0; i < orderNo.size(); ++i) {
        if (orderNo[i] >= '0' && orderNo[i] <= '9') {
            digits.push_back(orderNo[i]);
        }
    }
    if (digits.size() > 20) {
        digits = digits.substr(digits.size() - 20);
    }
    if (digits.size() < 20) {
        digits.insert(digits.begin(), 20 - digits.size(), '0');
    }
    for (size_t i = 0; i < 20; i += 2) {
        const uint8_t hi = static_cast<uint8_t>(digits[i] - '0');
        const uint8_t lo = static_cast<uint8_t>(digits[i + 1] - '0');
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
}

bool CommProcess::syncSystemTime(int year, int month, int day, int hour, int minute, int second)
{
    // BY ZF: 按平台下发时间执行系统校时（失败仅记录日志）。
    struct tm tmv;
    std::memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = year - 1900;
    tmv.tm_mon = month - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = hour;
    tmv.tm_min = minute;
    tmv.tm_sec = second;
    tmv.tm_isdst = -1;

    const time_t tt = std::mktime(&tmv);
    if (tt <= 0) {
        m_logSender.warn("platform_time_sync_fail", "invalid_bcd_time");
        return false;
    }

    struct timeval tv;
    tv.tv_sec = static_cast<long>(tt);
    tv.tv_usec = 0;
    if (::settimeofday(&tv, nullptr) != 0) {
        m_logSender.warn("platform_time_sync_fail", std::string("settimeofday_errno=") + std::to_string(errno));
        return false;
    }
    return true;
}

void CommProcess::resetCryptoSession()
{
    m_aesSessionKey.fill(0);
    m_aesSessionKeyReady = false;
    m_loginCryptoPrepared = false;
}

void CommProcess::prepareLoginCryptoContext()
{
    if (m_loginCryptoPrepared) {
        return;
    }
    // BY ZF: 上电/离线恢复后生成16字节一次性AES密钥A。
    if (RAND_bytes(m_aesSessionKey.data(), static_cast<int>(m_aesSessionKey.size())) != 1) {
        std::random_device rd;
        for (size_t i = 0; i < m_aesSessionKey.size(); ++i) {
            m_aesSessionKey[i] = static_cast<uint8_t>(rd() & 0xFF);
        }
    }
    m_aesSessionKeyReady = false;
    m_loginCryptoPrepared = true;
}

bool CommProcess::tryUpdateSm2PubKeyFromLoginAck(const uint8_t* body, size_t bodyLen)
{
    // BY ZF: 0x02登录应答：桩编码(7) + 登录结果(1) + 公钥(130)。
    // BY ZF: 平台在登录失败场景也可能回最新SM2公钥，收到后要立即更新并固化。
    if (bodyLen < 138) {
        return false;
    }
    std::string key(reinterpret_cast<const char*>(body + 8), 130U);
    if (key.empty() || key.find_first_not_of("0123456789ABCDEFabcdef") != std::string::npos) {
        return false;
    }
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][LOGIN_ACK_PUBKEY] " << key << std::endl;
    }
    if (key != m_sm2PublicKeyActive) {
        m_sm2PublicKeyActive = key;
        m_config.sm2PublicKey = key;
        const bool iniOk = persistSm2PubKeyToIni(key);
        m_logSender.info("sm2_pubkey_update", iniOk ? "platform_sm2_public_key_updated"
                                                    : "platform_sm2_public_key_updated_ini_save_fail");
    }
    return true;
}

bool CommProcess::tryUpdateRsaPubKeyFromResponse(const uint8_t* body, size_t bodyLen)
{
    if (!body || bodyLen == 0U) {
        return false;
    }

    size_t off = 0;
    size_t keyLen = bodyLen;
    if (bodyLen >= 2U) {
        const uint16_t lenLE = static_cast<uint16_t>(body[0] | (static_cast<uint16_t>(body[1]) << 8));
        const uint16_t lenBE = static_cast<uint16_t>((static_cast<uint16_t>(body[0]) << 8) | body[1]);
        if (lenLE > 0U && static_cast<size_t>(lenLE) <= bodyLen - 2U) {
            off = 2U;
            keyLen = lenLE;
        } else if (lenBE > 0U && static_cast<size_t>(lenBE) <= bodyLen - 2U) {
            off = 2U;
            keyLen = lenBE;
        } else if (body[0] > 0U && static_cast<size_t>(body[0]) <= bodyLen - 1U) {
            off = 1U;
            keyLen = body[0];
        }
    } else if (body[0] == 0U) {
        return false;
    }

    std::string key;
    {
        const unsigned char* p = body + off;
        RSA* rsa = d2i_RSAPublicKey(nullptr, &p, static_cast<long>(keyLen));
        if (!rsa) {
            p = body + off;
            rsa = d2i_RSA_PUBKEY(nullptr, &p, static_cast<long>(keyLen));
        }
        if (rsa) {
            // BY ZF: 平台 0x01 返回的是二进制 DER 公钥，统一转成 HEX 文本保存到运行态和 ini。
            key = toHex(body + off, keyLen);
            RSA_free(rsa);
        } else {
            key.assign(reinterpret_cast<const char*>(body + off), keyLen);
            while (!key.empty() && (key[0] == '\0' || std::isspace(static_cast<unsigned char>(key[0])) != 0)) {
                key.erase(key.begin());
            }
            while (!key.empty() && (key[key.size() - 1U] == '\0' ||
                                    std::isspace(static_cast<unsigned char>(key[key.size() - 1U])) != 0)) {
                key.erase(key.end() - 1);
            }
            if (key.empty()) {
                return false;
            }
        }
    }

    // BY ZF: 先实际加载一次，避免把异常内容写入运行态和配置。
    RSA* rsa = loadRsaPublicKey(key);
    if (!rsa) {
        return false;
    }
    RSA_free(rsa);

    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][RSA_PUBKEY_UPDATE] len=" << key.size() << std::endl;
    }
    if (key != m_rsaPublicKeyActive) {
        m_rsaPublicKeyActive = key;
        m_config.rsaPublicKey = key;
        const bool iniOk = persistRsaPubKeyToIni(key);
        m_logSender.info("rsa_pubkey_update", iniOk ? "platform_rsa_public_key_updated"
                                                    : "platform_rsa_public_key_updated_ini_save_fail");
    }
    return true;
}

std::vector<uint8_t> CommProcess::buildPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body, int seqOverride)
{
    return buildPlatformFrameEx(cmd, body, seqOverride, -1, -1, -1);
}

std::vector<uint8_t> CommProcess::buildPlatformFrameEx(uint8_t cmd, const std::vector<uint8_t>& body, int seqOverride,
                                                       int transferReasonOverride, int chargerAddrOverride,
                                                       int bodyCountOverride)
{
    (void)seqOverride;

    std::vector<uint8_t> payload;
    uint8_t encryptFlag = 0x00;
    if (!encryptBodyByCmd(cmd, body, m_aesSessionKey, m_aesSessionKeyReady, m_rsaPublicKeyActive, payload, encryptFlag)) {
        return std::vector<uint8_t>();
    }

    if (payload.size() > static_cast<size_t>(0xFFFFU - 5U)) {
        return std::vector<uint8_t>();
    }

    // BY ZF: NYC报文头：68H + 报文长度低/高 + 桩地址 + 帧类型 + 传送原因 + 加密方式 + 信息体个数。
    // BY ZF: 报文长度不含启动符和长度域，固定头后5字节 + 信息体长度；完整报文不附加中石化2.0 CRC。
    const uint16_t totalLen = static_cast<uint16_t>(5U + payload.size());
    const uint8_t bodyCount = (bodyCountOverride >= 0)
            ? static_cast<uint8_t>(bodyCountOverride & 0xFF)
            : static_cast<uint8_t>(payload.empty() ? 0x00 : 0x01);
    const uint8_t transferReason = (transferReasonOverride >= 0)
            ? static_cast<uint8_t>(transferReasonOverride & 0xFF)
            : 0x03;
    const uint8_t chargerAddr = (chargerAddrOverride >= 0)
            ? static_cast<uint8_t>(chargerAddrOverride & 0xFF)
            : 0;

    std::vector<uint8_t> frame;
    frame.reserve(static_cast<size_t>(3U + totalLen));
    frame.push_back(0x68);
    frame.push_back(static_cast<uint8_t>(totalLen & 0xFF));
    frame.push_back(static_cast<uint8_t>((totalLen >> 8) & 0xFF));
    frame.push_back(chargerAddr);
    frame.push_back(cmd);
    frame.push_back(transferReason);
    frame.push_back(encryptFlag); // BY ZF: 加密方式按帧类型自动判定。
    frame.push_back(bodyCount);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

bool CommProcess::sendPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body, int seqOverride)
{
    return sendPlatformFrameEx(cmd, body, seqOverride, -1, -1, -1);
}

bool CommProcess::sendPlatformFrameEx(uint8_t cmd, const std::vector<uint8_t>& body, int seqOverride,
                                      int transferReasonOverride, int chargerAddrOverride,
                                      int bodyCountOverride)
{
    if (m_tcpFd < 0) {
        return false;
    }
    const std::vector<uint8_t> frame = buildPlatformFrameEx(cmd, body, seqOverride,
                                                            transferReasonOverride,
                                                            chargerAddrOverride,
                                                            bodyCountOverride);
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][TX_FRAME] cmd=0x" << std::hex << static_cast<int>(cmd) << std::dec
                  << "(" << platformCmdName(cmd) << ")"
                  << " len=" << frame.size()
                  << " hex=" << toHex(frame.data(), frame.size()) << std::endl;
    }
    ssize_t n = ::send(m_tcpFd, frame.data(), frame.size(), 0);
    if (n < 0 || static_cast<size_t>(n) != frame.size()) {
        return false;
    }
    return true;
}

std::vector<uint8_t> CommProcess::buildRsaPublicKeyRequestBody() const
{
    // BY ZF: NYC 0x01请求RSA公钥无信息体，外层信息体个数填0。
    return std::vector<uint8_t>();
}

std::vector<uint8_t> CommProcess::buildLoginRequestBody() const
{
    // BY ZF: NYC 0x02设备认证明文：一次性AES密钥(16) + 资产编码长度(1) + 资产编码(ASCII)。
    // BY ZF: 外层sendPlatformFrame会按0x02自动使用主站RSA公钥/PKCS1加密，并写加密方式02H。
    std::vector<uint8_t> body;
    if (m_config.assetCode.empty() || m_config.assetCode.size() > 255U) {
        return std::vector<uint8_t>();
    }
    body.reserve(17U + m_config.assetCode.size());
    body.insert(body.end(), m_aesSessionKey.begin(), m_aesSessionKey.end());
    body.push_back(static_cast<uint8_t>(m_config.assetCode.size()));
    body.insert(body.end(), m_config.assetCode.begin(), m_config.assetCode.end());
    return body;
}

std::vector<uint8_t> CommProcess::buildFeeModelRequestBody(uint8_t gunNoBcd) const
{
    // BY ZF: NYC 0x73 请求计费模型无信息体，保留参数仅兼容旧调用点。
    (void)gunNoBcd;
    return std::vector<uint8_t>();
}

std::vector<uint8_t> CommProcess::buildHeartbeatBody()
{
    // BY ZF: NYC 0x07 心跳测试无信息体，外层信息体个数填0。
    return std::vector<uint8_t>();
}

std::vector<uint8_t> CommProcess::buildBrmBody(uint8_t gun) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    auto fillAsciiFixed = [](std::vector<uint8_t>& out, const std::string& s, size_t width) {
        size_t n = std::min(width, s.size());
        out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
        if (n < width) {
            out.insert(out.end(), width - n, 0x00);
        }
    };

    // BY ZF: 0x15 BRM 上送：交易流水号 + 桩编号 + 枪号。
    appendBcdFixed(body, rd.orderNo, 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));

    // BY ZF: BRM(BMS)通信协议版本号（3字节）。
    // BY ZF: 按协议示例 V1.10 => 0x01 0x01 0x00，上送3字节原值。
    body.insert(body.end(),
                rd.startCompleteData.pileBmsVersion.begin(),
                rd.startCompleteData.pileBmsVersion.end());

    // BY ZF: BRM 基础字段。
    body.push_back(rd.startCompleteData.batteryType);
    appendU16LE(body, rd.startCompleteData.ratedCapacity);
    appendU16LE(body, rd.startCompleteData.ratedTotalVoltage);

    fillAsciiFixed(body, rd.startCompleteData.batteryManufacturer, 4);
    body.insert(body.end(), rd.startCompleteData.batterySerial.begin(), rd.startCompleteData.batterySerial.end());

    body.push_back(rd.startCompleteData.batteryProdYear);
    body.push_back(rd.startCompleteData.batteryProdMonth);
    body.push_back(rd.startCompleteData.batteryProdDay);
    

    body.insert(body.end(), rd.startCompleteData.batteryChargeCount.begin(), rd.startCompleteData.batteryChargeCount.end());
    body.push_back(rd.startCompleteData.batteryPropertyFlag);
    // BY ZF: 预留1字节，当前固定0x00。
    body.push_back(0x00);
    fillAsciiFixed(body, rd.startCompleteData.vin, 17);
    body.insert(body.end(), rd.startCompleteData.bmsSoftwareVersion.begin(), rd.startCompleteData.bmsSoftwareVersion.end());
    // BY ZF: 离线模式标记，当前在线固定 0x00。
    body.push_back(0x00);
    return body;
}

std::vector<uint8_t> CommProcess::buildBcpBody(uint8_t gun) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];

    // BY ZF: 0x17 BCP参数配置报文。
    // 1) 交易流水号 BCD16
    appendBcdFixed(body, rd.orderNo, 16);
    // 2) 桩编号 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1（1..n）
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));

    // 4) BMS单体动力蓄电池最高允许充电电压（0.01V/位）
    appendU16LE(body, rd.startCompleteData.cellMaxChargeVoltage);
    // 5) BMS最高允许充电电流（0.1A/位，按协议偏移+800A）
    {
        int bmsMaxCurrentRaw = static_cast<int>(rd.startCompleteData.maxAllowChargeCurrent) + 8000;
        if (bmsMaxCurrentRaw < 0) {
            bmsMaxCurrentRaw = 0;
        }
        if (bmsMaxCurrentRaw > 65535) {
            bmsMaxCurrentRaw = 65535;
        }
        appendU16LE(body, static_cast<uint16_t>(bmsMaxCurrentRaw));
    }
    // 6) BMS动力蓄电池标称总能量（0.1kWh/位）
    appendU16LE(body, static_cast<uint16_t>(rd.startCompleteData.nominalEnergy));
    // 7) BMS最高允许充电总电压（0.1V/位）
    appendU16LE(body, rd.startCompleteData.bmsMaxChargeVoltage);
    // 8) BMS最高允许温度（偏移50，原始值直传）
    body.push_back(rd.startCompleteData.maxAllowTemp + 50);
    // 9) BMS整车动力蓄电池荷电状态SOC（0.1%/位）
    appendU16LE(body, static_cast<uint16_t>(rd.startCompleteData.soc));
    // 10) BMS整车动力蓄电池当前电池总电压（0.1V/位）
    appendU16LE(body, rd.startCompleteData.currentTotalVoltage);
    // 11) CML电桩最高输出电压（0.1V/位）
    appendU16LE(body, rd.startCompleteData.pileMaxOutputVoltage);
    // 12) CML电桩最低输出电压（0.1V/位）
    appendU16LE(body, rd.startCompleteData.pileMinOutputVoltage);
    // 13) CML电桩最大输出电流（0.1A/位，按协议偏移+800A）
    {
        int pileMaxCurrentRaw = static_cast<int>(rd.startCompleteData.pileMaxOutputCurrent) + 8000;
        if (pileMaxCurrentRaw < 0) {
            pileMaxCurrentRaw = 0;
        }
        if (pileMaxCurrentRaw > 65535) {
            pileMaxCurrentRaw = 65535;
        }
        appendU16LE(body, static_cast<uint16_t>(pileMaxCurrentRaw));
    }
    // 14) CML电桩最小输出电流（0.1A/位，按协议偏移+800A）
    {
        int pileMinCurrentRaw = static_cast<int>(rd.startCompleteData.pileMinOutputCurrent) + 8000;
        if (pileMinCurrentRaw < 0) {
            pileMinCurrentRaw = 0;
        }
        if (pileMinCurrentRaw > 65535) {
            pileMinCurrentRaw = 65535;
        }
        appendU16LE(body, static_cast<uint16_t>(pileMinCurrentRaw));
    }
    // 15) 是否离线模式下产生（当前固定在线0x00）
    body.push_back(0x00);

    return body;
}

std::vector<uint8_t> CommProcess::buildChargeEndStageBody(uint8_t gun, cJSON* stopCompleteData) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size() || !stopCompleteData) {
        return body;
    }

    // BY ZF: 结束阶段关键量直接取 stop_complete 事件，累计时间/电量取 feeData 运行态缓存。
    auto getU8 = [stopCompleteData](const char* key, uint8_t defVal) -> uint8_t {
        cJSON* n = cJSON_GetObjectItem(stopCompleteData, key);
        if (!n || !cJSON_IsNumber(n)) {
            return defVal;
        }
        int v = n->valueint;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<uint8_t>(v);
    };
    auto getU16 = [stopCompleteData](const char* key, uint16_t defVal) -> uint16_t {
        cJSON* n = cJSON_GetObjectItem(stopCompleteData, key);
        if (!n || !cJSON_IsNumber(n)) {
            return defVal;
        }
        int v = n->valueint;
        if (v < 0) v = 0;
        if (v > 0xFFFF) v = 0xFFFF;
        return static_cast<uint16_t>(v);
    };
    auto getTemp = [stopCompleteData](const char* key, uint8_t defVal) -> uint8_t {
        cJSON* n = cJSON_GetObjectItem(stopCompleteData, key);
        if (!n || !cJSON_IsNumber(n)) {
            return defVal;
        }
        int v = n->valueint + 50;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<uint8_t>(v);
    };

    const GunRuntimeData& rd = m_gunRuntimeData[gun];

    // BY ZF: 0x19 上传充电结束阶段报文。
    // 1) 交易流水号 BCD16
    appendBcdFixed(body, rd.orderNo, 16);
    // 2) 桩编号 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1（1..n）
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    // 4) BMS中止充电状态SOC BIN1
    body.push_back(getU8("stopSoc", 0));
    // 5) BMS动力蓄电池单体最低电压 BIN2（直接沿用 stop_complete 数据）
    appendU16LE(body, getU16("cellMinVoltage", 0));
    // 6) BMS动力蓄电池单体最高电压 BIN2（直接沿用 stop_complete 数据）
    appendU16LE(body, getU16("cellMaxVoltage", 0));
    // 7) BMS动力蓄电池最低温度 BIN1（按协议加50偏移）
    body.push_back(getTemp("batteryMinTemp", 50));
    // 8) BMS动力蓄电池最高温度 BIN1（按协议加50偏移）
    body.push_back(getTemp("batteryMaxTemp", 50));
    // BY ZF: feeData.chargedTime 当前缓存单位为秒，0x19 要求分钟。
    const uint16_t chargedMinutes = static_cast<uint16_t>(std::max(0.0, rd.chargedTime / 60.0));
    // 9) 本次充电累计充电时间 BIN2（min）
    appendU16LE(body, chargedMinutes);
    // BY ZF: feeData.totalEnergy 当前缓存单位为kWh，0x19 要求0.1kWh。
    int energyRaw = static_cast<int>(rd.totalEnergy * 10.0);
    if (energyRaw < 0) energyRaw = 0;
    if (energyRaw > 0xFFFF) energyRaw = 0xFFFF;
    // 10) 本次充电电量 BIN2（0.1kWh）
    appendU16LE(body, static_cast<uint16_t>(energyRaw));
    // 11) 是否离线模式下产生 BIN1（当前在线固定0x00）
    body.push_back(0x00);
    return body;
}

std::vector<uint8_t> CommProcess::buildBstBody(uint8_t gun, cJSON* stopCompleteData) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size() || !stopCompleteData) {
        return body;
    }

    auto getU8 = [stopCompleteData](const char* key, uint8_t defVal) -> uint8_t {
        cJSON* n = cJSON_GetObjectItem(stopCompleteData, key);
        if (!n || !cJSON_IsNumber(n)) {
            return defVal;
        }
        int v = n->valueint;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<uint8_t>(v);
    };
    auto getU16 = [stopCompleteData](const char* key, uint16_t defVal) -> uint16_t {
        cJSON* n = cJSON_GetObjectItem(stopCompleteData, key);
        if (!n || !cJSON_IsNumber(n)) {
            return defVal;
        }
        int v = n->valueint;
        if (v < 0) v = 0;
        if (v > 0xFFFF) v = 0xFFFF;
        return static_cast<uint16_t>(v);
    };

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    // BY ZF: 0x1D BST停车中止上送报文。
    // 1) 交易流水号 BCD16
    appendBcdFixed(body, rd.orderNo, 16);
    // 2) 桩编号 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1（1..n）
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    // 4) BMS中止充电原因 BIN1
    body.push_back(getU8("bmsStopReason", 0));
    // 5) BMS中止充电故障原因 BIN2
    appendU16LE(body, getU16("bmsChargeFaultReason", 0));
    // 6) BMS中止充电错误原因 BIN1
    body.push_back(getU8("bmsStopErrorReason", 0));
    // 7) 是否离线模式下产生 BIN1（当前在线固定0x00）
    body.push_back(0x00);
    return body;
}

std::vector<uint8_t> CommProcess::buildBclBcsCcsBody(uint8_t gun) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];

    auto clampU8 = [](int v) -> uint8_t {
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<uint8_t>(v);
    };
    auto clampU16 = [](int v) -> uint16_t {
        if (v < 0) v = 0;
        if (v > 0xFFFF) v = 0xFFFF;
        return static_cast<uint16_t>(v);
    };

    // BY ZF: 0x23 上送充电过程BMS需求与充电机输出报文（15秒周期）。
    // 1) 交易流水号 BCD16
    appendBcdFixed(body, rd.orderNo, 16);
    // 2) 桩编号 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1（1..n）
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));

    // 4) BCL BMS电压需求（0.1V）
    appendU16LE(body, clampU16(static_cast<int>(rd.bmsReqVoltage * 10.0)));
    // 5) BCL BMS电流需求（0.1A，偏移+800A）
    appendU16LE(body, clampU16(static_cast<int>(rd.bmsReqCurrent * 10.0 + 8000.0)));
    // 6) BCL BMS充电模式
    body.push_back(clampU8(rd.ycChargeMode));
    // 7) BCS BMS当前电压测量值（0.1V）
    appendU16LE(body, clampU16(static_cast<int>(rd.bmsMeasuredVoltage * 10.0)));
    // 8) BCS BMS当前电流测量值（0.1A，偏移+800A）
    appendU16LE(body, clampU16(static_cast<int>(rd.bmsMeasuredCurrent * 10.0 + 8000.0)));
    // BY ZF: 当前仅缓存最高/最低单体编号，分组号无来源，先按0上送。
    const uint16_t cellMaxVoltageAndGroup =
            static_cast<uint16_t>(clampU16(static_cast<int>(rd.cellMaxVoltage * 100.0)) & 0x0FFFU);
    // 9) BCS 最高单体动力蓄电池电压及组号
    appendU16LE(body, cellMaxVoltageAndGroup);
    // 10) BCS 当前荷电状态SOC（%）
    body.push_back(clampU8(static_cast<int>(rd.soc + 0.5)));
    // 11) BCS 估算剩余充电时间（min）
    appendU16LE(body, clampU16(static_cast<int>(rd.estimatedRemainTime)));
    // 12) CCS 电桩电压输出值（0.1V）
    appendU16LE(body, clampU16(static_cast<int>(rd.voltage * 10.0)));
    // 13) CCS 电桩电流输出值（0.1A，偏移+800A）
    appendU16LE(body, clampU16(static_cast<int>(rd.current * 10.0 + 8000.0)));
    // 14) CCS 累计充电时间（min）
    appendU16LE(body, clampU16(static_cast<int>(rd.chargedTime / 60.0)));
    // 15) 是否离线模式下产生
    body.push_back(0x00);
    const uint16_t cellMinVoltageAndGroup =
            static_cast<uint16_t>(clampU16(static_cast<int>(rd.cellMinVoltage * 100.0)) & 0x0FFFU);
    // 16) 最低单体动力蓄电池电压及组号
    appendU16LE(body, cellMinVoltageAndGroup);
    // 17) 预留字段（BCD4）
    body.insert(body.end(), 4, 0x00);
    return body;
}

std::vector<uint8_t> CommProcess::buildCstBody(uint8_t gun) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    // BY ZF: 0x21 CST充电中止上送报文（当前中止原因固定0x00）。
    // 1) 交易流水号 BCD16
    appendBcdFixed(body, rd.orderNo, 16);
    // 2) 桩编号 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1（1..n）
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    // 4) 充电桩中止充电原因 BIN1（固定0x00）
    body.push_back(0x00);
    // 5) 充电桩中止充电故障原因 BIN2（固定0x0000）
    appendU16LE(body, 0x0000);
    // 6) 充电桩中止充电错误原因 BIN1（固定0x00）
    body.push_back(0x00);
    // 7) 是否离线模式下产生 BIN1（当前在线固定0x00）
    body.push_back(0x00);
    return body;
}

std::vector<uint8_t> CommProcess::buildBsmBody(uint8_t gun) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }
    const GunRuntimeData& rd = m_gunRuntimeData[gun];

    auto clampU8 = [](int v) -> uint8_t {
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<uint8_t>(v);
    };

    // BY ZF: 0x25 BSM上送报文（充电中15秒周期）。
    // 1) 交易流水号 BCD16
    appendBcdFixed(body, rd.orderNo, 16);
    // 2) 桩编号 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1（1..n）
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));

    // 4) BMS最高单体动力蓄电池电压所在编号（1Byte）
    body.push_back(clampU8(rd.maxVoltageCellNo));
    // 5) BMS当前单体动力蓄电池最高温度（1Byte，偏移50）
    body.push_back(clampU8(static_cast<int>(rd.batteryMaxTemp + 50.0)));
    // 6) BMS最高温度探测点编号（1Byte）
    body.push_back(clampU8(rd.maxTempPointNo));
    // 7) BMS当前单体动力蓄电池最低温度（1Byte，偏移50）
    body.push_back(clampU8(static_cast<int>(rd.batteryMinTemp + 50.0)));
    // 8) BMS最低动力蓄电池温度探测点编号（1Byte）
    body.push_back(clampU8(rd.minTempPointNo));

    // 9~16) 8组状态位，每组2bit，共16bit(2Byte)。
    uint16_t bsmStatusWord = 0x0000;
    // g9~g14 默认00(正常)
    // g15 充电禁止：00=禁止，01=允许
    bsmStatusWord |= static_cast<uint16_t>(0x01U << 2); // g15
    // g16 预留默认00
    appendU16LE(body, bsmStatusWord);
    // 17) 是否离线模式下产生（1Byte）
    body.push_back(0x00);
    // 18) 预留字段（BCD4）
    body.insert(body.end(), 4, 0x00);
    return body;
}

std::vector<uint8_t> CommProcess::buildPlugChargeAuthBody(uint8_t gun, cJSON* data)
{
    std::vector<uint8_t> body;
    if (!data || gun >= m_gunRuntimeData.size()) {
        return body;
    }

    std::string vin = jsonGetStringCompat(data, "vin", "vinCode");
    vin = sanitizeVin17(vin);
    if (vin.empty()) {
        return body;
    }

    GunRuntimeData& rd = m_gunRuntimeData[gun];
    rd.pendingVinAuthVin = vin;

    body.reserve(2U + vin.size());
    body.push_back(0x00); // BY ZF: NYC 0x45 认证类型，00=车架号/VIN。
    body.push_back(static_cast<uint8_t>(vin.size() & 0xFFU));
    body.insert(body.end(), vin.begin(), vin.end());
    return body;
}

std::vector<uint8_t> CommProcess::buildRemoteStartAckBody(uint8_t remoteControlType, const std::string& message) const
{
    std::vector<uint8_t> body;
    // BY ZF: NYC 0x41 遥控启动应答：遥控性质(1) + 响应消息长度(1) + 响应消息(ASCII)。
    body.push_back(static_cast<uint8_t>(remoteControlType));
    const size_t textLen = std::min<size_t>(message.size(), 255U);
    body.push_back(static_cast<uint8_t>(textLen));
    body.insert(body.end(), message.begin(), message.begin() + static_cast<std::ptrdiff_t>(textLen));
    return body;
}

std::vector<uint8_t> CommProcess::buildRemoteStopAckBody(uint8_t stopNature, uint8_t stopReason, const std::string& message) const
{
    std::vector<uint8_t> body;
    // BY ZF: NYC 0x43 遥控停止应答：停止性质(1) + 停止原因(1) + 响应消息长度(1) + 响应消息(ASCII)。
    body.push_back(static_cast<uint8_t>(stopNature));
    body.push_back(static_cast<uint8_t>(stopReason));
    const size_t textLen = std::min<size_t>(message.size(), 255U);
    body.push_back(static_cast<uint8_t>(textLen));
    body.insert(body.end(), message.begin(), message.begin() + static_cast<std::ptrdiff_t>(textLen));
    return body;
}

std::vector<uint8_t> CommProcess::buildStartChargeResultBody(uint8_t gun) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    body.reserve(32);
    // BY ZF: 1) 交易流水号 BCD16，优先使用启动阶段缓存的订单号。
    appendBcdFixed(body, rd.orderNo, 16);
    // BY ZF: 2) 桩编号 BCD7。
    appendPileCodeBcd7(body, m_config.cdzNo);
    // BY ZF: 3) 枪号 BCD1（按1..n上送）。
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    // BY ZF: 4) 启动结果 BCD1：0x02成功，0x01失败。
    const int successFlag = static_cast<int>(rd.startCompleteData.successFlag);
    body.push_back(static_cast<uint8_t>(successFlag == 0 ? 0x02 : 0x01));
    // BY ZF: 5) 失败原因 BIN1（成功时填0x00）。
    int failReason = static_cast<int>(rd.startCompleteData.failReason);
    if (failReason < 0) {
        failReason = 0;
    }
    if (failReason > 255) {
        failReason = 255;
    }
    body.push_back(static_cast<uint8_t>(successFlag == 0 ? 0x00 : failReason));
    return body;
}

std::vector<uint8_t> CommProcess::buildAllTelesignalBody(uint8_t& pileCount) const
{
    std::vector<uint8_t> body;
    pileCount = 0U;
    const size_t gunCount = std::min(m_gunRuntimeData.size(), static_cast<size_t>(0xFF));
    if (gunCount == 0U) {
        return body;
    }

    body.reserve(gunCount * 8U);
    for (size_t gun = 0; gun < gunCount; ++gun) {
        const std::array<uint8_t, 6> values = buildTelesignalValues(m_gunRuntimeData[gun]);
        body.push_back(static_cast<uint8_t>(gun + 1)); // BY ZF: 充电桩地址按枪号1..N发送。
        body.push_back(6U); // BY ZF: 当前协议定义 6 个遥信量。
        for (size_t sig = 0; sig < values.size(); ++sig) {
            body.push_back(values[sig]);
        }
    }
    pileCount = static_cast<uint8_t>(gunCount);
    return body;
}

std::vector<uint8_t> CommProcess::buildAllTelemetryBody(uint8_t& pileCount) const
{
    std::vector<uint8_t> body;
    pileCount = 0U;
    const size_t gunCount = std::min(m_gunRuntimeData.size(), static_cast<size_t>(0xFF));
    if (gunCount == 0U) {
        return body;
    }

    body.reserve(gunCount * 66U);
    for (size_t gun = 0; gun < gunCount; ++gun) {
        const GunRuntimeData& rd = m_gunRuntimeData[gun];
        uint32_t peakEnergy = 0U;
        uint32_t valleyEnergy = 0U;
        uint32_t sharpEnergy = 0U;
        uint32_t flatEnergy = 0U;

        if (gun < m_feeModelByGun.size()) {
            const FeeModel& feeModel = m_feeModelByGun[gun];
            const size_t segCount = std::min(rd.feeSegments.size(), feeModel.segFlag.size());
            for (size_t i = 0; i < segCount; ++i) {
                const uint32_t segEnergy = scaleToU32(rd.feeSegments[i].energyKwh, 1000.0);
                switch (feeModel.segFlag[i]) {
                case 1U: peakEnergy += segEnergy; break;
                case 2U: valleyEnergy += segEnergy; break;
                case 3U: sharpEnergy += segEnergy; break;
                case 4U: flatEnergy += segEnergy; break;
                default: break;
                }
            }
        }

        const uint32_t totalMinutes = static_cast<uint32_t>(std::max(0.0, rd.chargedTime / 60.0));
        const uint32_t chargeHours = totalMinutes / 60U;
        const uint32_t chargeMinutes = totalMinutes % 60U;
        const double outputPowerKw = (rd.voltage * rd.current) / 1000.0;
        const uint32_t totalAmountRaw = scaleToU32((rd.totalAmount > 0.0) ? rd.totalAmount
                                                                          : (rd.electricAmount + rd.serviceAmount),
                                                   100.0);
        uint32_t faultCode = static_cast<uint32_t>(rd.yxOtherFault);
        if (faultCode == 0U) {
            faultCode = static_cast<uint32_t>(rd.yxTotalFault);
        }

        body.push_back(static_cast<uint8_t>(gun + 1));
        body.push_back(16U);
        appendU32LE(body, scaleToU32(rd.voltage, 100.0));        // 0 电压，0.01V
        appendU32LE(body, scaleToU32(rd.current, 100.0));        // 1 电流，0.01A
        appendU32LE(body, chargeHours);                          // 2 已充电时间-小时
        appendU32LE(body, chargeMinutes);                        // 3 已充电时间-分钟
        appendU32LE(body, scaleToU32(outputPowerKw, 1000.0));   // 4 输出功率，0.001kW
        appendU32LE(body, scaleToU32(rd.soc, 10.0));            // 5 SOC，0.1
        appendU32LE(body, faultCode);                           // 6 故障代码值
        appendU32LE(body, scaleToU32(rd.totalEnergy, 1000.0));  // 7 充电总电量，0.001kWh
        appendU32LE(body, totalAmountRaw);                      // 8 充电总费用，0.01元
        appendU32LE(body, peakEnergy);                          // 9 峰电量，0.001kWh
        appendU32LE(body, valleyEnergy);                        // 10 谷电量，0.001kWh
        appendU32LE(body, sharpEnergy);                         // 11 尖电量，0.001kWh
        appendU32LE(body, flatEnergy);                          // 12 平电量，0.001kWh
        appendU32LE(body, scaleToU32(rd.electricAmount, 100.0)); // 13 充电电费，0.01元
        appendU32LE(body, scaleToU32(rd.serviceAmount, 100.0));  // 14 充电服务费，0.01元
        appendU32LE(body, scaleToU32(rd.meterEnergy, 1000.0));   // 15 电表表示数，0.001kWh
    }

    pileCount = static_cast<uint8_t>(gunCount);
    return body;
}

std::vector<uint8_t> CommProcess::buildAlarmBody(uint8_t gun, uint16_t faultCode, bool faultActive, uint8_t& alarmCount) const
{
    std::vector<uint8_t> body;
    alarmCount = 0U;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    char detail[5] = {0};
    std::snprintf(detail, sizeof(detail), "%04X", static_cast<unsigned int>(faultCode & 0xFFFFU));
    body.reserve(7U);
    body.push_back(0x00U); // BY ZF: 故障告警码固定00，详情中透传 otherFault 的十六进制字符串。
    body.push_back(faultActive ? 0x01U : 0x00U);
    body.push_back(0x04U);
    body.insert(body.end(), detail, detail + 4);
    alarmCount = 1U;
    return body;
}

std::vector<uint8_t> CommProcess::buildChangedTelesignalBody(uint8_t gun, uint8_t& changeCount) const
{
    std::vector<uint8_t> body;
    changeCount = 0U;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    const std::array<uint8_t, 6> currentValues = buildTelesignalValues(m_gunRuntimeData[gun]);
    const bool hasBaseline = (gun < m_lastTelesignalValidByGun.size() && m_lastTelesignalValidByGun[gun] != 0U);
    const std::array<uint8_t, 6> previousValues =
            (gun < m_lastTelesignalValuesByGun.size()) ? m_lastTelesignalValuesByGun[gun]
                                                       : std::array<uint8_t, 6>{{0}};
    body.reserve(12U);
    for (uint8_t sig = 0; sig < 6U; ++sig) {
        if (!hasBaseline || currentValues[sig] != previousValues[sig]) {
            body.push_back(sig);
            body.push_back(currentValues[sig]);
            ++changeCount;
        }
    }
    return body;
}

void CommProcess::reportTelesignalPeriodic()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const uint8_t gunCount = static_cast<uint8_t>(std::min(m_gunRuntimeData.size(), m_runtimeChangedByGun.size()));

    // BY ZF: 遥信量变化时，按枪立即突发 0x17 变化遥信。
    for (uint8_t gun = 0; gun < gunCount; ++gun) {
        if (m_runtimeChangedByGun[gun] == 0U) {
            continue;
        }
        const bool faultActive = (m_gunRuntimeData[gun].yxTotalFault != 0U);
        const uint16_t faultCode = m_gunRuntimeData[gun].yxOtherFault;
        const bool hadFaultBaseline =
                (gun < m_lastFaultActiveByGun.size() && gun < m_lastFaultCodeByGun.size());
        const bool needFaultReport =
                hadFaultBaseline &&
                ((m_lastFaultActiveByGun[gun] != (faultActive ? 1U : 0U)) ||
                 (faultActive && m_lastFaultCodeByGun[gun] != faultCode));
        if (needFaultReport) {
            const uint16_t reportFaultCode = faultActive ? faultCode : m_lastFaultCodeByGun[gun];
            uint8_t alarmCount = 0U;
            const std::vector<uint8_t> alarmBody = buildAlarmBody(gun, reportFaultCode, faultActive, alarmCount);
            if (alarmCount > 0U &&
                !sendPlatformFrameEx(kCmdAlarm, alarmBody, -1, 0x01, static_cast<int>(gun + 1), alarmCount)) {
                closePlatformTcp();
                return;
            }
        }
        uint8_t changeCount = 0U;
        const std::vector<uint8_t> changeBody = buildChangedTelesignalBody(gun, changeCount);
        const std::array<uint8_t, 6> currentValues = buildTelesignalValues(m_gunRuntimeData[gun]);
        if (changeCount == 0U) {
            m_runtimeChangedByGun[gun] = 0U;
            if (gun < m_lastTelesignalValuesByGun.size()) {
                m_lastTelesignalValuesByGun[gun] = currentValues;
            }
            if (gun < m_lastTelesignalValidByGun.size()) {
                m_lastTelesignalValidByGun[gun] = 1U;
            }
            if (gun < m_lastFaultActiveByGun.size()) {
                m_lastFaultActiveByGun[gun] = faultActive ? 1U : 0U;
            }
            if (gun < m_lastFaultCodeByGun.size()) {
                m_lastFaultCodeByGun[gun] = faultCode;
            }
            continue;
        }
        if (!sendPlatformFrameEx(kCmdChangeTelemetry, changeBody, -1, 0x01, static_cast<int>(gun + 1), changeCount)) {
            closePlatformTcp();
            return;
        }
        m_runtimeChangedByGun[gun] = 0U;
        if (gun < m_lastTelesignalValuesByGun.size()) {
            m_lastTelesignalValuesByGun[gun] = currentValues;
        }
        if (gun < m_lastTelesignalValidByGun.size()) {
            m_lastTelesignalValidByGun[gun] = 1U;
        }
        if (gun < m_lastFaultActiveByGun.size()) {
            m_lastFaultActiveByGun[gun] = faultActive ? 1U : 0U;
        }
        if (gun < m_lastFaultCodeByGun.size()) {
            m_lastFaultCodeByGun[gun] = faultCode;
        }
    }

    // BY ZF: 每 30 秒发送一次 0x12 全遥信，信息体覆盖全部枪。
    if (now - m_lastChargeInfoReport >= std::chrono::seconds(30)) {
        uint8_t pileCount = 0U;
        const std::vector<uint8_t> body = buildAllTelesignalBody(pileCount);
        if (pileCount > 0U) {
            if (!sendPlatformFrameEx(kCmdAllTelesignal, body, -1, 0x04, 0x00, pileCount)) {
                closePlatformTcp();
                return;
            }
            m_lastChargeInfoReport = now;
            for (uint8_t gun = 0; gun < pileCount; ++gun) {
                if (gun < m_lastTelesignalValuesByGun.size()) {
                    m_lastTelesignalValuesByGun[gun] = buildTelesignalValues(m_gunRuntimeData[gun]);
                }
                if (gun < m_lastTelesignalValidByGun.size()) {
                    m_lastTelesignalValidByGun[gun] = 1U;
                }
                if (gun < m_runtimeChangedByGun.size()) {
                    m_runtimeChangedByGun[gun] = 0U;
                }
            }
        } else {
            m_lastChargeInfoReport = now;
        }
    }

    // BY ZF: 保留充电过程中 0x23/0x24 周期业务上送，不与 0x12/0x17 互相耦合。
    for (uint8_t gun = 0; gun < static_cast<uint8_t>(std::min(m_gunRuntimeData.size(), m_lastChargeInfoReportByGun.size())); ++gun) {
        const bool charging = (m_gunRuntimeData[gun].gunStatus == 0x03);
        if (now - m_lastChargeInfoReportByGun[gun] < std::chrono::seconds(15)) {
            continue;
        }
        if (charging) {
            const std::vector<uint8_t> bclBcsCcsBody = buildBclBcsCcsBody(gun);
            if (!bclBcsCcsBody.empty() && !sendPlatformFrame(kCmdRawMessagePassThrough, bclBcsCcsBody)) {
                closePlatformTcp();
                return;
            }
            const std::vector<uint8_t> bsmBody = buildBsmBody(gun);
            if (!bsmBody.empty() && !sendPlatformFrame(kCmdChargeCurveCall, bsmBody)) {
                closePlatformTcp();
                return;
            }
            m_lastChargeInfoReportByGun[gun] = now;
        }
    }
}

void CommProcess::reportTelemetryPeriodic()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - m_lastTelemetryReport < std::chrono::seconds(10)) {
        return;
    }

    uint8_t pileCount = 0U;
    const std::vector<uint8_t> body = buildAllTelemetryBody(pileCount);
    if (pileCount > 0U) {
        if (!sendPlatformFrameEx(kCmdAllTelemetry, body, -1, 0x04, 0x00, pileCount)) {
            closePlatformTcp();
            return;
        }
    }
    m_lastTelemetryReport = now;
}

void CommProcess::driveLoginStateMachine(const std::chrono::steady_clock::time_point& now)
{
    if (!m_platformConnected.load()) {
        return;
    }

    switch (m_loginState) {
    case LOGIN_IDLE:
        if (m_nextLoginAllowedTime.time_since_epoch().count() != 0 &&
            now < m_nextLoginAllowedTime) {
            break;
        }
        prepareLoginCryptoContext();
        m_loginState = LOGIN_REQ_RSA_PUBLIC_KEY;
        m_lastLoginAction = now - std::chrono::seconds(m_config.loginRetrySec);
        break;
    case LOGIN_REQ_RSA_PUBLIC_KEY:
        if (now - m_lastLoginAction >= std::chrono::seconds(m_config.loginRetrySec)) {
            const std::vector<uint8_t> rsaReqBody = buildRsaPublicKeyRequestBody();
            if (sendPlatformFrame(kCmdRequestRsaPublicKey, rsaReqBody)) {
                m_logSender.info("platform_login_step", "rsa_pubkey_req_sent");
            } else {
                closePlatformTcp();
            }
            m_lastLoginAction = now;
        }
        break;
    case LOGIN_REQ_FEE_MODEL: {
        if (now - m_lastLoginAction >= std::chrono::seconds(m_config.loginRetrySec)) {
            if (sendPlatformFrameEx(kCmdFeeModelReq, buildFeeModelRequestBody(0x01), -1,
                                    0x03, 0x01, 0x00)) {
                m_logSender.info("platform_login_step", "fee_model_req_sent");
            } else {
                closePlatformTcp();
            }
            m_lastLoginAction = now;
        }
        break;
    }
    case LOGIN_REQ_AUTH:
        if (now - m_lastLoginAction >= std::chrono::seconds(m_config.loginRetrySec)) {
            const std::vector<uint8_t> loginBody = buildLoginRequestBody();
            if (loginBody.empty()) {
                m_logSender.warn("platform_login_step", "login_req_build_fail");
            } else if (sendPlatformFrame(kCmdDeviceAuth, loginBody)) {
                m_logSender.info("platform_login_step", "device_auth_sent");
            } else {
                closePlatformTcp();
            }
            m_lastLoginAction = now;
        }
        break;
    case LOGIN_ONLINE: {
        // BY ZF: NYC 协议要求集中器每20秒发送一次心跳测试。
        if (now - m_lastHeartbeat >= std::chrono::seconds(m_config.tcpHeartbeatSec)) {
            if (!sendPlatformFrame(kCmdHeartbeat, buildHeartbeatBody())) {
                m_logSender.warn("platform_heartbeat", "send_fail");
                if (m_config.debugTcp) {
                    std::cout << "[Comm][TCP][CLOSE_REASON] heartbeat_send_fail" << std::endl;
                }
                closePlatformTcp();
            } else {
                m_lastHeartbeat = now;
            }
        }
        reportTelemetryPeriodic();
        reportTelesignalPeriodic();
        break;
    }
    default:
        break;
    }
}

void CommProcess::maintainPlatformTcp()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const int heartbeatTimeoutSec = 120;
    // BY ZF: NYC 协议要求 120 秒内未收到主站有效正确报文/有效回应时，集中器主动断开 TCP。
    if (m_platformConnected.load() &&
        now - m_lastHeartbeatRecv >= std::chrono::seconds(heartbeatTimeoutSec)) {
        if (m_platformOnlineEventActive && !m_config.offlineRunMode) {
            m_platformOnlineEventActive = false;
            publishPlatformLinkEvent(false, "heartbeat_timeout");
        }
        m_logSender.warn("platform_heartbeat", "recv_timeout_120s");
        if (m_config.debugTcp) {
            std::cout << "[Comm][TCP][CLOSE_REASON] heartbeat_recv_timeout_120s" << std::endl;
        }
        closePlatformTcp();
    }

    if (!m_platformConnected.load()) {
        if (m_lastTcpConnectTry.time_since_epoch().count() == 0 ||
            now - m_lastTcpConnectTry >= std::chrono::seconds(m_config.tcpReconnectSec)) {
            m_lastTcpConnectTry = now;
            connectPlatformTcp();
        }
        return;
    }

    char buf[512];
    const ssize_t n = ::recv(m_tcpFd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n == 0) {
        m_logSender.warn("platform_tcp", "peer_closed");
        if (m_config.debugTcp) {
            std::cout << "[Comm][TCP][CLOSE_REASON] peer_closed" << std::endl;
        }
        closePlatformTcp();
        return;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        m_logSender.warn("platform_tcp",
                         std::string("recv_error_") + std::strerror(errno));
        if (m_config.debugTcp) {
            std::cout << "[Comm][TCP][CLOSE_REASON] recv_error errno=" << errno
                      << " msg=" << std::strerror(errno) << std::endl;
        }
        closePlatformTcp();
        return;
    }
    if (n > 0) {
        handlePlatformRxData(buf, static_cast<size_t>(n));
    }

    driveLoginStateMachine(now);
}

void CommProcess::handlePlatformRxData(const char* data, size_t len)
{
    if (!data || len == 0) {
        return;
    }
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][RX] len=" << len
                  << " hex=" << toHex(reinterpret_cast<const uint8_t*>(data), len) << std::endl;
    }
    m_tcpRxCache.insert(m_tcpRxCache.end(), data, data + len);

    while (m_tcpRxCache.size() >= 8) {
        // BY ZF: 同步头 0x68
        if (static_cast<uint8_t>(m_tcpRxCache[0]) != 0x68) {
            m_tcpRxCache.erase(m_tcpRxCache.begin());
            continue;
        }
        const uint16_t totalLen = static_cast<uint16_t>(
                    static_cast<uint8_t>(m_tcpRxCache[1]) |
                    (static_cast<uint16_t>(static_cast<uint8_t>(m_tcpRxCache[2])) << 8));
        const size_t frameLen = static_cast<size_t>(3 + totalLen); // BY ZF: 启动符+长度域+报文长度。
        if (totalLen < 5U || frameLen < 8U) {
            m_tcpRxCache.erase(m_tcpRxCache.begin());
            continue;
        }
        if (m_tcpRxCache.size() < frameLen) {
            break;
        }

        processPlatformPacket(m_tcpRxCache.data(), frameLen);
        m_tcpRxCache.erase(m_tcpRxCache.begin(), m_tcpRxCache.begin() + static_cast<long>(frameLen));
    }
}

void CommProcess::processPlatformPacket(const uint8_t* frame, size_t frameLen)
{
    if (!frame || frameLen < 8) {
        return;
    }
    const uint16_t totalLen = static_cast<uint16_t>(frame[1] | (static_cast<uint16_t>(frame[2]) << 8));
    if (totalLen < 5U || frameLen != static_cast<size_t>(3 + totalLen)) {
        return;
    }
    const uint8_t chargerAddr = frame[3];
    const uint8_t cmd = frame[4];
    const uint8_t transferReason = frame[5];
    const uint8_t encryptFlag = frame[6];
    const uint8_t bodyCount = frame[7];
    const uint16_t rxSeq = 0; // BY ZF: NYC帧头无序号字段，兼容旧应答接口占位。
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][RX_FRAME] cmd=0x" << std::hex << static_cast<int>(cmd) << std::dec
                  << "(" << platformCmdName(cmd) << ")"
                  << " len=" << frameLen
                  << " addr=" << static_cast<int>(chargerAddr)
                  << " reason=0x" << std::hex << static_cast<int>(transferReason) << std::dec
                  << " bodyCount=" << static_cast<int>(bodyCount)
                  << std::endl;
    }
    const size_t bodyLen = static_cast<size_t>(totalLen - 5);
    const uint8_t* bodyRaw = (bodyLen > 0) ? (frame + 8) : nullptr;
    std::vector<uint8_t> decBody;
    const bool keyAvailable = m_aesSessionKeyReady;
    if (!decryptBodyByFlag(encryptFlag, bodyRaw, bodyLen, m_aesSessionKey, keyAvailable, m_rsaPublicKeyActive, decBody)) {
        if (m_config.debugTcp) {
            std::cout << "[Comm][TCP][RX_BODY_DEC_FAIL] cmd=0x" << std::hex << static_cast<int>(cmd) << std::dec
                      << " encFlag=0x" << std::hex << static_cast<int>(encryptFlag) << std::dec
                      << " bodyLen=" << bodyLen
                      << " keyReady=" << (keyAvailable ? 1 : 0)
                      << std::endl;
        }
        m_logSender.warn("platform_rx_decrypt_fail", std::string("cmd=") + std::to_string(cmd));
        return;
    }
    if (m_config.debugTcp && bodyLen > 0) {
        std::cout << "[Comm][TCP][RX_BODY_DEC] cmd=0x" << std::hex << static_cast<int>(cmd) << std::dec
                  << " encFlag=0x" << std::hex << static_cast<int>(encryptFlag) << std::dec
                  << " rawLen=" << bodyLen
                  << " decLen=" << decBody.size()
                  << " decHex=" << (decBody.empty() ? "" : toHex(decBody.data(), decBody.size()))
                  << std::endl;
    }
    const uint8_t* body = decBody.empty() ? nullptr : decBody.data();
    const size_t decBodyLen = decBody.size();
    // BY ZF: NYC 120 秒断链按“主站有效正确报文”判定，任意合法来包都刷新接收时刻。
    m_lastHeartbeatRecv = std::chrono::steady_clock::now();
    // BY ZF: 关键业务指令接收留痕，便于联调追踪关键时刻。
    switch (cmd) {
    case kCmdRemoteStartCmd:
    case kCmdUploadTradeRecord:
    case kCmdRemoteStopCmd:
    case kCmdCardChargeReq:
    case kCmdPlugChargeAuth:
    case kCmdGenericRemoteAdjust:
    case kCmdFeeModelReq:
    case kCmdFeeModelPush:
        {
            std::ostringstream oss;
            oss << "cmd=0x" << std::hex << std::uppercase << static_cast<int>(cmd);
            m_logSender.info("plat_cmd_rx", oss.str());
        }
        break;
    default:
        break;
    }

    if (cmd == kCmdRequestRsaPublicKey) {
        if (m_loginState == LOGIN_REQ_RSA_PUBLIC_KEY) {
            if (transferReason != 0x04 || !tryUpdateRsaPubKeyFromResponse(body, decBodyLen)) {
                m_loginState = LOGIN_IDLE;
                m_nextLoginAllowedTime = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                m_logSender.warn("platform_login_step", "rsa_pubkey_resp_invalid");
                return;
            }
            m_loginState = LOGIN_REQ_AUTH;
            m_lastLoginAction = std::chrono::steady_clock::now() - std::chrono::seconds(m_config.loginRetrySec);
            m_logSender.info("platform_login_step", "rsa_pubkey_resp_ok");
        }
        return;
    }

    if (cmd == kCmdDeviceAuth) {
        if (m_loginState == LOGIN_REQ_AUTH) {
            if (transferReason != 0x04) {
                m_loginState = LOGIN_IDLE;
                m_nextLoginAllowedTime = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                std::ostringstream oss;
                oss << "device_auth_rejected_reason=0x" << std::hex << std::uppercase
                    << static_cast<int>(transferReason);
                m_logSender.warn("platform_login_step", oss.str());
                return;
            }

            m_aesSessionKeyReady = true;
            m_loginState = LOGIN_REQ_FEE_MODEL;
            m_lastLoginAction = std::chrono::steady_clock::now() - std::chrono::seconds(m_config.loginRetrySec);
            if (body && decBodyLen > 0U && m_config.debugTcp) {
                std::string welcome(reinterpret_cast<const char*>(body), decBodyLen);
                std::cout << "[Comm][TCP][DEVICE_AUTH_WELCOME] " << welcome << std::endl;
            }
            m_logSender.info("platform_login_step", "device_auth_ok_fee_model_req_next");
        }
        return;
    }

    if ((cmd == kCmdFeeModelReq || cmd == kCmdFeeModelPush) &&
        (transferReason == 0x04 || transferReason == 0x01)) {
        FeeModel feeModel;
        if (!parseFeeModel(body, decBodyLen, feeModel)) {
            m_logSender.warn("platform_login_step", "fee_model_ack_parse_fail");
            return;
        }

        // BY ZF: NYC 0x73/0x74 下发后始终接收解析；每把枪按自身状态决定是否切换计费模型。
        size_t updatedCount = 0;
        size_t chargingCount = 0;
        for (size_t i = 0; i < m_feeModelByGun.size(); ++i) {
            if (i < m_gunRuntimeData.size() && m_gunRuntimeData[i].gunStatus >= 0x03) {
                ++chargingCount;
                continue;
            }
            m_feeModelByGun[i] = feeModel;
            ++updatedCount;
        }
        if (updatedCount > 0U) {
            m_logSender.saveFeeModel(feeModel);
        } else {
            m_logSender.warn("platform_login_step", "fee_model_ack_skip_all_charging");
        }

        if (m_loginState == LOGIN_REQ_FEE_MODEL) {
            m_loginState = LOGIN_ONLINE;
            if (!m_platformOnlineEventActive) {
                m_platformOnlineEventActive = true;
                publishPlatformLinkEvent(true, "fee_model_ready");
            }
            m_lastHeartbeat = std::chrono::steady_clock::now();
            m_lastHeartbeatRecv = std::chrono::steady_clock::now();
        }
        std::ostringstream oss;
        oss << "fee_model_ack_ok updated=" << updatedCount
            << " charging=" << chargingCount;
        m_logSender.info("platform_login_step", oss.str());
        return;
    }

    if (cmd == kCmdTimeSync && transferReason == 0x01) {
        // BY ZF: NYC 0x0A 对时为主站主动下发，无需确认；信息体直接为 CP56Time2a(7)。
        if (body && decBodyLen >= 7) {
            const uint8_t* t = body;
            const uint16_t ms = static_cast<uint16_t>(t[0]) |
                                (static_cast<uint16_t>(t[1]) << 8);
            const int second = (ms / 1000U) % 60U;
            const int minute = t[2] & 0x3F;
            const int hour = t[3] & 0x1F;
            const int day = t[4] & 0x1F;
            const int month = t[5] & 0x0F;
            const int year = 2000 + (t[6] & 0x7F);
            syncSystemTime(year, month, day, hour, minute, second);
            m_logSender.info("platform_time_sync", "time_sync_rx");
        }
        return;
    }

    if (cmd == kCmdHeartbeat) {
        m_lastHeartbeatRecv = std::chrono::steady_clock::now();
        return;
    }

    if (cmd == kCmdPlugChargeAuth) {
        if (!publishPlugChargeAuthResult(chargerAddr, transferReason, body, decBodyLen)) {
            std::ostringstream oss;
            oss << "gun=" << static_cast<int>(chargerAddr)
                << ",reason=0x" << std::hex << std::uppercase << static_cast<int>(transferReason);
            m_logSender.warn("platform_plug_charge_auth_ack", oss.str());
        }
        return;
    }

    if (cmd == kCmdRemoteStartCmd) {
        uint8_t gun = 0;
        uint8_t remoteControlType = (decBodyLen > 0U && body) ? body[0] : 0x01;
        uint8_t ackReason = 0x10;
        std::string ackMessage = "invalid_request";
        cJSON* startData = nullptr;
        FeeModel parsedFeeModel;
        if (!m_aesSessionKeyReady || m_loginState != LOGIN_ONLINE) {
            ackReason = 0x12;
            ackMessage = "handshake_not_ready";
        } else if (parseRemoteStart(body, decBodyLen, chargerAddr, gun, remoteControlType, &startData,
                                    parsedFeeModel, ackReason, ackMessage)) {
            m_logSender.info("platform_remote_start_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
            if (!parsedFeeModel.feeModelId.empty() && gun < m_feeModelByGun.size()) {
                m_feeModelByGun[gun] = parsedFeeModel;
            }
            if (!parsedFeeModel.feeModelId.empty()) {
                m_logSender.saveFeeModel(parsedFeeModel);
            }
            if (publishPlatCommand(gun, "start_charge", startData)) {
                ackReason = 0x04;
                ackMessage = "ok";
            } else {
                ackReason = 0x10;
                ackMessage = "mqtt_publish_fail";
            }
        }
        if (ackReason != 0x04) {
            std::ostringstream oss;
            oss << "gun=" << static_cast<int>(chargerAddr)
                << ",reason=0x" << std::hex << std::uppercase << static_cast<int>(ackReason)
                << ",msg=" << ackMessage;
            m_logSender.warn("platform_start_reject", oss.str());
        }
        const std::vector<uint8_t> ackBody = buildRemoteStartAckBody(remoteControlType, ackMessage);
        (void)sendPlatformFrameEx(kCmdRemoteStartCmd, ackBody, rxSeq, ackReason, chargerAddr, 0x01);
        if (startData) {
            cJSON_Delete(startData);
        }
        return;
    }

    if (cmd == kCmdRemoteStopCmd) {
        uint8_t stopNature = (decBodyLen > 0U && body) ? body[0] : 0x00;
        uint8_t stopReason = (decBodyLen > 1U && body) ? body[1] : 0x10;
        uint8_t ackReason = 0x04;
        std::string ackMessage = "ok";

        if (!m_aesSessionKeyReady || m_loginState != LOGIN_ONLINE) {
            ackReason = 0x12;
            ackMessage = "handshake_not_ready";
        } else if (!body || decBodyLen < 2U) {
            ackReason = 0x10;
            ackMessage = "body_too_short";
        } else if (!handleRemoteStopCommand(chargerAddr, stopReason)) {
            ackReason = 0x10;
            ackMessage = "stop_publish_fail";
        }
        const std::vector<uint8_t> ackBody = buildRemoteStopAckBody(stopNature, stopReason, ackMessage);
        (void)sendPlatformFrameEx(kCmdRemoteStopCmd, ackBody, rxSeq, ackReason, chargerAddr, 0x01);
        return;
    }

    if (cmd == kCmdUploadTradeRecord) {
        uint8_t gun = 0;
        cJSON* cfmData = nullptr;
        if (parseRecordConfirm040(body, decBodyLen, gun, &cfmData)) {
            m_logSender.info("platform_record_confirm_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
            publishPlatCommand(gun, "record_cfm", cfmData);
        }
        if (cfmData) {
            cJSON_Delete(cfmData);
        }
        return;
    }

    // BY ZF: 未识别平台帧仅保留本地调试日志，不发布MQTT。
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][RX_FRAME] ignored cmd=0x"
                  << std::hex << static_cast<int>(cmd) << std::dec << std::endl;
    }
}

bool CommProcess::parseFeeModel(const uint8_t* body, size_t bodyLen, FeeModel& feeModel)
{
    if (!body || bodyLen < 3U) {
        return false;
    }

    // BY ZF: NYC 0x73/0x74 信息体：模型编号长度1 + 模型编号N + 时段数1 + N组时段信息。
    const size_t modelIdLen = static_cast<size_t>(body[0]);
    if (modelIdLen == 0U || bodyLen < (2U + modelIdLen)) {
        return false;
    }

    const std::string modelNo(reinterpret_cast<const char*>(body + 1), modelIdLen);
    const uint8_t feeCount = body[1U + modelIdLen];
    if (feeCount == 0U || feeCount > 96U) {
        return false;
    }

    const size_t periodLen = 11U; // 标识1 + 起始时1 + 起始分1 + 电价4 + 服务费4
    const size_t needLen = 2U + modelIdLen + static_cast<size_t>(feeCount) * periodLen;
    if (bodyLen < needLen) {
        return false;
    }

    feeModel.feeModelId = modelNo.empty() ? "0000" : modelNo;
    feeModel.timeSeg.clear();
    feeModel.segFlag.clear();
    feeModel.chargeFee.clear();
    feeModel.serviceFee.clear();

    feeModel.timeSeg.reserve(feeCount);
    feeModel.segFlag.reserve(feeCount);
    feeModel.chargeFee.reserve(feeCount);
    feeModel.serviceFee.reserve(feeCount);

    size_t pos = 2U + modelIdLen;
    for (uint8_t i = 0; i < feeCount; ++i) {
        const uint8_t segFlag = body[pos];
        const uint8_t hour = body[pos + 1];
        const uint8_t minute = body[pos + 2];
        if (segFlag < 1U || segFlag > 4U || hour > 23U || minute > 59U) {
            return false;
        }

        const uint32_t chargeRateRaw = readU32LE(body + pos + 3);
        const uint32_t serviceRateRaw = readU32LE(body + pos + 7);
        char ts[8] = {0};
        std::snprintf(ts, sizeof(ts), "%02u%02u",
                      static_cast<unsigned int>(hour),
                      static_cast<unsigned int>(minute));
        feeModel.timeSeg.push_back(ts);
        feeModel.segFlag.push_back(static_cast<unsigned int>(segFlag));

        // BY ZF: NYC 协议电价/服务费分辨率为 0.0001 元，内部仍按 10^-5 元保存，避免影响现有金额链路。
        feeModel.chargeFee.push_back(chargeRateRaw * 10U);
        feeModel.serviceFee.push_back(serviceRateRaw * 10U);
        pos += periodLen;
    }

    if (feeModel.timeSeg.size() != static_cast<size_t>(feeCount) ||
        feeModel.segFlag.size() != static_cast<size_t>(feeCount) ||
        feeModel.chargeFee.size() != static_cast<size_t>(feeCount) ||
        feeModel.serviceFee.size() != static_cast<size_t>(feeCount)) {
        return false;
    }
    feeModel.timeNum = feeCount;
    return true;
}

bool CommProcess::parseRemoteStart(const uint8_t* body, size_t bodyLen, uint8_t chargerAddr, uint8_t& gun,
                                   uint8_t& remoteControlType, cJSON** outData, FeeModel& feeModel,
                                   uint8_t& rejectReason, std::string& rejectMessage)
{
    if (!body || !outData) {
        rejectReason = 0x10;
        rejectMessage = "invalid_request";
        return false;
    }
    // BY ZF: NYC 0x41 精简兼容：至少包含遥控性质1 + 启动原因1 + 停止条件1 + 停止数据4 + 卡号长度1 + 计费模型长度1。
    if (bodyLen < 9U) {
        rejectReason = 0x10;
        rejectMessage = "body_too_short";
        return false;
    }

    remoteControlType = body[0];
    if (remoteControlType != 0x01U && remoteControlType != 0x02U) {
        rejectReason = 0x10;
        rejectMessage = "invalid_remote_type";
        return false;
    }
    const uint8_t startReason = body[1];
    const uint8_t stopCondition = body[2];
    const uint32_t stopDataRaw = readU32LE(body + 3);
    (void)stopCondition;
    (void)stopDataRaw;

    if (chargerAddr == 0U) {
        rejectReason = 0x10;
        rejectMessage = "invalid_charger_addr";
        return false;
    }
    const int gunIndex = static_cast<int>(chargerAddr) - 1;
    if (gunIndex < 0 || gunIndex >= static_cast<int>(m_gunRuntimeData.size())) {
        rejectReason = 0x22;
        rejectMessage = "gun_out_of_range";
        return false;
    }
    gun = static_cast<uint8_t>(gunIndex);

    size_t pos = 7U;
    auto takeStringCompat = [body, bodyLen](size_t& offset, uint8_t declaredLen) -> std::string {
        if (offset >= bodyLen) {
            return std::string();
        }
        const size_t available = bodyLen - offset;
        const size_t actualLen = std::min<size_t>(static_cast<size_t>(declaredLen), available);
        std::string text(reinterpret_cast<const char*>(body + offset), actualLen);
        offset += actualLen;
        return text;
    };

    const uint8_t cardLen = body[pos++];
    const std::string chargeUserNo = takeStringCompat(pos, cardLen);

    const uint8_t feeModelIdLen = (pos < bodyLen) ? body[pos++] : 0U;
    const std::string reqFeeModelId = takeStringCompat(pos, feeModelIdLen);

    std::string orderNo;
    if (pos < bodyLen) {
        const uint8_t orderLen = body[pos++];
        orderNo = takeStringCompat(pos, orderLen);
    }

    if (gun < m_gunRuntimeData.size()) {
        const uint8_t gunStatus = m_gunRuntimeData[gun].gunStatus;
        if (gunStatus >= 0x03U) {
            rejectReason = 0x22;
            rejectMessage = "state_abnormal";
            return false;
        }
    }

    if (gun < m_feeModelByGun.size()) {
        feeModel = m_feeModelByGun[gun];
    } else {
        feeModel = FeeModel();
    }
    const bool feeModelReady =
            (!feeModel.feeModelId.empty()) &&
            (feeModel.timeNum > 0) &&
            (feeModel.timeSeg.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.chargeFee.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.serviceFee.size() >= static_cast<size_t>(feeModel.timeNum));
    if (!feeModelReady) {
        rejectReason = 0x23;
        rejectMessage = "fee_model_not_ready";
        if (gun < m_gunRuntimeData.size()) {
            m_gunRuntimeData[gun].orderNo = orderNo;
        }
        return false;
    }
    if (reqFeeModelId.empty()) {
        rejectReason = 0x23;
        rejectMessage = "fee_model_missing";
        return false;
    }
    if (!reqFeeModelId.empty() && reqFeeModelId != feeModel.feeModelId) {
        rejectReason = 0x23;
        rejectMessage = "fee_model_unsynced";
        return false;
    }

    auto addFeeModelToJson = [&feeModel](cJSON* data) {
        cJSON_AddNumberToObject(data, "timeNum", static_cast<int>(feeModel.timeNum));
        cJSON* timeSeg = cJSON_CreateArray();
        cJSON* chargeFee = cJSON_CreateArray();
        cJSON* serviceFee = cJSON_CreateArray();
        for (size_t i = 0; i < feeModel.timeSeg.size(); ++i) {
            cJSON_AddItemToArray(timeSeg, cJSON_CreateString(feeModel.timeSeg[i].c_str()));
        }
        for (size_t i = 0; i < feeModel.chargeFee.size(); ++i) {
            cJSON_AddItemToArray(chargeFee, cJSON_CreateNumber(static_cast<double>(feeModel.chargeFee[i]) / 100000.0));
        }
        for (size_t i = 0; i < feeModel.serviceFee.size(); ++i) {
            cJSON_AddItemToArray(serviceFee, cJSON_CreateNumber(static_cast<double>(feeModel.serviceFee[i]) / 100000.0));
        }
        cJSON_AddItemToObject(data, "timeSeg", timeSeg);
        cJSON_AddItemToObject(data, "chargeFee", chargeFee);
        cJSON_AddItemToObject(data, "serviceFee", serviceFee);
    };
    auto parseFeeModelNo = [](const std::string& feeModelId) -> int {
        std::string digits;
        digits.reserve(feeModelId.size());
        for (size_t i = 0; i < feeModelId.size(); ++i) {
            if (feeModelId[i] >= '0' && feeModelId[i] <= '9') {
                digits.push_back(feeModelId[i]);
            }
        }
        if (digits.empty()) {
            return 0;
        }
        return std::atoi(digits.c_str());
    };
    const bool isPlugAndCharge = (startReason == 0x02U);
    const double prechargeAmount = 10000.0;

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "startTime", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddStringToObject(data, "chargeUserNo", chargeUserNo.c_str());
    if (!orderNo.empty()) {
        cJSON_AddStringToObject(data, "orderNo", orderNo.c_str());
        cJSON_AddStringToObject(data, "preTradeNo", orderNo.c_str());
        cJSON_AddStringToObject(data, "tradeNo", orderNo.c_str());
    }
    cJSON_AddNumberToObject(data, "chargeMode", 0x60);
    cJSON_AddNumberToObject(data, "prechargeAmount", prechargeAmount);
    cJSON_AddNumberToObject(data, "feeModelNo", parseFeeModelNo(feeModel.feeModelId));
    cJSON_AddStringToObject(data, "feeModelId", feeModel.feeModelId.c_str());
    cJSON_AddNumberToObject(data, "plugAndChargeFlag", isPlugAndCharge ? 0x02 : 0x01);
    cJSON_AddNumberToObject(data, "mergeChargeFlag", 0x00);
    cJSON_AddNumberToObject(data, "v2g", (remoteControlType == 0x02U) ? 0x01 : 0x00);
    addFeeModelToJson(data);

    if (gun < m_gunRuntimeData.size()) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        const std::time_t nowSec = std::time(nullptr);
        std::tm* tmv = std::localtime(&nowSec);
        const int year = tmv ? (tmv->tm_year + 1900) : 2026;
        rd.startTimeBcd[0] = bcdByte((year / 100) % 100);
        rd.startTimeBcd[1] = bcdByte(year % 100);
        rd.startTimeBcd[2] = bcdByte((tmv ? tmv->tm_mon + 1 : 1));
        rd.startTimeBcd[3] = bcdByte((tmv ? tmv->tm_mday : 1));
        rd.startTimeBcd[4] = bcdByte((tmv ? tmv->tm_hour : 0));
        rd.startTimeBcd[5] = bcdByte((tmv ? tmv->tm_min : 0));
        rd.startTimeBcd[6] = bcdByte((tmv ? tmv->tm_sec : 0));
        rd.startTimeBcd[7] = 0xFF;

        rd.chargeUserNo = chargeUserNo;
        rd.orderNo = orderNo;
        rd.chargeMode = 0x60;
        rd.prechargeAmount = prechargeAmount;
        rd.userStatus = 0;
        rd.billingFlag = 0;
        rd.remoteControlType = remoteControlType;
        rd.remoteStartReason = startReason;
        rd.remoteStopCondition = 0;
        rd.remoteStopData = 0.0;
        rd.feeModelId = feeModel.feeModelId;
        rd.feeTimeNum = static_cast<int>(feeModel.timeNum);
        rd.feeSegments.clear();
        const size_t periodCount = feeModel.timeSeg.size();
        rd.feeSegments.reserve(periodCount);
        for (size_t i = 0; i < periodCount; ++i) {
            FeeSegmentData seg;
            seg.startTs = feeModel.timeSeg[i];
            seg.endTs = (i + 1 < periodCount) ? feeModel.timeSeg[i + 1] : "2400";
            seg.energyKwh = 0.0;
            seg.electricAmount = 0.0;
            seg.serviceAmount = 0.0;
            rd.feeSegments.push_back(seg);
        }
    }

    rejectReason = 0x04;
    rejectMessage = "ok";
    *outData = data;
    return true;
}

bool CommProcess::publishPlugChargeAuthResult(uint8_t chargerAddr, uint8_t transferReason,
                                              const uint8_t* body, size_t bodyLen)
{
    if (chargerAddr == 0U) {
        return false;
    }

    const uint8_t gun = static_cast<uint8_t>(chargerAddr - 1U);
    if (gun >= m_config.gunCount || gun >= m_gunRuntimeData.size()) {
        return false;
    }

    const bool authSuccess = (transferReason == 0x04U);
    std::string authMessage;
    if (body && bodyLen >= 2U) {
        size_t msgLen = static_cast<size_t>(body[1]);
        if (msgLen > bodyLen - 2U) {
            msgLen = bodyLen - 2U;
        }
        if (msgLen > 0U) {
            authMessage.assign(reinterpret_cast<const char*>(body + 2U), msgLen);
        }
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "vin", m_gunRuntimeData[gun].pendingVinAuthVin.c_str());
    cJSON_AddStringToObject(data, "vinCode", m_gunRuntimeData[gun].pendingVinAuthVin.c_str());
    cJSON_AddNumberToObject(data, "result", authSuccess ? 0x01 : 0x00);
    cJSON_AddNumberToObject(data, "successFlag", authSuccess ? 0x00 : 0x01);
    cJSON_AddNumberToObject(data, "failReason", authSuccess ? 0x00 : transferReason);
    if (!authMessage.empty()) {
        cJSON_AddStringToObject(data, "message", authMessage.c_str());
    }

    const bool publishOk = publishPlatCommand(gun, "plug_and_charge_auth_result", data);
    cJSON_Delete(data);
    if (publishOk) {
        std::ostringstream oss;
        oss << "gun=" << static_cast<int>(gun)
            << ",reason=0x" << std::hex << std::uppercase << static_cast<int>(transferReason);
        m_logSender.info("platform_plug_charge_auth_ack_rx", oss.str());
    }
    return publishOk;
}

bool CommProcess::handleRemoteStopCommand(uint8_t chargerAddr, uint8_t stopReason)
{
    cJSON* stopData = cJSON_CreateObject();
    if (!stopData) {
        return false;
    }
    cJSON_AddNumberToObject(stopData, "stopReason", stopReason);
    cJSON_AddNumberToObject(stopData, "tcuStopCode", 0);

    bool publishOk = true;
    if (chargerAddr == 0xFFU) {
        for (size_t i = 0; i < m_config.gunCount; ++i) {
            if (!publishPlatCommand(static_cast<uint8_t>(i), "stop_charge", stopData)) {
                publishOk = false;
            }
        }
        m_logSender.info("platform_remote_stop_rx", "gun=all");
    } else if (chargerAddr > 0U) {
        const uint8_t gun = static_cast<uint8_t>(chargerAddr - 1U);
        if (gun < m_config.gunCount) {
            publishOk = publishPlatCommand(gun, "stop_charge", stopData);
            m_logSender.info("platform_remote_stop_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
        } else {
            publishOk = false;
        }
    } else {
        publishOk = false;
    }

    cJSON_Delete(stopData);
    return publishOk;
}

bool CommProcess::parseRecordConfirm040(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData)
{
    if (!body || !outData) {
        return false;
    }
    // BY ZF: 0x40 最小长度：交易流水号BCD16 + 确认结果1
    if (bodyLen < 17) {
        return false;
    }

    const std::string tradeNo = bcdToDigitString(body, 16);
    const uint8_t feedbackResult = body[16];
    const bool ackOk = (feedbackResult == 0x00);

    // BY ZF: 协议无枪号，按 tradeNo 在待确认缓存中反查所属枪。
    int gunIndex = 0;
    bool foundGun = false;
    for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
        if (m_gunRuntimeData[i].pendingRecordTradeNo == tradeNo) {
            gunIndex = static_cast<int>(i);
            foundGun = true;
            break;
        }
    }
    // BY ZF: 若未命中缓存，兜底归到0号枪，避免丢失确认消息。
    (void)foundGun;
    gun = static_cast<uint8_t>(gunIndex);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "tradeNo", tradeNo.c_str());
    cJSON_AddNumberToObject(data, "confirmFlag", ackOk ? 1 : 0);
    cJSON_AddNumberToObject(data, "result", feedbackResult);
    *outData = data;

    // BY ZF: 直接完成交易记录确认（0x00=上传成功）。
    if (ackOk && !tradeNo.empty()) {
        m_logSender.confirmTradeRecord(tradeNo, 1);
    }

    // BY ZF: 成功应答后清理缓存，避免后续回包误关联旧记录。
    if (ackOk && foundGun && gun < m_gunRuntimeData.size()) {
        m_gunRuntimeData[gun].pendingRecordTradeNo.clear();
    }
    return true;
}

bool CommProcess::publishPlatCommand(uint8_t gun, const char* cmd, cJSON* dataObj)
{
    if (!cmd || !dataObj) {
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_comm");
    cJSON_AddNumberToObject(root, "gun", gun);
    cJSON_AddStringToObject(root, "cmd", cmd);
    cJSON_AddItemToObject(root, "data", cJSON_Duplicate(dataObj, 1));

    char* out = cJSON_PrintUnformatted(root);
    std::string payload;
    if (out) {
        payload = out;
        free(out);
    }
    cJSON_Delete(root);
    if (payload.empty()) {
        return false;
    }
    return publishPlatCmd(gun, payload);
}

bool CommProcess::publishSetConfig(uint8_t gun, cJSON* dataObj)
{
    if (!dataObj) {
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
    cJSON_AddStringToObject(root, "source", "tcu_comm");
    cJSON_AddNumberToObject(root, "gun", gun);
    cJSON_AddStringToObject(root, "type", "setConfig");
    cJSON* dataCopy = cJSON_Duplicate(dataObj, 1);
    if (!dataCopy) {
        cJSON_Delete(root);
        return false;
    }
    cJSON_AddItemToObject(root, "data", dataCopy);

    char* out = cJSON_PrintUnformatted(root);
    std::string payload;
    if (out) {
        payload = out;
        free(out);
    }
    cJSON_Delete(root);
    if (payload.empty()) {
        return false;
    }
    if (gun < m_lastSetConfigPayloadByGun.size() && gun < m_lastSetConfigPublishByGun.size()) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const bool samePayload = (m_lastSetConfigPayloadByGun[gun] == payload);
        if (samePayload && (now - m_lastSetConfigPublishByGun[gun]) < std::chrono::seconds(10)) {
            return true;
        }
        m_lastSetConfigPayloadByGun[gun] = payload;
        m_lastSetConfigPublishByGun[gun] = now;
    }
    const std::string outTopic = buildTopic("plat", gun, "event");
    return m_mqtt.publish(outTopic, payload, 1, true);
}

void CommProcess::publishPlatformLinkEvent(bool online, const char* reason)
{
    // BY ZF: 离线运行模式下，统一对外上报平台通信正常，便于脱网联调。
    if (m_config.offlineRunMode) {
        online = true;
        reason = "offline_mode";
        m_platformOnlineEventActive = true;
    }
    for (uint8_t gun = 0; gun < m_config.gunCount; ++gun) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "ts", static_cast<double>(std::time(nullptr)) * 1000.0);
        cJSON_AddNumberToObject(root, "seq", static_cast<double>(++m_seq));
        cJSON_AddStringToObject(root, "source", "tcu_comm");
        cJSON_AddNumberToObject(root, "gun", gun);
        cJSON_AddStringToObject(root, "event", online ? "platform_online" : "platform_offline");

        cJSON* data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "reason", reason ? reason : "");
        cJSON_AddItemToObject(root, "data", data);

        char* out = cJSON_PrintUnformatted(root);
        std::string payload;
        if (out) {
            payload = out;
            free(out);
        }
        cJSON_Delete(root);
        if (payload.empty()) {
            continue;
        }
        const std::string outTopic = buildTopic("plat", gun, "event");
        m_mqtt.publish(outTopic, payload, 1, true);
    }
}

bool CommProcess::persistSm2PubKeyToIni(const std::string& pubKey)
{
    if (pubKey.empty()) {
        return false;
    }

    ConfigManagerLite& cfg = getConfig();
    const std::string section = "Comm";
    cfg.setString(section, "sm2_public_key", pubKey);
    // BY ZF: 平台在0x02下发的新SM2公钥需要固化，避免重启后继续使用旧公钥。
    return cfg.saveConfig("/usr/app/config/tcu_comm.ini");
}

bool CommProcess::persistRsaPubKeyToIni(const std::string& pubKey)
{
    if (pubKey.empty()) {
        return false;
    }

    ConfigManagerLite& cfg = getConfig();
    const std::string section = "Comm";
    cfg.setString(section, "rsa_public_key", pubKey);
    // BY ZF: 01H返回的主站RSA公钥固化，避免重启后缺少认证公钥。
    return cfg.saveConfig("/usr/app/config/tcu_comm.ini");
}

void CommProcess::publishInitialSetConfig()
{
    const uint8_t gunCount = static_cast<uint8_t>(m_config.gunIdList.size());
    for (uint8_t gun = 0; gun < gunCount; ++gun) {
        cJSON* data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "gunId", static_cast<double>(m_config.gunIdList[gun]));
        cJSON_AddNumberToObject(data, "gunNo", static_cast<int>(gun) + 1);
        cJSON_AddStringToObject(data, "cdzNo", m_config.cdzNo.c_str());
        cJSON_AddStringToObject(data, "factoryCreditCode", m_config.factoryCreditCode.c_str());
        cJSON_AddStringToObject(data, "macAddr", m_config.macAddr.c_str());
        const std::string qr = (gun < m_config.gunQrCodeList.size()) ? m_config.gunQrCodeList[gun] : std::string();
        cJSON_AddStringToObject(data, "qrCode", qr.c_str());

        if (!publishSetConfig(gun, data)) {
            m_logSender.warn("platform_setconfig_bootstrap", "publish_fail");
        }
        cJSON_Delete(data);
    }
}

bool CommProcess::isHexString(const std::string& s, size_t needLen) const
{
    if (s.size() != needLen) {
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}
