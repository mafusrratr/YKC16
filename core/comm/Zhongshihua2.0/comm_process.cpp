/**
 * tcu_comm process implementation
 * BY ZF
 */

#include "comm_process.h"
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
    // BY ZF: 中石化2.0平台帧类型（表4-1，固定值，不走配置）。
    //登陆流程相关帧定义
    static const uint8_t kCmdLoginReq = 0x01;
    static const uint8_t kCmdLoginAck = 0x02;
    static const uint8_t kCmdHeartbeat = 0x03;
    static const uint8_t kCmdHeartbeatAck = 0x04;
    static const uint8_t kCmdFeeModelReq = 0x09;
    static const uint8_t kCmdFeeModelAck = 0x0A;
    static const uint8_t kCmdTimeSyncReq = 0x0B;
    static const uint8_t kCmdTimeSyncAck = 0x0C;
    static const uint8_t kCmdGunFeeModelReq = 0x0D;

    //实时数据帧定义
    static const uint8_t kCmdStartChargeResult = 0x2D;
    static const uint8_t kCmdChargeInfo = 0x13;


    // BY ZF: 启动/并充/停止/交易相关帧定义（先定义，后续逐步接入业务处理）。
    static const uint8_t kCmdMergeChargeApply = 0xA1;
    static const uint8_t kCmdMergeChargeApplyAck = 0xA2;
    static const uint8_t kCmdMergeStartReply = 0xA3;
    static const uint8_t kCmdRemoteMergeStart = 0xA4;
    
    static const uint8_t kCmdStartApply = 0xA5;
    static const uint8_t kCmdStartApplyAck = 0xA6;
    static const uint8_t kCmdRemoteStartAck = 0xA7;
    static const uint8_t kCmdRemoteStartCmd = 0xA8;
    //停止命令
    static const uint8_t kCmdRemoteStopAck = 0x35;
    static const uint8_t kCmdRemoteStopCmd = 0x36;
    //充电记录
    static const uint8_t kCmdUploadTradeRecord = 0x3D;
    static const uint8_t kCmdRecordConfirm = 0x40;

    //固定充电桩/枪类型
    static const uint8_t kFixedChargerType = 0x01;
    static const uint8_t kFixedGunType = 0x01;

    // BY ZF: 登录秘钥(8字节ASCII)生成（优先使用loginId，回退到cdzNo）。
    // BY ZF: 允许字母+数字，不再仅保留数字。
    static std::string deriveLoginSecret8(const std::string& seedId)
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
    static std::string sm2EncryptToAscii(const std::vector<uint8_t>& plain,
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

    static bool sm4CbcEncryptPkcs7(const uint8_t key[16], const std::vector<uint8_t>& plain, std::vector<uint8_t>& out)
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

    static bool sm4CbcDecryptPkcs7(const uint8_t key[16], const uint8_t* in, size_t inLen, std::vector<uint8_t>& out)
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

    // BY ZF: 按帧类型判断消息体是否需要SM4加密。
    // BY ZF: 协议约定以下帧消息体不加密：0x01/0x03/0x04/0x0B/0x0C。
    static bool shouldEncryptBody(uint8_t cmd)
    {
        switch (cmd) {
        case 0x01: // 登录认证
        case 0x03: // 心跳请求
        case 0x04: // 心跳应答
        case 0x0B: // 对时请求
        case 0x0C: // 对时应答
            return false;
        default:
            return true;
        }
    }

    static bool encryptBodyByCmd(uint8_t cmd,
                                 const std::vector<uint8_t>& plain,
                                 const std::array<uint8_t, 16>& key,
                                 bool keyReady,
                                 std::vector<uint8_t>& out,
                                 uint8_t& encryptFlag)
    {
        if (!shouldEncryptBody(cmd)) {
            encryptFlag = 0x00;
            out = plain;
            return true;
        }
        if (!keyReady) {
            return false;
        }
        encryptFlag = 0x01;
        return sm4CbcEncryptPkcs7(key.data(), plain, out);
    }

    static bool decryptBodyByFlag(uint8_t encryptFlag,
                                  const uint8_t* body,
                                  size_t bodyLen,
                                  const std::array<uint8_t, 16>& key,
                                  bool keyReady,
                                  std::vector<uint8_t>& out)
    {
        if (!body || bodyLen == 0U) {
            out.clear();
            return true;
        }
        if (encryptFlag == 0x00) {
            out.assign(body, body + bodyLen);
            return true;
        }
        if (encryptFlag != 0x01 || !keyReady) {
            return false;
        }
        return sm4CbcDecryptPkcs7(key.data(), body, bodyLen, out);
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

    // BY ZF: YYYYMMDDhhmmss 转 8 字节 BCD（末字节固定 0xFF）。
    static void ymdHmsToBcd8(uint64_t ymdhms, uint8_t out[8])
    {
        std::memset(out, 0, 8);
        char buf[15] = {0};
        std::snprintf(buf, sizeof(buf), "%014llu", static_cast<unsigned long long>(ymdhms));
        auto bcd2 = [](char a, char b) -> uint8_t {
            const uint8_t hi = (a >= '0' && a <= '9') ? static_cast<uint8_t>(a - '0') : 0;
            const uint8_t lo = (b >= '0' && b <= '9') ? static_cast<uint8_t>(b - '0') : 0;
            return static_cast<uint8_t>((hi << 4) | lo);
        };
        out[0] = bcd2(buf[0], buf[1]);
        out[1] = bcd2(buf[2], buf[3]);
        out[2] = bcd2(buf[4], buf[5]);
        out[3] = bcd2(buf[6], buf[7]);
        out[4] = bcd2(buf[8], buf[9]);
        out[5] = bcd2(buf[10], buf[11]);
        out[6] = bcd2(buf[12], buf[13]);
        out[7] = 0xFF;
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

    static void appendU64LE(std::vector<uint8_t>& out, uint64_t v)
    {
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    // BY ZF: YYYYMMDDhhmmss 转 CP56Time2a(7)；毫秒未知时按整秒。
    static void appendCp56Time2aFromYmdHms(std::vector<uint8_t>& out, uint64_t ymdhms)
    {
        char buf[15] = {0};
        std::snprintf(buf, sizeof(buf), "%014llu", static_cast<unsigned long long>(ymdhms));
        auto d2 = [](char a, char b) -> int {
            if (a < '0' || a > '9' || b < '0' || b > '9') return 0;
            return (a - '0') * 10 + (b - '0');
        };
        const int year = d2(buf[2], buf[3]);    // YY
        const int month = d2(buf[4], buf[5]);
        const int day = d2(buf[6], buf[7]);
        const int hour = d2(buf[8], buf[9]);
        const int minute = d2(buf[10], buf[11]);
        const int second = d2(buf[12], buf[13]);
        const uint16_t ms = static_cast<uint16_t>((second < 0 ? 0 : second) * 1000);
        appendU16LE(out, ms);
        out.push_back(static_cast<uint8_t>(minute & 0x3F));
        out.push_back(static_cast<uint8_t>(hour & 0x1F));
        out.push_back(static_cast<uint8_t>(day & 0x1F));
        out.push_back(static_cast<uint8_t>(month & 0x0F));
        out.push_back(static_cast<uint8_t>(year & 0x7F));
    }

    // BY ZF: 平台命令字转名称，便于TCP调试输出。
    static const char* platformCmdName(uint8_t cmd)
    {
        switch (cmd) {
        case 0x01: return "login_req";
        case 0x02: return "login_ack";
        case 0x03: return "heartbeat";
        case 0x04: return "heartbeat_ack";
        case 0x09: return "fee_model_req";
        case 0x0A: return "fee_model_ack";
        case 0x0B: return "time_sync_req";
        case 0x0C: return "time_sync_ack";
        case 0x0D: return "gun_fee_model_req";
        case 0x13: return "charge_info";
        case 0x2D: return "start_result";
        case 0x3D: return "upload_trade_record";
        case 0x40: return "record_confirm";
        case 0xA1: return "merge_charge_apply";
        case 0xA2: return "merge_charge_apply_ack";
        case 0xA3: return "merge_start_reply";
        case 0xA4: return "remote_merge_start";
        case 0xA5: return "start_apply";
        case 0xA6: return "start_apply_ack";
        case 0xA7: return "remote_start_ack";
        case 0xA8: return "remote_start_cmd";
        case 0x35: return "remote_stop_ack";
        case 0x36: return "remote_stop_cmd";
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
    , m_heartbeatCounter(0)
    , m_sm4SessionKeyReady(false)
    , m_loginCryptoPrepared(false)
{
    m_sm4SessionKey.fill(0);
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
    if (!initMqtt()) {
        return false;
    }
    m_lastChargeInfoReportByGun.assign(static_cast<size_t>(m_config.gunCount), std::chrono::steady_clock::now());
    m_runtimeChangedByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_logSender.info("init_completed", std::string("gun_count=") + std::to_string(m_config.gunCount));
    return true;
}

void CommProcess::doRun()
{
    m_running = true;
    // BY ZF: 喂狗频率控制为 5 秒一次。
    auto lastFeedTime = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    while (m_running.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastFeedTime >= std::chrono::seconds(5)) {
            feedWatchdog();
            lastFeedTime = now;
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
    m_config.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_config.mqttPassword = cfg.getString(section, "mqtt_password", "");
    m_config.masterHost = cfg.getString(section, "master_host", "127.0.0.1");
    m_config.masterPort = cfg.getInt(section, "master_port", 9000);
    m_config.cdzNo = cfg.getString(section, "cdz_no", "CDZ000001");
    m_config.loginId = cfg.getString(section, "login_id", "");
    m_config.macAddr = cfg.getString(section, "mac_addr", "");
    m_config.factoryCreditCode = cfg.getString(section, "factory_credit_code", "");
    m_config.sm2PublicKey = cfg.getString(section, "sm2_public_key", "");
    m_config.tcpReconnectSec = cfg.getInt(section, "tcp_reconnect_sec", 3);
    // BY ZF: 按中石化2.0联调要求，心跳周期固定20秒。
    m_config.tcpHeartbeatSec = 20;
    m_config.loginRetrySec = cfg.getInt(section, "login_retry_sec", 10);
    m_config.debugTcp = (cfg.getInt(section, "debug", 0) != 0);

    m_config.gunIdList.clear();
    m_config.gunTypeList.clear();
    m_gunRuntimeData.clear();
    m_feeModelByGun.clear();
    m_config.gunIdList.reserve(m_config.gunCount);
    m_config.gunTypeList.reserve(m_config.gunCount);
    m_gunRuntimeData.reserve(m_config.gunCount);
    m_feeModelByGun.reserve(m_config.gunCount);
    m_config.chargerType = kFixedChargerType;


    for (uint8_t i = 0; i < m_config.gunCount; ++i) {
        std::ostringstream idKey;
        idKey << "gun" << static_cast<int>(i + 1) << "_id";

        const std::string idText = cfg.getString(section, idKey.str(), "0xFFFFFFFF");
        const uint32_t gunId = static_cast<uint32_t>(std::strtoul(idText.c_str(), nullptr, 0));
        m_config.gunIdList.push_back(gunId);
        m_config.gunTypeList.push_back(kFixedGunType);
        m_gunRuntimeData.push_back(GunRuntimeData());
        m_feeModelByGun.push_back(FeeModel());
    }

    if (!m_config.macAddr.empty() && !isHexString(m_config.macAddr, 24)) {
        m_logSender.warn("invalid_mac_addr", m_config.macAddr);
    }
    if (!m_config.factoryCreditCode.empty() && m_config.factoryCreditCode.size() > 32) {
        m_logSender.warn("factory_credit_code_too_long", m_config.factoryCreditCode);
    }
    if (m_config.sm2PublicKey.empty()) {
        m_logSender.warn("sm2_public_key_empty", "sm2_public_key not configured");
    }
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
    gun = static_cast<uint8_t>(std::atoi(seg[2].c_str()));
    leaf = seg[3];
    return true;
}

bool CommProcess::publishPlatCmd(uint8_t gun, const std::string& payload)
{
    const std::string outTopic = buildTopic("plat", gun, "cmd");
    return m_mqtt.publish(outTopic, ensureGunField(payload, gun), 1, false);
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
                    m_gunRuntimeData[gun].gunStatus = mappedStatus;
                }
            }
            // BY ZF: 充电记录上报事件 -> 平台0x3D上传交易记录。
            if (cJSON_IsString(evt) &&
                std::strcmp(evt->valuestring, "update_record") == 0 &&
                cJSON_IsObject(data)) {
                std::vector<uint8_t> body;
                if (buildChargeRecordBodyFromUpdateRecord(gun, data, body) && !body.empty()) {
                    sendPlatformFrame(kCmdUploadTradeRecord, body);
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

                
                updateU8("workStatus", rd.yxWorkStatus);
                updateU8("totalFault", rd.yxTotalFault);
                updateU8("totalAlarm", rd.yxTotalAlarm);
                updateU8("emergencyStopFault", rd.yxEmergencyStopFault);
                updateU8("vehicleConnectStatus", rd.yxVehicleConnectStatus);
                updateU8("vinReq", rd.yxVinReq);
                updateU8("gunSeatStatus", rd.yxGunSeatStatus);
                updateU8("electronicLockStatus", rd.yxElectronicLockStatus);
                updateU8("dcContactorStatus", rd.yxDcContactorStatus);
                updateU8("otherFault", rd.yxOtherFault);
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
        int successFlag = 1;
        cJSON* s = cJSON_GetObjectItem(data, "successFlag");
        if (s && cJSON_IsNumber(s)) {
            successFlag = s->valueint;
        }

        int failReason = 0;
        // cJSON* r = cJSON_GetObjectItem(data, "chargeFailReason");
        // if (r && cJSON_IsNumber(r)) {
        //     failReason = r->valueint;
        // }
        // if (failReason < 0) {
        //     failReason = 0;
        // }
        // if (failReason > 255) {
        //     failReason = 255;
        // }

        std::vector<uint8_t> body;
        body.reserve(32);
        // BY ZF: 1) 交易流水号 BCD16，优先使用启动阶段缓存的订单号。
        appendBcdFixed(body, m_gunRuntimeData[gun].orderNo, 16);
        // BY ZF: 2) 桩编号 BCD7。
        appendPileCodeBcd7(body, m_config.cdzNo);
        // BY ZF: 3) 枪号 BCD1（按1..n上送）。
        const int gunNo = static_cast<int>(gun) + 1;
        body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
        // BY ZF: 4) 启动结果 BCD1：0x02成功，0x01失败。
        body.push_back(static_cast<uint8_t>(successFlag == 0 ? 0x02 : 0x01));
        // BY ZF: 5) 失败原因 BIN1（成功时填0x00）。
        body.push_back(static_cast<uint8_t>(successFlag == 0 ? 0x00 : failReason));

        (void)sendPlatformFrame(kCmdStartChargeResult, body);
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
    const std::string cardNumber = jsonGetString(data, "cardNumber");
    int startType = 1;
    (void)jsonGetInt(data, "startType", startType);
    const double totalElect = [&](){ double v = 0.0; jsonGetNumber(data, "totalElect", v); return v; }();
    const double totalPowerCost = [&](){ double v = 0.0; jsonGetNumber(data, "totalPowerCost", v); return v; }();
    const double totalCost = [&](){ double v = 0.0; jsonGetNumber(data, "totalCost", v); return v; }();
    const double totalServCost = [&](){ double v = 0.0; jsonGetNumber(data, "totalServCost", v); return v; }();
    const double sumStart = [&](){ double v = 0.0; jsonGetNumber(data, "sumStart", v); return v; }();
    const double sumEnd = [&](){ double v = 0.0; jsonGetNumber(data, "sumEnd", v); return v; }();
    const int reason = [&](){ int v = 0; jsonGetInt(data, "reason", v); return v; }();
    const uint64_t startTime = [&](){ cJSON* n = cJSON_GetObjectItem(data, "chargeStartTime"); return (n && cJSON_IsNumber(n)) ? static_cast<uint64_t>(n->valuedouble) : 0ULL; }();
    const uint64_t endTime = [&](){ cJSON* n = cJSON_GetObjectItem(data, "chargeEndTime"); return (n && cJSON_IsNumber(n)) ? static_cast<uint64_t>(n->valuedouble) : 0ULL; }();

    // BY ZF: 0x3D 上传交易记录（按中石化2.0字段顺序组帧）。
    // 1) 交易流水号 BCD16
    appendBcdFixed(body, tradeNo, 16);
    // 2) 桩编号 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1（1..n）
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    // 4) 开始时间 CP56Time2a 7字节
    appendCp56Time2aFromYmdHms(body, startTime);
    // 5) 结束时间 CP56Time2a 7字节
    appendCp56Time2aFromYmdHms(body, endTime);
    // 6) 电表表号 BCD6（无则补0）
    body.insert(body.end(), 6, 0x00);
    // 7) 电表协议版本 BIN2（无则补0）
    appendU16LE(body, 0U);
    // 8) 启动电表值 BIN8（精确到1e-4）
    appendU64LE(body, static_cast<uint64_t>(scaleToU32(sumStart, 10000.0)));
    // 9) 结束电表值 BIN8（精确到1e-4）
    appendU64LE(body, static_cast<uint64_t>(scaleToU32(sumEnd, 10000.0)));
    // 10) 总电量 BIN4（精确到1e-4）
    appendU32LE(body, scaleToU32(totalElect, 10000.0));
    // 11) 计损总电量 BIN4（按要求与总电量一致）
    appendU32LE(body, scaleToU32(totalElect, 10000.0));
    // 12) 清算金额 BIN4（精确到1e-4，包含电费+服务费）
    appendU32LE(body, scaleToU32(totalCost, 10000.0));
    // 13) VIN ASCII17（不足补'.'）
    std::string vin = vinCode;
    if (vin.size() > 17U) vin = vin.substr(0, 17U);
    if (vin.size() < 17U) vin.append(17U - vin.size(), '.');
    body.insert(body.end(), vin.begin(), vin.end());
    // 14) 交易标识 BIN1
    uint8_t tradeFlag = 0x01; // app启动
    if (startType == 2) tradeFlag = 0x02;      // 卡启动
    else if (startType == 4) tradeFlag = 0x04; // 离线卡启动
    else if (startType == 5) tradeFlag = 0x05; // VIN码启动
    body.push_back(tradeFlag);
    // 15) 交易时间 CP56Time2a 7字节（取结束时间）
    appendCp56Time2aFromYmdHms(body, endTime);
    // 16) 停止原因 BIN2
    appendU16LE(body, static_cast<uint16_t>(reason < 0 ? 0 : reason));
    // 17) 物理卡号 BIN8（不足补0，优先十六进制串）
    std::string hexDigits;
    for (size_t i = 0; i < cardNumber.size(); ++i) {
        const char c = cardNumber[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
            hexDigits.push_back(c);
        }
    }
    if (hexDigits.size() % 2U != 0U) {
        hexDigits.insert(hexDigits.begin(), '0');
    }
    if (hexDigits.size() > 16U) {
        hexDigits = hexDigits.substr(0, 16U);
    }
    while (hexDigits.size() < 16U) {
        hexDigits.push_back('0');
    }
    for (size_t i = 0; i < 16U; i += 2U) {
        const std::string b = hexDigits.substr(i, 2U);
        body.push_back(static_cast<uint8_t>(std::strtoul(b.c_str(), nullptr, 16)));
    }

    // BY ZF: 缓存本次上送的交易号，供0x40应答使用（协议不带订单号时回填）。
    if (gun < m_gunRuntimeData.size()) {
        m_gunRuntimeData[gun].pendingRecordTradeNo = tradeNo;
    }

    // 18~: 费率时段明细。
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
            localFeeModel.segFlag.size() >= static_cast<size_t>(periodCount)) {
            matchedFeeModel = &localFeeModel;
        }
    }

    // BY ZF: 统一封装单条费率明细，避免重复拼包逻辑。
    auto appendRateDetail = [&body](uint8_t rateNo,
                                    uint32_t chargePrice,
                                    uint32_t servicePrice,
                                    uint32_t energy,
                                    uint32_t chargeFeeVal,
                                    uint32_t serviceFeeVal) {
        const uint32_t lossEnergy = energy; // BY ZF: 计损电量与电量一致

        body.push_back(rateNo);          // N费率时段号
        appendU32LE(body, chargePrice);  // N费率电费单价(1e-5元)
        appendU32LE(body, servicePrice); // N费率服务费单价(1e-5元)
        appendU32LE(body, energy);       // N费率电量(1e-4kWh)
        appendU32LE(body, lossEnergy);   // N费率计损电量(1e-4kWh)
        appendU32LE(body, chargeFeeVal); // N费率电费(1e-4元)
        appendU32LE(body, serviceFeeVal);// N费率服务费(1e-4元)
    };

    if (matchedFeeModel) {
        // BY ZF: 计费模型一致时，按 segFlag 聚合后再上送（费率号维度）。
        std::vector<uint64_t> energyByRate(256, 0ULL);
        std::vector<uint64_t> chargeByRate(256, 0ULL);
        std::vector<uint64_t> serviceByRate(256, 0ULL);
        std::vector<uint32_t> chargePriceByRate(256, 0U);
        std::vector<uint32_t> servicePriceByRate(256, 0U);
        std::vector<uint8_t> rateSeen(256, 0U);
        uint8_t maxRateNo = 0;

        for (int i = 0; i < periodCount; ++i) {
            double periodChargeFee = 0.0;
            double periodServiceFee = 0.0;
            double periodEnergy = 0.0;
            if (cJSON_IsArray(chargeFeeArr)) {
                cJSON* n = cJSON_GetArrayItem(chargeFeeArr, i);
                if (n && cJSON_IsNumber(n)) periodChargeFee = n->valuedouble;
            }
            if (cJSON_IsArray(serviceFeeArr)) {
                cJSON* n = cJSON_GetArrayItem(serviceFeeArr, i);
                if (n && cJSON_IsNumber(n)) periodServiceFee = n->valuedouble;
            }
            if (cJSON_IsArray(partElectArr)) {
                cJSON* n = cJSON_GetArrayItem(partElectArr, i);
                if (n && cJSON_IsNumber(n)) periodEnergy = n->valuedouble;
            }

            unsigned int rawFlag = matchedFeeModel->segFlag[static_cast<size_t>(i)];
            if (rawFlag > 255U) {
                rawFlag = 255U;
            }
            const uint8_t rateNo = static_cast<uint8_t>(rawFlag);

            energyByRate[rateNo] += static_cast<uint64_t>(scaleToU32(periodEnergy, 10000.0));
            chargeByRate[rateNo] += static_cast<uint64_t>(scaleToU32(periodChargeFee, 10000.0));
            serviceByRate[rateNo] += static_cast<uint64_t>(scaleToU32(periodServiceFee, 10000.0));
            // BY ZF: 单价按计费模型发送，不根据金额/电量反算。
            if (static_cast<size_t>(i) < matchedFeeModel->chargeFee.size()) {
                chargePriceByRate[rateNo] = static_cast<uint32_t>(matchedFeeModel->chargeFee[static_cast<size_t>(i)] * 100U); // 0.001元 -> 1e-5元
            }
            if (static_cast<size_t>(i) < matchedFeeModel->serviceFee.size()) {
                servicePriceByRate[rateNo] = static_cast<uint32_t>(matchedFeeModel->serviceFee[static_cast<size_t>(i)] * 100U); // 0.001元 -> 1e-5元
            }
            rateSeen[rateNo] = 1U;
            if (rateNo > maxRateNo) {
                maxRateNo = rateNo;
            }
        }

        // BY ZF: 费率总数按费率号最大值推导（zero-based => count=max+1）。
        const uint8_t rateCount = static_cast<uint8_t>(maxRateNo + 1U);
        body.push_back(rateCount);
        for (uint16_t rateNo = 0; rateNo < static_cast<uint16_t>(rateCount); ++rateNo) {
            const uint64_t energy64 = energyByRate[rateNo];
            const uint64_t charge64 = chargeByRate[rateNo];
            const uint64_t service64 = serviceByRate[rateNo];
            const uint32_t energy = static_cast<uint32_t>(std::min<uint64_t>(energy64, 0xFFFFFFFFULL));
            const uint32_t chargeFeeVal = static_cast<uint32_t>(std::min<uint64_t>(charge64, 0xFFFFFFFFULL));
            const uint32_t serviceFeeVal = static_cast<uint32_t>(std::min<uint64_t>(service64, 0xFFFFFFFFULL));
            const uint32_t chargePrice = rateSeen[rateNo] ? chargePriceByRate[rateNo] : 0U;
            const uint32_t servicePrice = rateSeen[rateNo] ? servicePriceByRate[rateNo] : 0U;
            appendRateDetail(static_cast<uint8_t>(rateNo), chargePrice, servicePrice, energy, chargeFeeVal, serviceFeeVal);
        }
    } else {
        // BY ZF: 计费模型不一致时，按单费率上送总和数据。
        body.push_back(1U);
        const uint32_t energy = scaleToU32(totalElect, 10000.0);
        const uint32_t chargeFeeVal = scaleToU32(totalPowerCost, 10000.0);
        const uint32_t serviceFeeVal = scaleToU32(totalServCost, 10000.0);
        // BY ZF: 模型不一致时，单价由总费用/总电量反算。
        const uint32_t chargePrice = (energy > 0U)
            ? static_cast<uint32_t>((static_cast<uint64_t>(chargeFeeVal) * 10ULL + energy / 2U) / energy)
            : 0U;
        const uint32_t servicePrice = (energy > 0U)
            ? static_cast<uint32_t>((static_cast<uint64_t>(serviceFeeVal) * 10ULL + energy / 2U) / energy)
            : 0U;
        appendRateDetail(0U, chargePrice, servicePrice, energy, chargeFeeVal, serviceFeeVal);
    }

    // BY ZF: 在线模式产生（0x00）+ 离线验证码(BCD3,填0)。
    body.push_back(0x00);
    body.push_back(0x00);
    body.push_back(0x00);
    body.push_back(0x00);

    (void)totalServCost;
    return true;
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
    oss << m_config.mqttTopicPrefix << "/" << module << "/" << static_cast<int>(gun) << "/" << leaf;
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

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(m_config.masterPort));
    if (::inet_pton(AF_INET, m_config.masterHost.c_str(), &addr.sin_addr) != 1) {
        closePlatformTcp();
        return false;
    }

    if (::connect(m_tcpFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        closePlatformTcp();
        return false;
    }

    m_platformConnected = true;
    m_loginState = LOGIN_IDLE;
    m_lastLoginAction = std::chrono::steady_clock::now();
    m_lastHeartbeat = std::chrono::steady_clock::now();
    m_lastChargeInfoReport = std::chrono::steady_clock::now();
    m_lastChargeInfoReportByGun.assign(static_cast<size_t>(m_config.gunCount), m_lastChargeInfoReport);
    m_runtimeChangedByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_tcpRxCache.clear();
    resetCryptoSession();
    m_logSender.info("platform_tcp_connected", m_config.masterHost + ":" + std::to_string(m_config.masterPort));
    return true;
}

void CommProcess::closePlatformTcp()
{
    if (m_tcpFd >= 0) {
        ::close(m_tcpFd);
        m_tcpFd = -1;
    }
    m_platformConnected = false;
    m_loginState = LOGIN_IDLE;
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
    m_sm4SessionKey.fill(0);
    m_sm4SessionKeyReady = false;
    m_loginCryptoPrepared = false;
}

void CommProcess::prepareLoginCryptoContext()
{
    if (m_loginCryptoPrepared) {
        return;
    }
    // BY ZF: 上电/离线恢复后生成16字节会话密钥A（后续用于SM4）。
    if (RAND_bytes(m_sm4SessionKey.data(), static_cast<int>(m_sm4SessionKey.size())) != 1) {
        std::random_device rd;
        for (size_t i = 0; i < m_sm4SessionKey.size(); ++i) {
            m_sm4SessionKey[i] = static_cast<uint8_t>(rd() & 0xFF);
        }
    }
    m_sm4SessionKeyReady = false;
    m_loginCryptoPrepared = true;
}

bool CommProcess::tryUpdateSm2PubKeyFromLoginAck(const uint8_t* body, size_t bodyLen)
{
    // BY ZF: 0x02登录应答：桩编码(7) + 登录结果(1) + 公钥(130, 登录成功才有)。
    // BY ZF: 登录成功时才携带公钥，长度固定130。
    if (bodyLen < 138) {
        return false;
    }
    std::string key(reinterpret_cast<const char*>(body + 8), 130U);
    if (key.empty()) {
        return false;
    }
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][LOGIN_ACK_PUBKEY] " << key << std::endl;
    }
    if (key != m_sm2PublicKeyActive) {
        m_sm2PublicKeyActive = key;
        m_logSender.info("sm2_pubkey_update", "platform_sm2_public_key_updated");
    }
    return true;
}

std::vector<uint8_t> CommProcess::buildPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body)
{
    // BY ZF: 协议头部：0x68 + 设备类型(1) + 协议版本(2) + 长度(2,小端)
    // BY ZF: 数据域：序列号(2) + CP56Time2a(7) + 加密标志(1) + 帧类型(1) + 消息体(N)
    // BY ZF: 校验：CRC16(2,大端)，校验范围为“序列号到消息体”。
    std::vector<uint8_t> frame;
    frame.reserve(32 + body.size());
    frame.push_back(0x68);
    frame.push_back(0x01); // deviceType: 0x01 直流充电桩
    // BY ZF: 按平台联调样例：
    // BY ZF: 登录认证(0x01)使用 0x01 0x15，其它业务帧使用 0x14 0x01。
    if (cmd == kCmdLoginReq) {
        frame.push_back(0x01);
        frame.push_back(0x15);
    } else {
        frame.push_back(0x14);
        frame.push_back(0x01);
    }

    std::vector<uint8_t> payload;
    uint8_t encryptFlag = 0x00;
    if (!encryptBodyByCmd(cmd, body, m_sm4SessionKey, m_sm4SessionKeyReady, payload, encryptFlag)) {
        return std::vector<uint8_t>();
    }
    const uint16_t seq = static_cast<uint16_t>((++m_seq) & 0xFFFF);
    const uint16_t totalLen = static_cast<uint16_t>(2 + 7 + 1 + 1 + payload.size()); // seq+time+enc+cmd+payload
    // BY ZF: 数据长度小端序。
    frame.push_back(static_cast<uint8_t>(totalLen & 0xFF));
    frame.push_back(static_cast<uint8_t>((totalLen >> 8) & 0xFF));

    // BY ZF: 序列号小端序（与长度域一致）。
    frame.push_back(static_cast<uint8_t>(seq & 0xFF));
    frame.push_back(static_cast<uint8_t>((seq >> 8) & 0xFF));

    // BY ZF: 7字节CP56Time2a（ms低/高、分、时、日、月、年）。
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    std::time_t tt = static_cast<std::time_t>(tv.tv_sec);
    std::tm* tmv = std::localtime(&tt);
    const uint16_t ms = static_cast<uint16_t>((tmv ? tmv->tm_sec : 0) * 1000 + (tv.tv_usec / 1000));
    frame.push_back(static_cast<uint8_t>(ms & 0xFF));
    frame.push_back(static_cast<uint8_t>((ms >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(tmv ? (tmv->tm_min & 0x3F) : 0));
    frame.push_back(static_cast<uint8_t>(tmv ? (tmv->tm_hour & 0x1F) : 0));
    frame.push_back(static_cast<uint8_t>(tmv ? (tmv->tm_mday & 0x1F) : 1));
    frame.push_back(static_cast<uint8_t>(tmv ? ((tmv->tm_mon + 1) & 0x0F) : 1));
    frame.push_back(static_cast<uint8_t>(tmv ? ((tmv->tm_year + 1900) % 100) : 26));

    frame.push_back(encryptFlag); // BY ZF: 加密标志按帧类型自动判定
    frame.push_back(cmd);
    frame.insert(frame.end(), payload.begin(), payload.end());

    const uint16_t crc = calcCrc16Modbus(frame.data() + 6, static_cast<size_t>(totalLen));
    // BY ZF: 帧尾CRC按协议和旧实现使用小端序（低字节在前）。
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return frame;
}

bool CommProcess::sendPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body)
{
    if (m_tcpFd < 0) {
        return false;
    }
    const std::vector<uint8_t> frame = buildPlatformFrame(cmd, body);
    if (frame.empty()) {
        return false;
    }
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

std::vector<uint8_t> CommProcess::buildLoginRequestBody() const
{
    // BY ZF: 中石化2.0 登录认证请求体（按协议字段顺序）：
    // BY ZF: 1) 随机秘钥密文(152 ASCII) 2) 登录秘钥密文(140 ASCII)
    // BY ZF: 3) 桩编码BCD(7) 4) 枪数量(1)
    // BY ZF: 5) 主程序包版本号长度(1) 6) 主程序包版本号(ASCII)
    // BY ZF: 7) 网络链接类型(1)
    // BY ZF: 8) SIM卡ICCID(BCD10)
    // BY ZF: 9) 运营商(1)
    // BY ZF: 10) 桩生产批次号(BCD7)
    // BY ZF: 11) 手机号长度(1) + 12) 手机号(ASCII13)
    // BY ZF: 13) 支持网络制式(1) + 14) 当前网络制式(1)
    // BY ZF: 15) 经度(4) + 16) 纬度(4)
    std::vector<uint8_t> body;
    body.reserve(360);

    // BY ZF: 1) 随机秘钥A明文（16字节BIN）取当前会话SM4密钥。
    std::vector<uint8_t> randomSecretPlain(m_sm4SessionKey.begin(), m_sm4SessionKey.end());
    const std::string randomCipher = sm2EncryptToAscii(randomSecretPlain, m_sm2PublicKeyActive);
    if (randomCipher.empty()) {
        return std::vector<uint8_t>();
    }
    body.insert(body.end(), randomCipher.begin(), randomCipher.end());

    // BY ZF: 2) 登录秘钥明文（8字节ASCII）并加密。
    const std::string loginSeed = m_config.loginId.empty() ? m_config.cdzNo : m_config.loginId;
    const std::string loginSecret = deriveLoginSecret8(loginSeed);
    std::vector<uint8_t> loginSecretPlain(loginSecret.begin(), loginSecret.end());
    const std::string loginCipher = sm2EncryptToAscii(loginSecretPlain, m_sm2PublicKeyActive);
    if (loginCipher.empty()) {
        return std::vector<uint8_t>();
    }
    body.insert(body.end(), loginCipher.begin(), loginCipher.end());

    // BY ZF: 3) 桩编码BCD(7)。
    appendPileCodeBcd7(body, m_config.cdzNo);

    // BY ZF: 4) 充电枪数量。
    body.push_back(m_config.gunCount);

    // BY ZF: 5)+6) 主程序包版本号（示例 V1.2.3）。
    const std::string mainVer = "V1.2.3";
    const uint8_t verLen = static_cast<uint8_t>(mainVer.size() > 100 ? 100 : mainVer.size());
    body.push_back(verLen);
    body.insert(body.end(), mainVer.begin(), mainVer.begin() + static_cast<std::ptrdiff_t>(verLen));

    // BY ZF: 7) 网络链接类型：0x00 4g（可后续配置化）。
    body.push_back(0x00);

    // BY ZF: 8) SIM卡ICCID码（BCD 10字节），默认全0。
    body.insert(body.end(), 10, 0x00);

    // BY ZF: 9) 运营商，默认0x00。
    body.push_back(0x00);

    // BY ZF: 10) 桩生产批次号（BCD 7字节），默认全0。
    body.insert(body.end(), 7, 0x00);

    // BY ZF: 11) 手机号长度，默认0。
    body.push_back(0x00);
    // BY ZF: 12) 手机号（ASCII 13字节），默认全0。
    body.insert(body.end(), 13, 0x00);

    // BY ZF: 13) 支持网络制式，默认4G（Bit2=1 => 0x04）。
    body.push_back(0x04);
    // BY ZF: 14) 当前网络制式，默认4G（Bit2=1 => 0x04）。
    body.push_back(0x04);

    // BY ZF: 15) 经度，默认0（4字节）。
    appendU32BE(body, 0U);
    // BY ZF: 16) 纬度，默认0（4字节）。
    appendU32BE(body, 0U);

    return body;
}

std::vector<uint8_t> CommProcess::buildFeeModelRequestBody(uint8_t gunNoBcd) const
{
    // BY ZF: 中石化2.0 0x0D 枪计费模型请求信息体：桩编号BCD(7字节) + 枪号BCD(1字节)。
    std::vector<uint8_t> body;
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(gunNoBcd);
    return body;
}

std::vector<uint8_t> CommProcess::buildTimeSyncRequestBody() const
{
    // BY ZF: 中石化2.0 0x0B 对时请求信息体：仅桩编号BCD(7字节)。
    std::vector<uint8_t> body;
    appendPileCodeBcd7(body, m_config.cdzNo);
    return body;
}

std::vector<uint8_t> CommProcess::buildHeartbeatBody()
{
    // BY ZF: 按当前联调要求，心跳信息体仅上送桩编号（BCD 7字节）。
    std::vector<uint8_t> body;
    appendPileCodeBcd7(body, m_config.cdzNo);
    return body;
}

std::vector<uint8_t> CommProcess::buildRemoteStartAckBody(uint8_t gun, uint8_t result) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }
    // BY ZF: 0xA7 远程启动充电应答：交易流水号BCD16 + 桩编号BCD7 + 枪号BCD1 + 接收结果BCD1。
    appendBcdFixed(body, m_gunRuntimeData[gun].orderNo, 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    body.push_back(static_cast<uint8_t>(result));
    return body;
}

std::vector<uint8_t> CommProcess::buildChargeInfoBody(uint8_t gun)
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size() || gun >= m_config.gunIdList.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];

    // BY ZF: 0x13 实时监测数据（按中石化2.0联调口径组帧）。
    // 1) 交易流水号 BCD16（不足补0）
    appendBcdFixed(body, rd.orderNo, 16);
    // 2) 桩编码 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1：按联调要求固定 0x00
    body.push_back(0x00);

    // 4) 状态：直接使用上游 state_change 映射结果。
    body.push_back(rd.gunStatus);

    // 5) 枪是否归位（按联调要求固定 0x02）
    body.push_back(0x02);
    // 6) 是否插枪：来自 yx 车辆连接状态
    body.push_back(rd.yxVehicleConnectStatus ? 0x01 : 0x00);

    // 7) 输出电压（0.1V）
    const uint16_t outputVoltage = static_cast<uint16_t>(std::max(0.0, rd.voltage * 10.0));
    appendU16BE(body, outputVoltage);
    // 8) 输出电流（0.1A）
    const uint16_t outputCurrent = static_cast<uint16_t>(std::max(0.0, rd.current * 10.0));
    appendU16BE(body, outputCurrent);

    // 9) 枪线温度：使用温度探头1，按偏移50编码
    int gunTemp = static_cast<int>(rd.interfaceTemp1 + 50.0);
    if (gunTemp < 0) gunTemp = 0;
    if (gunTemp > 255) gunTemp = 255;
    body.push_back(static_cast<uint8_t>(gunTemp));

    // 10) 枪线编码（8字节）按联调要求固定0
    body.insert(body.end(), 8, 0x00);

    // 11) SOC（1字节，百分比）
    int soc = static_cast<int>(rd.soc + 0.5);
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;
    body.push_back(static_cast<uint8_t>(soc));

    // 12) 电池组最高温度（偏移50）
    int batMaxTemp = static_cast<int>(rd.batteryMaxTemp + 50.0);
    if (batMaxTemp < 0) batMaxTemp = 0;
    if (batMaxTemp > 255) batMaxTemp = 255;
    body.push_back(static_cast<uint8_t>(batMaxTemp));

    // 13) 累计充电时间（min，2字节）
    const uint16_t chargedMinutes = static_cast<uint16_t>(std::max(0.0, rd.chargedTime / 60.0));
    appendU16BE(body, chargedMinutes);

    // 14) 剩余时间（min，2字节）
    const uint16_t remainMinutes = static_cast<uint16_t>(std::max(0.0, rd.estimatedRemainTime));
    appendU16BE(body, remainMinutes);

    // 15) 充电度数（4字节，精确到小数点后4位）
    appendU32BE(body, static_cast<uint32_t>(std::max(0.0, rd.totalEnergy * 10000.0)));
    // 16) 计费充电度数（4字节，当前与充电度数保持一致）
    appendU32BE(body, static_cast<uint32_t>(std::max(0.0, rd.totalEnergy * 10000.0)));
    // 17) 已充电费（4字节，精确到小数点后4位）
    appendU32BE(body, static_cast<uint32_t>(std::max(0.0, rd.electricAmount * 10000.0)));
    // 18) 已充服务费（4字节，精确到小数点后4位）
    appendU32BE(body, static_cast<uint32_t>(std::max(0.0, rd.serviceAmount * 10000.0)));

    // 19) 枪体温度（偏移50），按联调要求仍取温度探头1
    int gunBodyTemp = static_cast<int>(rd.interfaceTemp1 + 50.0);
    if (gunBodyTemp < 0) gunBodyTemp = 0;
    if (gunBodyTemp > 255) gunBodyTemp = 255;
    body.push_back(static_cast<uint8_t>(gunBodyTemp));

    // 20) 电表示值（8字节，精确到小数点后4位）
    const uint64_t meterVal = static_cast<uint64_t>(std::max(0.0, rd.meterEnergy * 10000.0));
    appendU64BE(body, meterVal);

    // 21) 功率因数（2字节，精确到小数点后2位）：固定0.99
    appendU16BE(body, 99U);

    // 22) 功率需量（4字节，精确到小数点后2位）：实时功率 U*I(kW)*100
    const double powerDemand = (rd.voltage * rd.current) / 1000.0 * 100.0;
    appendU32BE(body, static_cast<uint32_t>(std::max(0.0, powerDemand)));

    // 23) 最大功率需量（4字节，精确到小数点后2位）：固定250000
    appendU32BE(body, 250000U);

    // 24) 是否离线模式下产生：在线模式固定0x00
    body.push_back(0x00);

    // 25) 预留字段（BCD4）：固定0
    body.insert(body.end(), 4, 0x00);

    return body;
}

void CommProcess::reportChargeInfoPeriodic()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const uint8_t count = static_cast<uint8_t>(m_gunRuntimeData.size());
    for (uint8_t gun = 0; gun < count; ++gun) {
        if (gun >= m_lastChargeInfoReportByGun.size() || gun >= m_runtimeChangedByGun.size()) {
            continue;
        }
        const bool forceSend = (m_runtimeChangedByGun[gun] != 0);
        const bool charging = (m_gunRuntimeData[gun].gunStatus == 0x03);
        const std::chrono::seconds interval = charging ? std::chrono::seconds(15) : std::chrono::seconds(300);
        if (!forceSend && (now - m_lastChargeInfoReportByGun[gun] < interval)) {
            continue;
        }
        const std::vector<uint8_t> body = buildChargeInfoBody(gun);
        if (!body.empty()) {
            sendPlatformFrame(kCmdChargeInfo, body);
            m_lastChargeInfoReportByGun[gun] = now;
            m_runtimeChangedByGun[gun] = 0;
        }
    }
}

void CommProcess::driveLoginStateMachine(const std::chrono::steady_clock::time_point& now)
{
    if (!m_platformConnected.load()) {
        return;
    }

    switch (m_loginState) {
    case LOGIN_IDLE:
        prepareLoginCryptoContext();
        m_loginState = LOGIN_REQ_AUTH;
        m_lastLoginAction = now - std::chrono::seconds(m_config.loginRetrySec);
        break;
    case LOGIN_REQ_FEE_MODEL: {
        if (now - m_lastLoginAction >= std::chrono::seconds(m_config.loginRetrySec)) {
            bool allSent = true;
            const uint8_t gunCount = (m_config.gunCount == 0) ? 1 : m_config.gunCount;
            for (uint8_t i = 0; i < gunCount; ++i) {
                const uint8_t gunNoBcd = static_cast<uint8_t>(i + 1); // BY ZF: 枪号按 01/02... 发送
                if (!sendPlatformFrame(kCmdGunFeeModelReq, buildFeeModelRequestBody(gunNoBcd))) {
                    allSent = false;
                    break;
                }
            }
            if (allSent) {
                m_logSender.info("platform_login_step", "gun_fee_model_req_sent");
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
            } else if (sendPlatformFrame(kCmdLoginReq, loginBody)) {
                m_logSender.info("platform_login_step", "login_req_sent");
            } else {
                closePlatformTcp();
            }
            m_lastLoginAction = now;
        }
        break;
    case LOGIN_ONLINE: {
        if (now - m_lastHeartbeat >= std::chrono::seconds(m_config.tcpHeartbeatSec)) {
            if (!sendPlatformFrame(kCmdHeartbeat, buildHeartbeatBody())) {
                closePlatformTcp();
            } else {
                m_lastHeartbeat = now;
            }
        }
        bool hasRuntimeChange = false;
        for (size_t i = 0; i < m_runtimeChangedByGun.size(); ++i) {
            if (m_runtimeChangedByGun[i] != 0) {
                hasRuntimeChange = true;
                break;
            }
        }
        // BY ZF: 外层每15秒调度一次；任一枪运行态变化时立即触发一次调度。
        if (hasRuntimeChange || now - m_lastChargeInfoReport >= std::chrono::seconds(15)) {
            reportChargeInfoPeriodic();
            m_lastChargeInfoReport = now;
        }
        break;
    }
    default:
        break;
    }
}

void CommProcess::maintainPlatformTcp()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

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
        closePlatformTcp();
        return;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
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
                    static_cast<uint8_t>(m_tcpRxCache[4]) |
                    (static_cast<uint16_t>(static_cast<uint8_t>(m_tcpRxCache[5])) << 8));
        const size_t frameLen = static_cast<size_t>(8 + totalLen); // header(6)+data(totalLen)+crc(2)
        if (frameLen < 19) { // 最小：2+7+1+1 + 8(头+CRC)
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
    if (!frame || frameLen < 19) {
        return;
    }
    const uint16_t totalLen = static_cast<uint16_t>(frame[4] | (static_cast<uint16_t>(frame[5]) << 8));
    if (frameLen != static_cast<size_t>(8 + totalLen)) {
        return;
    }
    // BY ZF: 帧尾CRC按小端序读取。
    const uint16_t recvCrc = static_cast<uint16_t>(
                static_cast<uint16_t>(frame[frameLen - 2]) |
                (static_cast<uint16_t>(frame[frameLen - 1]) << 8));
    const uint16_t calcCrc = calcCrc16Modbus(frame + 6, static_cast<size_t>(totalLen));
    if (recvCrc != calcCrc) {
        if (m_config.debugTcp) {
            std::cout << "[Comm][TCP][RX_CRC_FAIL] recv=0x" << std::hex << recvCrc
                      << " calc=0x" << calcCrc << std::dec
                      << " len=" << frameLen << std::endl;
        }
        return;
    }
    const uint8_t encryptFlag = frame[15];
    const uint8_t cmd = frame[16];
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][RX_FRAME] cmd=0x" << std::hex << static_cast<int>(cmd) << std::dec
                  << "(" << platformCmdName(cmd) << ")"
                  << " len=" << frameLen << std::endl;
    }
    if (totalLen < 11) { // seq2 + time7 + enc1 + cmd1
        return;
    }
    const size_t bodyLen = static_cast<size_t>(totalLen - 11);
    const uint8_t* bodyRaw = (bodyLen > 0) ? (frame + 17) : nullptr;
    std::vector<uint8_t> decBody;
    const bool keyAvailable = m_sm4SessionKeyReady || m_loginCryptoPrepared;
    if (!decryptBodyByFlag(encryptFlag, bodyRaw, bodyLen, m_sm4SessionKey, keyAvailable, decBody)) {
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

    if (cmd == kCmdFeeModelAck) {
        // BY ZF: 仅在全枪均非充电中时更新计费模型，避免充电过程中切换费率。
        for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
            if (m_gunRuntimeData[i].gunStatus == 0x03) {
                m_logSender.warn("platform_login_step", "fee_model_ack_ignored_charging");
                return;
            }
        }
        FeeModel feeModel;
        if (!parseFeeModelAck00A(body, decBodyLen, feeModel)) {
            m_logSender.warn("platform_login_step", "fee_model_ack_parse_fail");
            return;
        }
        // BY ZF: 0x0A 计费模型按整桩生效，统一更新全部枪。
        for (size_t i = 0; i < m_feeModelByGun.size(); ++i) {
            m_feeModelByGun[i] = feeModel;
        }
        m_logSender.saveFeeModel(feeModel);

        // BY ZF: 收到有效0x0A后发起0x0B对时请求，再进入在线态。
        (void)sendPlatformFrame(kCmdTimeSyncReq, buildTimeSyncRequestBody());
        m_loginState = LOGIN_ONLINE;
        m_lastHeartbeat = std::chrono::steady_clock::now() - std::chrono::seconds(m_config.tcpHeartbeatSec);
        m_logSender.info("platform_login_step", "fee_model_ack_ok");
        return;
    }

    if (cmd == kCmdLoginAck) {
        if (m_loginState == LOGIN_REQ_AUTH) {
            if (!body || decBodyLen < 8) {
                m_loginState = LOGIN_IDLE;
                m_logSender.warn("platform_login_step", "login_ack_invalid");
                return;
            }

            // BY ZF: body[0..6]=桩编码BCD7，body[7]=登录结果。
            const uint8_t feedbackResult = body[7];
            if (feedbackResult != 0x00) {
                m_loginState = LOGIN_IDLE;
                m_logSender.warn("platform_login_step", "login_ack_invalid");
                return;
            }
            m_sm4SessionKeyReady = true;
            (void)tryUpdateSm2PubKeyFromLoginAck(body, decBodyLen);

            // BY ZF: 登录成功后进入计费模型请求阶段；0x0B在收到0x0A后发起。
            m_loginState = LOGIN_REQ_FEE_MODEL;
            m_lastLoginAction = std::chrono::steady_clock::now() - std::chrono::seconds(m_config.loginRetrySec);
            m_logSender.info("platform_login_step", "login_ack_ok");
        }
        return;
    }

    if (cmd == kCmdTimeSyncAck) {
        // BY ZF: 0x0C 对时应答：桩编号BCD7 + CP56Time2a(7)。
        if (body && decBodyLen >= 14) {
            const uint8_t* t = body + 7;
            const uint16_t ms = static_cast<uint16_t>(t[0]) |
                                (static_cast<uint16_t>(t[1]) << 8);
            const int second = (ms / 1000U) % 60U;
            const int minute = t[2] & 0x3F;
            const int hour = t[3] & 0x1F;
            const int day = t[4] & 0x1F;
            const int month = t[5] & 0x0F;
            const int year = 2000 + (t[6] & 0x7F);
            syncSystemTime(year, month, day, hour, minute, second);
        }
        return;
    }

    if (cmd == kCmdHeartbeatAck) {
        return;
    }

    if (cmd == kCmdRemoteStartCmd) {
        uint8_t gun = 0;
        cJSON* startData = nullptr;
        FeeModel parsedFeeModel;
        if (parseRemoteStart0A8(body, decBodyLen, gun, &startData, parsedFeeModel)) {
            if (!parsedFeeModel.feeModelId.empty() && gun < m_feeModelByGun.size()) {
                m_feeModelByGun[gun] = parsedFeeModel;
            }
            if (!parsedFeeModel.feeModelId.empty()) {
                m_logSender.saveFeeModel(parsedFeeModel);
            }
            publishPlatCommand(gun, "start_charge", startData);

            // BY ZF: 0xA8 下发后立即回复 0xA7 远程启动应答（当前联调口径固定成功0x01）。
            const std::vector<uint8_t> ackBody = buildRemoteStartAckBody(gun, 0x01);
            if (!ackBody.empty()) {
                (void)sendPlatformFrame(kCmdRemoteStartAck, ackBody);
            }
        } else {
            // BY ZF: 启动命令解析失败（常见为计费模型未就绪）：
            // BY ZF: 回复0xA7失败，并立即重发0x0D枪计费模型请求。
            const std::vector<uint8_t> nackBody = buildRemoteStartAckBody(gun, 0x00);
            if (!nackBody.empty()) {
                (void)sendPlatformFrame(kCmdRemoteStartAck, nackBody);
            }
            const uint8_t gunCount = (m_config.gunCount == 0) ? 1 : m_config.gunCount;
            for (uint8_t i = 0; i < gunCount; ++i) {
                (void)sendPlatformFrame(kCmdGunFeeModelReq, buildFeeModelRequestBody(static_cast<uint8_t>(i + 1)));
            }
            m_logSender.warn("platform_start_reject", "fee_model_not_ready_requery");
        }
        if (startData) {
            cJSON_Delete(startData);
        }
        return;
    }

    if (cmd == kCmdRemoteStopCmd) {
        uint8_t gun = 0;
        cJSON* stopData = nullptr;
        if (parseRemoteStop036(body, decBodyLen, gun, &stopData)) {
            publishPlatCommand(gun, "stop_charge", stopData);
        }
        if (stopData) {
            cJSON_Delete(stopData);
        }
        return;
    }

    if (cmd == kCmdRecordConfirm) {
        uint8_t gun = 0;
        cJSON* cfmData = nullptr;
        if (parseRecordConfirm040(body, decBodyLen, gun, &cfmData)) {
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

bool CommProcess::parseFeeModelAck00A(const uint8_t* body, size_t bodyLen, FeeModel& feeModel)
{
    if (!body || bodyLen < 11) {
        return false;
    }

    // BY ZF: 0x0A 信息体：桩编码BCD7 + 枪号BCD1 + 计费模型编号BCD2 + 费率组数量1 + N组费率 + 位付识别1 + 48个半小时映射。
    const std::string modelNo = bcdToDigitString(body + 8, 2);
    const uint8_t feeCount = body[10];
    if (feeCount == 0 || feeCount > 48) {
        return false;
    }

    const size_t ratesLen = static_cast<size_t>(feeCount) * 8U; // 每组：电费4 + 服务费4
    const size_t needLen = 11U + ratesLen + 1U + 48U;
    if (bodyLen < needLen) {
        return false;
    }

    std::vector<uint32_t> chargeRateRaw;
    std::vector<uint32_t> serviceRateRaw;
    chargeRateRaw.reserve(feeCount);
    serviceRateRaw.reserve(feeCount);

    size_t pos = 11;
    for (uint8_t i = 0; i < feeCount; ++i) {
        // BY ZF: 0x0A 费率字段按小端序编码（与平台示例一致）。
        const uint32_t c = static_cast<uint32_t>(body[pos]) |
                           (static_cast<uint32_t>(body[pos + 1]) << 8) |
                           (static_cast<uint32_t>(body[pos + 2]) << 16) |
                           (static_cast<uint32_t>(body[pos + 3]) << 24);
        const uint32_t s = static_cast<uint32_t>(body[pos + 4]) |
                           (static_cast<uint32_t>(body[pos + 5]) << 8) |
                           (static_cast<uint32_t>(body[pos + 6]) << 16) |
                           (static_cast<uint32_t>(body[pos + 7]) << 24);
        chargeRateRaw.push_back(c);
        serviceRateRaw.push_back(s);
        pos += 8;
    }

    // BY ZF: 计损比例保留，当前不处理。
    pos += 1;

    std::vector<uint8_t> slotMap(48, 0);
    for (size_t i = 0; i < 48; ++i) {
        slotMap[i] = body[pos + i];
    }

    feeModel.feeModelId = modelNo.empty() ? "0000" : modelNo;
    feeModel.timeSeg.clear();
    feeModel.segFlag.clear();
    feeModel.chargeFee.clear();
    feeModel.serviceFee.clear();

    // BY ZF: 先缓存 feeCount 个费率组，再按48个半小时位图逐段切换生成时段。
    // BY ZF: 中石化协议按 zero-based 编码：0..feeCount-1（0表示第1组）。
    int prevFeeIdx = -1;
    for (int slot = 0; slot < 48; ++slot) {
        int feeIdx = static_cast<int>(slotMap[static_cast<size_t>(slot)]); // 0x00 => 第1组
        if (feeIdx < 0 || feeIdx >= static_cast<int>(feeCount)) {
            feeIdx = 0; // BY ZF: 异常值兜底到第1组
        }

        const bool isNewSeg = (slot == 0) || (feeIdx != prevFeeIdx);
        if (!isNewSeg) {
            continue;
        }

        const int minute = slot * 30;
        const int hh = minute / 60;
        const int mm = minute % 60;
        char ts[8] = {0};
        std::snprintf(ts, sizeof(ts), "%02d%02d", hh, mm);
        feeModel.timeSeg.push_back(ts);
        feeModel.segFlag.push_back(static_cast<unsigned int>(feeIdx + 1));

        // BY ZF: 平台费率精度到10^-5元，转换为内部0.001元（四舍五入）。
        const unsigned int chargeMilli = static_cast<unsigned int>((chargeRateRaw[static_cast<size_t>(feeIdx)] + 50U) / 100U);
        const unsigned int serviceMilli = static_cast<unsigned int>((serviceRateRaw[static_cast<size_t>(feeIdx)] + 50U) / 100U);
        feeModel.chargeFee.push_back(chargeMilli);
        feeModel.serviceFee.push_back(serviceMilli);

        prevFeeIdx = feeIdx;
    }

    if (feeModel.timeSeg.empty()) {
        return false;
    }
    feeModel.timeNum = static_cast<unsigned char>(feeModel.timeSeg.size());
    return true;
}

bool CommProcess::parseRemoteStart0A8(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData, FeeModel& feeModel)
{
    if (!body || !outData) {
        return false;
    }
    // BY ZF: 0xA8 最小长度：交易流水号16 + 桩编号7 + 枪号1 + 逻辑卡号8 + 物理卡号8 + 账户余额4 + 最大功率2
    if (bodyLen < 46) {
        return false;
    }

    const uint8_t* tradeBcd = body;          // 16
    const uint8_t* pileBcd = body + 16;      // 7
    const uint8_t gunBcd = body[23];         // 1
    const uint8_t* logicCardBcd = body + 24; // 8
    const uint8_t* phyCardBin = body + 32;   // 8
    const uint32_t balanceRaw = static_cast<uint32_t>(body[40]) |
                                (static_cast<uint32_t>(body[41]) << 8) |
                                (static_cast<uint32_t>(body[42]) << 16) |
                                (static_cast<uint32_t>(body[43]) << 24);
    const uint16_t maxPowerKw = static_cast<uint16_t>(body[44]) |
                                (static_cast<uint16_t>(body[45]) << 8);

    const int gunNo = ((gunBcd >> 4) & 0x0F) * 10 + (gunBcd & 0x0F);
    if (gunNo <= 0) {
        return false;
    }
    const int gunIndex = gunNo - 1;
    if (gunIndex < 0 || gunIndex >= static_cast<int>(m_gunRuntimeData.size())) {
        return false;
    }
    gun = static_cast<uint8_t>(gunIndex);

    const std::string orderNo = bcdToDigitString(tradeBcd, 16);
    const std::string chargeUserNo = bcdToDigitString(logicCardBcd, 8);
    const std::string pileNo = bcdToDigitString(pileBcd, 7);
    (void)pileNo;

    // BY ZF: 0xA8不携带时段费率，使用该枪已同步缓存的计费模型。
    if (gun < m_feeModelByGun.size()) {
        feeModel = m_feeModelByGun[gun];
    } else {
        feeModel.feeModelId.clear();
        feeModel.timeNum = 0;
        feeModel.timeSeg.clear();
        feeModel.segFlag.clear();
        feeModel.chargeFee.clear();
        feeModel.serviceFee.clear();
    }
    // BY ZF: 无可用计费模型时拒绝启动（由上层回复0xA7失败并重发0x0D）。
    const bool feeModelReady =
            (!feeModel.feeModelId.empty()) &&
            (feeModel.timeNum > 0) &&
            (feeModel.timeSeg.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.chargeFee.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.serviceFee.size() >= static_cast<size_t>(feeModel.timeNum));
    if (!feeModelReady) {
        // BY ZF: 仅缓存订单号，供0xA7失败应答使用。
        if (gun < m_gunRuntimeData.size()) {
            m_gunRuntimeData[gun].orderNo = orderNo;
        }
        return false;
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "startTime", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddStringToObject(data, "chargeUserNo", chargeUserNo.c_str());
    cJSON_AddStringToObject(data, "orderNo", orderNo.c_str());

    cJSON_AddStringToObject(data, "logicCardNo", chargeUserNo.c_str());
    cJSON_AddStringToObject(data, "physicalCardHex", toHex(phyCardBin, 8).c_str());
    
    cJSON_AddNumberToObject(data, "chargeMode", 0x60);
    cJSON_AddNumberToObject(data, "prechargeAmount", static_cast<double>(balanceRaw) / 100.0);
    cJSON_AddNumberToObject(data, "maxPowerKw", maxPowerKw);
    cJSON_AddNumberToObject(data, "feeModelNo", 0);
    cJSON_AddStringToObject(data, "feeModelId", feeModel.feeModelId.c_str());
    cJSON_AddNumberToObject(data, "billingFlag", 0);
    cJSON_AddNumberToObject(data, "userStatus", 0);
    // BY ZF: 补充启动充电控制参数，供 tcu_logic/pile_controller 透传使用。
    cJSON_AddNumberToObject(data, "loadControlSwitch", 0x02);
    cJSON_AddNumberToObject(data, "plugAndChargeFlag", 0x01);
    cJSON_AddNumberToObject(data, "auxPowerVoltage", 0x0C);
    cJSON_AddNumberToObject(data, "mergeChargeFlag", 0x00);


    // BY ZF: 费率相关字段（沿用已缓存计费模型）。
    cJSON_AddNumberToObject(data, "timeNum", static_cast<int>(feeModel.timeNum));
    cJSON* timeSeg = cJSON_CreateArray();
    cJSON* chargeFee = cJSON_CreateArray();
    cJSON* serviceFee = cJSON_CreateArray();
    for (size_t i = 0; i < feeModel.timeSeg.size(); ++i) {
        cJSON_AddItemToArray(timeSeg, cJSON_CreateString(feeModel.timeSeg[i].c_str()));
    }
    for (size_t i = 0; i < feeModel.chargeFee.size(); ++i) {
        cJSON_AddItemToArray(chargeFee, cJSON_CreateNumber(static_cast<double>(feeModel.chargeFee[i]) / 1000.0));
    }
    for (size_t i = 0; i < feeModel.serviceFee.size(); ++i) {
        cJSON_AddItemToArray(serviceFee, cJSON_CreateNumber(static_cast<double>(feeModel.serviceFee[i]) / 1000.0));
    }
    cJSON_AddItemToObject(data, "timeSeg", timeSeg);
    cJSON_AddItemToObject(data, "chargeFee", chargeFee);
    cJSON_AddItemToObject(data, "serviceFee", serviceFee);

    // BY ZF: 保存启动命令解析结果到每枪运行态缓存，供后续平台上送复用。
    if (gun < m_gunRuntimeData.size()) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        //本地时间转BCD码
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
        rd.prechargeAmount = static_cast<double>(balanceRaw) / 100.0;
        rd.userStatus = 0;
        rd.billingFlag = 0;
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

    *outData = data;
    return true;
}

bool CommProcess::parseRemoteStop036(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData)
{
    if (!body || !outData) {
        return false;
    }
    // BY ZF: 0x15 最小长度：枪ID4 + 订单10 + 操作类型1
    if (bodyLen < 15) {
        return false;
    }

    const uint32_t gunId = readU32BE(body);
    const uint8_t* orderBcd = body + 4;
    const uint8_t operationType = body[14];
    if (operationType != 0x01) {
        return false;
    }

    int gunIndex = -1;
    for (size_t i = 0; i < m_config.gunIdList.size(); ++i) {
        if (m_config.gunIdList[i] == gunId) {
            gunIndex = static_cast<int>(i);
            break;
        }
    }
    if (gunIndex < 0) {
        return false;
    }
    gun = static_cast<uint8_t>(gunIndex);

    const std::string orderNo = bcdToDigitString(orderBcd, 10);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "orderNo", orderNo.c_str());
    cJSON_AddNumberToObject(data, "stopReason", 0x01);  // BY ZF: 按要求固定停机原因 0x01
    cJSON_AddNumberToObject(data, "tcuStopCode", 0);
    *outData = data;
    return true;
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
