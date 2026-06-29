/**
 * tcu_comm YunKuaiChong process implementation
 * BY LZW
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
#include <cmath>
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
#include <sqlite3.h>

#ifndef NID_sm2
#ifdef NID_sm2p256v1
#define NID_sm2 NID_sm2p256v1
#endif
#endif

namespace {
    void feedDaemonWatchdog()
    {
        // BY LZW: 通过守护进程看门狗消息队列上报 tcu_comm 存活状态。
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

    // BY ZF: 云快充 V1.6 基础帧格式常量。
    static const uint8_t kYlcFrameHead = 0x68;
    static const size_t kYlcFrameHeadLen = 1;
    static const size_t kYlcLengthLen = 1;
    static const size_t kYlcSeqLen = 2;
    static const size_t kYlcEncryptFlagLen = 1;
    static const size_t kYlcFrameTypeLen = 1;
    static const size_t kYlcCrcLen = 2;
    static const size_t kYlcFixedDataLen = kYlcSeqLen + kYlcEncryptFlagLen + kYlcFrameTypeLen;
    static const size_t kYlcMinFrameLen = kYlcFrameHeadLen + kYlcLengthLen + kYlcFixedDataLen + kYlcCrcLen;
    static const size_t kYkcChargeMaxDataLen = 200; // BY ZF: 云快充V1.6充电协议数据域长度不超过200字节。
    static const size_t kYlcMaxDataLen = 255; // BY ZF: 传输层兼容保留放电扩展帧，按1字节长度域上限处理。
    static const size_t kYlcMaxBodyLen = kYlcMaxDataLen - kYlcFixedDataLen;
    static const uint8_t kYlcEncryptPlain = 0x00;
    static const uint8_t kYlcEncrypt3Des = 0x01;
    static const uint16_t kYlcCrcPoly = 0xA001;
    static const size_t kYlcQrPrefixProtocolMaxLen = 200;
    static const size_t kYlcQrPrefixFrameMaxLen = kYlcMaxBodyLen - 9U;
    static const bool kYlcEnablePhase2VinStart = false;     // BY LZW: VIN/即插即充启动二期拓展，首版默认关闭。
    static const bool kYlcEnablePhase2MergeCharge = false;  // BY LZW: 并充 0xA1~0xA4 二期拓展，首版默认关闭。
    static const bool kYlcEnablePhase2ParkingLock = false;  // BY LZW: 地锁 0x61/0x63 二期拓展，首版默认关闭。

    // BY LZW: YLC 公共链路与计费模型帧定义。
    static const uint8_t kCmdLoginReq = 0x01;
    static const uint8_t kCmdLoginAck = 0x02;
    static const uint8_t kCmdHeartbeat = 0x03;
    static const uint8_t kCmdHeartbeatAck = 0x04;
    static const uint8_t kCmdFeeModelCheckReq = 0x05;
    static const uint8_t kCmdFeeModelCheckAck = 0x06;
    static const uint8_t kCmdFeeModelReq = 0x09;
    static const uint8_t kCmdFeeModelAck = 0x0A;
    static const uint8_t kCmdTimeSyncAck = 0x55;
    static const uint8_t kCmdTimeSyncSet = 0x56;
    static const uint8_t kCmdFeeModelSetAck = 0x57;
    static const uint8_t kCmdFeeModelSet = 0x58;
    static const uint8_t kCmdQrCodeSet = 0xF0;
    static const uint8_t kCmdQrCodeSetAck = 0xF1;

    // BY LZW: YLC 充电实时与 GBT 过程帧定义。
    static const uint8_t kCmdReadChargeInfo = 0x12;
    static const uint8_t kCmdChargeInfo = 0x13;
    static const uint8_t kCmdBrm = 0x15;
    static const uint8_t kCmdBcp = 0x17;
    static const uint8_t kCmdChargeEndStage = 0x19;
    static const uint8_t kCmdChargeError = 0x1B;
    static const uint8_t kCmdBst = 0x1D;
    static const uint8_t kCmdBclBcsCcs = 0x23;
    static const uint8_t kCmdCst = 0x21;
    static const uint8_t kCmdBsm = 0x25;

    // BY LZW: YLC 充电运营交互帧定义。
    static const uint8_t kCmdStartApply = 0x31;
    static const uint8_t kCmdStartApplyAck = 0x32;
    static const uint8_t kCmdRemoteStartAck = 0x33;
    static const uint8_t kCmdRemoteStartCmd = 0x34;
    static const uint8_t kCmdRemoteStopAck = 0x35;
    static const uint8_t kCmdRemoteStopCmd = 0x36;
    static const uint8_t kCmdUploadTradeRecord = 0x3B;
    static const uint8_t kCmdRecordConfirm = 0x40;
    static const uint8_t kCmdBalanceUpdateAck = 0x41;
    static const uint8_t kCmdBalanceUpdate = 0x42;
    static const uint8_t kCmdOfflineCardSyncAck = 0x43;
    static const uint8_t kCmdOfflineCardSync = 0x44;
    static const uint8_t kCmdOfflineCardClearAck = 0x45;
    static const uint8_t kCmdOfflineCardClear = 0x46;
    static const uint8_t kCmdOfflineCardQueryAck = 0x47;
    static const uint8_t kCmdOfflineCardQuery = 0x48;
    static const uint8_t kCmdWorkParamSetAck = 0x51;
    static const uint8_t kCmdWorkParamSet = 0x52;
    static const uint8_t kCmdParkingLockStatus = 0x61;
    static const uint8_t kCmdParkingLockCtrl = 0x62;
    static const uint8_t kCmdParkingLockCtrlAck = 0x63;
    static const uint8_t kCmdPlugVinReport = 0x71;
    static const uint8_t kCmdPlugVinReportAck = 0x72;
    static const uint8_t kCmdPlugVinQuery = 0x73;
    static const uint8_t kCmdRemoteRebootAck = 0x91;
    static const uint8_t kCmdRemoteReboot = 0x92;
    static const uint8_t kCmdRemoteUpdateAck = 0x93;
    static const uint8_t kCmdRemoteUpdate = 0x94;
    static const uint8_t kCmdRemoteLogRequestAck = 0x95;
    static const uint8_t kCmdRemoteLogRequest = 0x96;

    // BY LZW: YLC 并充帧定义，0xA4/0xA3 方向已确认按章节 12.3/12.4。
    static const uint8_t kCmdMergeChargeApply = 0xA1;
    static const uint8_t kCmdMergeChargeApplyAck = 0xA2;
    static const uint8_t kCmdMergeStartReply = 0xA3;
    static const uint8_t kCmdRemoteMergeStart = 0xA4;

    // BY LZW: YLC 放电协议 V1.6.1 帧定义。
    static const uint8_t kCmdDischargeStartAck = 0xE1;
    static const uint8_t kCmdDischargeStartCmd = 0xE2;
    static const uint8_t kCmdDischargeStopAck = 0xE3;
    static const uint8_t kCmdDischargeStopCmd = 0xE4;
    static const uint8_t kCmdDischargeCmdResult = 0xE5;
    static const uint8_t kCmdReadDischargeInfo = 0xE6;
    static const uint8_t kCmdDischargeInfo = 0xE7;
    static const uint8_t kCmdDischargeRecordConfirm = 0xE8;
    static const uint8_t kCmdDischargeTradeRecord = 0xE9;

    static bool isDischargeFrameCmd(uint8_t cmd)
    {
        return cmd >= 0xE1U && cmd <= 0xE9U;
    }

    // BY LZW: YLC PDF 6.1：桩类型 0x00=直流桩，0x01=交流桩。
    static const uint8_t kFixedChargerType = 0x00;
    static const uint8_t kFixedGunType = 0x01;

    // BY LZW: 登录秘钥(8字节ASCII)生成（优先使用loginId，回退到cdzNo）。
    // BY LZW: 允许字母+数字，不再仅保留数字。
    static const int kOfflineReplayQueryLimit = 100;
    static const int kOfflineReplayEmptyWaitSec = 15;
    static const int kOfflineReplayConfirmWaitSec = 30;

    static bool isYlcFeeModelReady(const FeeModel& feeModel)
    {
        const size_t timeNum = static_cast<size_t>(feeModel.timeNum);
        return !feeModel.feeModelId.empty() &&
               timeNum > 0U &&
               feeModel.timeSeg.size() >= timeNum &&
               feeModel.chargeFee.size() >= timeNum &&
               feeModel.serviceFee.size() >= timeNum;
    }

    static std::string ylcFeeModelNoFromLocalId(const std::string& feeModelId)
    {
        const std::string::size_type pos = feeModelId.rfind('_');
        std::string modelNo = (pos == std::string::npos) ? feeModelId : feeModelId.substr(pos + 1U);
        if (modelNo.empty() || modelNo.size() > 4U) {
            return "0000";
        }
        for (size_t i = 0; i < modelNo.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(modelNo[i]))) {
                return "0000";
            }
        }
        while (modelNo.size() < 4U) {
            modelNo.insert(modelNo.begin(), '0');
        }
        return modelNo;
    }

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

    // BY LZW: 桩编码 -> 7字节BCD，不足7字节补0（取数字字符）。
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

    // BY LZW: 桩编码归一化为14位数字字符串（用于平台下发桩号一致性校验）。
    static std::string normalizePileCode14(const std::string& cdzNo)
    {
        std::string digits;
        for (size_t i = 0; i < cdzNo.size(); ++i) {
            if (cdzNo[i] >= '0' && cdzNo[i] <= '9') {
                digits.push_back(cdzNo[i]);
            }
        }
        if (digits.size() > 14U) {
            digits = digits.substr(digits.size() - 14U);
        }
        if (digits.size() < 14U) {
            digits.insert(digits.begin(), 14U - digits.size(), '0');
        }
        return digits;
    }

    // BY LZW: Base64编码（无换行）。
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
                // BY LZW: OpenSSL 1.1.1 上将 EC 公钥别名为 SM2，确保 EVP_PKEY_encrypt 可用。
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
            raw.insert(raw.begin(), 0x04U); // BY LZW: 兼容仅X||Y格式公钥。
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
        // BY LZW: OpenSSL 1.1.1 上将 EC 公钥别名为 SM2，确保 EVP_PKEY_encrypt 可用。
        EVP_PKEY_set_alias_type(pkey, EVP_PKEY_SM2);
#endif
        return pkey;
    }

    // BY LZW: SM2加密（输出Base64 ASCII）。
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

                    // BY LZW: OpenSSL SM2默认产物是ASN.1 DER，这里转换为平台要求的C1C3C2再做Base64。
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

    // BY LZW: YLC 首版仅支持明文。0x01 代表 3DES，参数待平台确认，不能复用中石化 SM4。
    static bool encryptYlcBody(uint8_t cmd,
                               const std::vector<uint8_t>& plain,
                               bool encryptEnabled,
                               std::vector<uint8_t>& out,
                               uint8_t& encryptFlag)
    {
        (void)cmd;
        if (!encryptEnabled) {
            encryptFlag = kYlcEncryptPlain;
            out = plain;
            return true;
        }
        encryptFlag = kYlcEncrypt3Des;
        out.clear();
        return false;
    }

    static bool decryptYlcBody(uint8_t encryptFlag,
                               const uint8_t* body,
                               size_t bodyLen,
                               std::vector<uint8_t>& out)
    {
        if (encryptFlag == kYlcEncryptPlain) {
            if (!body || bodyLen == 0U) {
                out.clear();
            } else {
                out.assign(body, body + bodyLen);
            }
            return true;
        }
        out.clear();
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

    // BY ZF: 交易流水号按协议BCD长度归一化，供0x3B/0xE9确认映射复用。
    static std::string protocolTradeNoFromLocal(const std::string& text, size_t bcdBytes)
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
        return digits;
    }

    static void appendBcdFixed(std::vector<uint8_t>& out, const std::string& text, size_t bcdBytes)
    {
        const std::string digits = protocolTradeNoFromLocal(text, bcdBytes);
        for (size_t i = 0; i < digits.size(); i += 2) {
            const uint8_t hi = static_cast<uint8_t>(digits[i] - '0');
            const uint8_t lo = static_cast<uint8_t>(digits[i + 1] - '0');
            out.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
    }

    static int bcdByteToInt(uint8_t bcd)
    {
        const int hi = (bcd >> 4) & 0x0F;
        const int lo = bcd & 0x0F;
        if (hi > 9 || lo > 9) {
            return -1;
        }
        return hi * 10 + lo;
    }

    static bool bcdByteToInt(uint8_t bcd, int& out)
    {
        const int value = bcdByteToInt(bcd);
        if (value < 0) {
            return false;
        }
        out = value;
        return true;
    }

    static uint8_t intToBcdByte(int value)
    {
        if (value < 0) value = 0;
        if (value > 99) value = 99;
        return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
    }

    static uint8_t encodeYlcProtocolVersionBin1(const std::string& version)
    {
        std::vector<int> nums;
        std::string cur;
        for (size_t i = 0; i <= version.size(); ++i) {
            const char ch = (i < version.size()) ? version[i] : '.';
            if (ch >= '0' && ch <= '9') {
                cur.push_back(ch);
            } else if (!cur.empty()) {
                int v = std::atoi(cur.c_str());
                if (v < 0) v = 0;
                nums.push_back(v);
                cur.clear();
            }
        }
        if (nums.empty()) {
            return 0x0A;
        }
        const int major = nums[0];
        const int minor = (nums.size() > 1U) ? nums[1] : 0;
        int encoded = major * 10 + minor;
        if (encoded < 0) encoded = 0;
        if (encoded > 255) encoded = 255;
        return static_cast<uint8_t>(encoded);
    }

    // BY LZW: JSON 字段读取工具。
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

    static bool jsonHasKey(cJSON* obj, const char* key)
    {
        return obj && key && cJSON_GetObjectItem(obj, key) != nullptr;
    }

    static bool jsonTruthy(cJSON* obj, const char* key, bool& out)
    {
        cJSON* n = cJSON_GetObjectItem(obj, key);
        if (!n) {
            return false;
        }
        if (cJSON_IsBool(n)) {
            out = cJSON_IsTrue(n);
            return true;
        }
        if (cJSON_IsNumber(n)) {
            out = (n->valuedouble != 0.0);
            return true;
        }
        if (cJSON_IsString(n) && n->valuestring) {
            out = (std::strcmp(n->valuestring, "1") == 0 ||
                   std::strcmp(n->valuestring, "true") == 0 ||
                   std::strcmp(n->valuestring, "TRUE") == 0);
            return true;
        }
        return false;
    }

    static bool jsonHasDischargeFlag(cJSON* root, cJSON* data)
    {
        return jsonHasKey(data, "v2g") || jsonHasKey(data, "isDischarge") ||
               jsonHasKey(root, "v2g") || jsonHasKey(root, "isDischarge");
    }

    static bool jsonIsDischargePayload(cJSON* root, cJSON* data)
    {
        bool v = false;
        if (jsonTruthy(data, "v2g", v)) {
            return v;
        }
        if (jsonTruthy(data, "isDischarge", v)) {
            return v;
        }
        if (jsonTruthy(root, "v2g", v)) {
            return v;
        }
        if (jsonTruthy(root, "isDischarge", v)) {
            return v;
        }
        return false;
    }

    // BY ZF: 区分充电0x3B和放电0xE9，避免相同协议流水号确认串单。
    static std::string tradeRecordUploadKey(uint8_t cmd, const std::string& tradeNo)
    {
        return std::string(cmd == 0xE9 ? "E9:" : "3B:") + tradeNo;
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

    static void appendAsciiFixed(std::vector<uint8_t>& out, const std::string& text, size_t width, uint8_t fill = 0x00)
    {
        const size_t n = std::min(width, text.size());
        out.insert(out.end(), text.begin(), text.begin() + n);
        if (n < width) {
            out.insert(out.end(), width - n, fill);
        }
    }

    static std::string asciiFixedToString(const uint8_t* p, size_t len)
    {
        if (!p || len == 0U) {
            return std::string();
        }
        size_t n = 0;
        while (n < len && p[n] != 0x00U) {
            ++n;
        }
        while (n > 0U && (p[n - 1U] == ' ' || p[n - 1U] == '\t')) {
            --n;
        }
        return std::string(reinterpret_cast<const char*>(p), n);
    }

    static std::string jsonGetStringCompat(cJSON* obj, const char* key1, const char* key2)
    {
        std::string text = jsonGetString(obj, key1);
        if (text.empty() && key2) {
            text = jsonGetString(obj, key2);
        }
        return text;
    }

    // BY LZW: 浮点缩放并四舍五入到无符号整数。
    static uint32_t scaleToU32(double v, double scale)
    {
        if (v <= 0.0) {
            return 0U;
        }
        return static_cast<uint32_t>(v * scale + 0.5);
    }

    static uint64_t scaleToU64(double v, double scale)
    {
        if (v <= 0.0) {
            return 0ULL;
        }
        return static_cast<uint64_t>(v * scale + 0.5);
    }

    static uint16_t scaleToU16(double v, double scale)
    {
        if (v <= 0.0) {
            return 0U;
        }
        const double raw = v * scale + 0.5;
        if (raw > 65535.0) {
            return 65535U;
        }
        return static_cast<uint16_t>(raw);
    }

    static uint8_t clampToU8(int v)
    {
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<uint8_t>(v);
    }

    static void setYlcFaultBit(std::vector<uint8_t>& bits, size_t byteNo, uint8_t bitNo, bool on)
    {
        if (!on || byteNo >= bits.size() || bitNo > 7U) {
            return;
        }
        bits[byteNo] = static_cast<uint8_t>(bits[byteNo] | static_cast<uint8_t>(1U << bitNo));
    }

    // BY LZW: 小端序追加工具（YLC BIN 字段按小端组织）。
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

    static void appendU40LE(std::vector<uint8_t>& out, uint64_t v)
    {
        if (v > 0xFFFFFFFFFFULL) {
            v = 0xFFFFFFFFFFULL;
        }
        for (int i = 0; i < 5; ++i) {
            out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    static void appendU64LE(std::vector<uint8_t>& out, uint64_t v)
    {
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    // BY LZW: YYYYMMDDhhmmss 转 CP56Time2a(7)；毫秒未知时按整秒。
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

    // BY LZW: 平台命令字转名称，便于TCP调试输出。
    static const char* platformCmdName(uint8_t cmd)
    {
        switch (cmd) {
        case 0x01: return "login_req";
        case 0x02: return "login_ack";
        case 0x03: return "heartbeat";
        case 0x04: return "heartbeat_ack";
        case 0x05: return "fee_model_check_req";
        case 0x06: return "fee_model_check_ack";
        case 0x09: return "fee_model_req";
        case 0x0A: return "fee_model_ack";
        case 0x12: return "read_charge_info";
        case 0x13: return "charge_info";
        case 0x15: return "brm";
        case 0x17: return "bcp";
        case 0x19: return "bsd";
        case 0x1B: return "charge_error";
        case 0x1D: return "bst";
        case 0x21: return "cst";
        case 0x23: return "bcl";
        case 0x25: return "bsm";
        case 0x31: return "start_apply";
        case 0x32: return "start_apply_ack";
        case 0x33: return "remote_start_ack";
        case 0x34: return "remote_start_cmd";
        case 0x35: return "remote_stop_ack";
        case 0x36: return "remote_stop_cmd";
        case 0x3B: return "upload_trade_record";
        case 0x40: return "record_confirm";
        case 0x41: return "balance_update_ack";
        case 0x42: return "balance_update";
        case 0x43: return "offline_card_sync_ack";
        case 0x44: return "offline_card_sync";
        case 0x45: return "offline_card_clear_ack";
        case 0x46: return "offline_card_clear";
        case 0x47: return "offline_card_query_ack";
        case 0x48: return "offline_card_query";
        case 0x51: return "work_param_set_ack";
        case 0x52: return "work_param_set";
        case 0x61: return "parking_lock_status";
        case 0x62: return "parking_lock_ctrl";
        case 0x63: return "parking_lock_ctrl_ack";
        case 0x71: return "plug_vin_report";
        case 0x72: return "plug_vin_report_ack";
        case 0x73: return "plug_vin_query";
        case 0x91: return "remote_reboot_ack";
        case 0x92: return "remote_reboot";
        case 0x93: return "remote_update_ack";
        case 0x94: return "remote_update";
        case 0x95: return "remote_log_request_ack";
        case 0x96: return "remote_log_request";
        case 0x55: return "time_sync_ack";
        case 0x56: return "time_sync_set";
        case 0x57: return "fee_model_set_ack";
        case 0x58: return "fee_model_set";
        case 0xA1: return "merge_charge_apply";
        case 0xA2: return "merge_charge_apply_ack";
        case 0xA3: return "merge_start_reply";
        case 0xA4: return "remote_merge_start";
        case 0xE1: return "discharge_start_ack";
        case 0xE2: return "discharge_start_cmd";
        case 0xE3: return "discharge_stop_ack";
        case 0xE4: return "discharge_stop_cmd";
        case 0xE5: return "discharge_cmd_result";
        case 0xE6: return "read_discharge_info";
        case 0xE7: return "discharge_info";
        case 0xE8: return "discharge_record_confirm";
        case 0xE9: return "discharge_trade_record";
        case 0xF0: return "qr_code_set";
        case 0xF1: return "qr_code_set_ack";
        default: return "unknown";
        }
    }
}

CommProcess::CommProcess()
    : BaseProcess(PROC_COMMUNICATION, "tcu_comm")
    , m_logSender("tcu_comm_ykc")
    , m_seq(0)
    , m_platformSeq(0)
    , m_platformConnected(false)
    , m_tcpFd(-1)
    , m_loginState(LOGIN_IDLE)
    , m_lastChargeInfoReport(std::chrono::steady_clock::now())
    , m_lastPeriodicSetConfigPublish(std::chrono::steady_clock::now())
    , m_platformOfflineTimeoutReported(false)
    , m_heartbeatCounter(0)
    , m_sm4SessionKeyReady(false)
    , m_loginCryptoPrepared(false)
    , m_platformOnlineEventActive(false)
    , m_afterOfflineReplayState(LOGIN_REQ_FEE_MODEL_CHECK)
    , m_offlineReplayRequested(false)
    , m_offlineReplaySawRecord(false)
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
    if (m_config.offlineRunMode) {
        // BY LZW: 离线模式下平台链路状态对外恒定为在线。
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
    m_forcePluggedChargeInfoByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_logSender.info("init_completed", std::string("gun_count=") + std::to_string(m_config.gunCount));
    return true;
}

void CommProcess::doRun()
{
    m_running = true;
    // BY LZW: 本地/daemon 喂狗频率统一控制为 5 秒一次。
    auto lastFeedTime = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    while (m_running.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastFeedTime >= std::chrono::seconds(5)) {
            feedWatchdog();
            feedDaemonWatchdog();
            lastFeedTime = now;
        }
        // BY LZW: 运行期间每30秒补发一次每枪 setConfig（retain），便于订阅端周期获取最新配置。
        if (now - m_lastPeriodicSetConfigPublish >= std::chrono::seconds(30)) {
            publishInitialSetConfig();
            m_lastPeriodicSetConfigPublish = now;
        }
        // BY LZW: 平台 TCP 链路维护与登录流程状态机驱动。
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
    m_config.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_comm_ykc");
    m_config.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");
    m_config.biasNo = cfg.getInt(section, "bias_no", 0);
    m_config.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_config.mqttPassword = cfg.getString(section, "mqtt_password", "");
    m_config.masterHost = cfg.getString(section, "master_host", "127.0.0.1");
    m_config.masterPort = cfg.getInt(section, "master_port", 9000);
    m_config.cdzNo = cfg.getString(section, "cdz_no", "CDZ000001");
    m_config.loginId = cfg.getString(section, "login_id", "");
    m_config.macAddr = cfg.getString(section, "mac_addr", "");
    m_config.factoryCreditCode = cfg.getString(section, "factory_credit_code", "");
    m_config.sm2PublicKey = cfg.getString(section, "sm2_public_key", "");
    m_config.ylcChargeProtocolVersion = cfg.getString(section, "ylc_charge_protocol_version", "1.6");
    m_config.ylcDischargeProtocolVersion = cfg.getString(section, "ylc_discharge_protocol_version", "1.6.1");
    m_config.ylcProgramVersion = cfg.getString(section, "ylc_program_version", "1.0.0");
    m_config.ylcNetworkType = cfg.getInt(section, "ylc_network_type", 0);
    m_config.ylcOperatorId = cfg.getInt(section, "ylc_operator_id", 0);
    m_config.ylcSimIccid = cfg.getString(section, "ylc_sim_iccid", "");
    m_config.ylcEncryptEnable = (cfg.getInt(section, "ylc_encrypt_enable", 0) != 0);
    m_config.ylc3desKey = cfg.getString(section, "ylc_3des_key", "");
    m_config.ylc3desMode = cfg.getString(section, "ylc_3des_mode", "");
    m_config.ylc3desIv = cfg.getString(section, "ylc_3des_iv", "");
    m_config.ylc3desPadding = cfg.getString(section, "ylc_3des_padding", "");
    m_config.ylcCrcOnCiphertext = cfg.getString(section, "ylc_crc_on_ciphertext", "");
    if (m_config.ylcEncryptEnable) {
        const std::string msg = "3DES parameters are not confirmed; YLC refuses to start with ylc_encrypt_enable=1";
        std::cerr << "[Comm][YLC] " << msg << std::endl;
        m_logSender.error("ylc_3des_config_unsupported", msg);
        return false;
    }
    if (!m_config.ylc3desKey.empty() || !m_config.ylc3desMode.empty() ||
        !m_config.ylc3desIv.empty() || !m_config.ylc3desPadding.empty() ||
        !m_config.ylcCrcOnCiphertext.empty()) {
        m_logSender.info("ylc_3des_config_reserved",
                         std::string("enable=0 key_set=") + (m_config.ylc3desKey.empty() ? "0" : "1") +
                         " mode_set=" + (m_config.ylc3desMode.empty() ? "0" : "1") +
                         " iv_set=" + (m_config.ylc3desIv.empty() ? "0" : "1") +
                         " padding_set=" + (m_config.ylc3desPadding.empty() ? "0" : "1") +
                         " crc_on_ciphertext_set=" + (m_config.ylcCrcOnCiphertext.empty() ? "0" : "1"));
    }
    m_config.feeDbPath = cfg.getString(section, "fee_db_path", "/mnt/nandflash/data/feemodel.db");
    m_config.tcpReconnectSec = cfg.getInt(section, "tcp_reconnect_sec", 3);
    // BY LZW: YLC 骨架沿用配置心跳周期，协议实现阶段再按平台要求收敛默认值。
    m_config.tcpHeartbeatSec = cfg.getInt(section, "tcp_heartbeat_sec", 10);
    m_config.loginRetrySec = cfg.getInt(section, "login_retry_sec", 10);
    m_config.offlineRunMode = (cfg.getInt(section, "offline_run_mode", 0) != 0);
    m_config.debugTcp = (cfg.getInt(section, "debug", 0) != 0);

    m_config.gunIdList.clear();
    m_config.gunTypeList.clear();
    m_config.gunQrCodeList.clear();
    m_gunRuntimeData.clear();
    m_feeModelByGun.clear();
    m_tradeRecordFeeModelByGun.clear();
    m_tradeRecordOriginalByProtocolKey.clear();
    m_ylcFeeModelNoByGun.clear();
    m_config.gunIdList.reserve(m_config.gunCount);
    m_config.gunTypeList.reserve(m_config.gunCount);
    m_config.gunQrCodeList.reserve(m_config.gunCount);
    m_gunRuntimeData.reserve(m_config.gunCount);
    m_feeModelByGun.reserve(m_config.gunCount);
    m_tradeRecordFeeModelByGun.reserve(m_config.gunCount);
    m_ylcFeeModelNoByGun.reserve(m_config.gunCount);
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
        m_tradeRecordFeeModelByGun.push_back(FeeModel());
        m_ylcFeeModelNoByGun.push_back("0000");
    }
    // BY LZW: YLC重启后先从本地DB恢复最新计费模型，避免平台0x34早于0x58/0x0A导致启动误拒。
    preloadLatestFeeModelFromDb();

    if (!m_config.macAddr.empty() && !isHexString(m_config.macAddr, 24)) {
        m_logSender.warn("invalid_mac_addr", m_config.macAddr);
    }
    if (!m_config.factoryCreditCode.empty() && m_config.factoryCreditCode.size() > 32) {
        m_logSender.warn("factory_credit_code_too_long", m_config.factoryCreditCode);
    }
    // BY LZW: YLC 首版默认明文登录，不要求 SM2 公钥；该字段仅作为 Zhongshihua2.0 骨架兼容项保留。
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
    // BY LZW: MQTT重连后主动发布一次每枪setConfig（retain）。
    publishInitialSetConfig();
    // BY LZW: 仅在已确认平台在线时补发在线事件，避免把“尚未完成登录”误判成平台离线。
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
    return m_mqtt.publish(outTopic, ensureGunField(payload, gun), 1, false);
}

bool CommProcess::handleLogicEventForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun < m_gunRuntimeData.size()) {
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root && cJSON_IsObject(root)) {
            cJSON* evt = cJSON_GetObjectItem(root, "event");
            if (!cJSON_IsString(evt)) {
                evt = cJSON_GetObjectItem(root, "type");
            }
            cJSON* data = cJSON_GetObjectItem(root, "data");
            // BY LZW: HMI/local card discharge enters through tcu_logic, not YLC 0xE2.
            // Cache auth_basis.v2g early so later start_complete/feeData/update_record
            // without discharge flags can still be routed to 0xE7/0xE9.
            if (cJSON_IsString(evt) &&
                std::strcmp(evt->valuestring, "auth_basis") == 0 &&
                cJSON_IsObject(data)) {
                GunRuntimeData& rd = m_gunRuntimeData[gun];
                bool runtimeChanged = false;
                bool isDischarge = rd.lastBusinessIsDischarge;
                if (jsonHasDischargeFlag(root, data)) {
                    isDischarge = jsonIsDischargePayload(root, data);
                    if (rd.lastBusinessIsDischarge != isDischarge) {
                        rd.lastBusinessIsDischarge = isDischarge;
                        runtimeChanged = true;
                    }
                }

                // BY ZF: Keep local-card auth trade number in runtime cache so 0x13 matches 0x3B.
                std::string authTradeNo = jsonGetString(data, "orderNo");
                const char* authTradeNoSource = "orderNo";
                if (authTradeNo.empty()) {
                    authTradeNo = jsonGetString(data, "tradeNo");
                    authTradeNoSource = "tradeNo";
                }
                if (authTradeNo.empty()) {
                    authTradeNo = jsonGetString(data, "preTradeNo");
                    authTradeNoSource = "preTradeNo";
                }
                if (!authTradeNo.empty() && rd.orderNo != authTradeNo) {
                    rd.orderNo = authTradeNo;
                    runtimeChanged = true;
                }

                if (runtimeChanged && gun < m_runtimeChangedByGun.size()) {
                    m_runtimeChangedByGun[gun] = 1;
                }
                m_logSender.info("logic_auth_basis",
                                 std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                                 ",isDischarge=" + (rd.lastBusinessIsDischarge ? "1" : "0") +
                                 ",tradeNoSource=" + (authTradeNo.empty() ? "none" : authTradeNoSource) +
                                 ",tradeNo=" + (authTradeNo.empty() ? rd.orderNo : authTradeNo));
            }
            //更新充电状态
            if (cJSON_IsString(evt) && std::strcmp(evt->valuestring, "state_change") == 0 && cJSON_IsObject(data)) {
                cJSON* to = cJSON_GetObjectItem(data, "to");
                if (cJSON_IsString(to) && to->valuestring) {
                    uint8_t mappedStatus = 0x02;
                    // BY LZW: tcu_logic 状态 -> YLC 0x13 状态：离线00/故障01/空闲02/充电03。
                    if (std::strcmp(to->valuestring, "IDLE") == 0) {
                        mappedStatus = 0x02;
                    } else if (std::strcmp(to->valuestring, "PREPARE") == 0) {
                        mappedStatus = 0x02;
                    } else if (std::strcmp(to->valuestring, "STARTING") == 0) {
                        mappedStatus = 0x02;
                    } else if (std::strcmp(to->valuestring, "CHARGING") == 0) {
                        mappedStatus = 0x03;
                    } else if (std::strcmp(to->valuestring, "STOPPING") == 0) {
                        mappedStatus = 0x03;
                    } else if (std::strcmp(to->valuestring, "ERROR") == 0) {
                        mappedStatus = 0x01;
                    } else if (std::strcmp(to->valuestring, "STOPPED") == 0) {
                        mappedStatus = 0x02;
                    } else if (std::strcmp(to->valuestring, "OFFLINE") == 0) {
                        mappedStatus = 0x00;
                    }
                    if (m_gunRuntimeData[gun].gunStatus != mappedStatus) {
                        m_gunRuntimeData[gun].gunStatus = mappedStatus;
                        if (gun < m_runtimeChangedByGun.size()) {
                            m_runtimeChangedByGun[gun] = 1;
                        }
                    }
                }
            }
            // BY LZW: 独立插枪VIN上报事件 -> 平台0x71，不复用0x31即插即充鉴权申请。
            if (cJSON_IsString(evt) &&
                (std::strcmp(evt->valuestring, "plug_vin_report") == 0 ||
                 std::strcmp(evt->valuestring, "vin_info") == 0) &&
                cJSON_IsObject(data)) {
                (void)handlePlugVinReportEvent(gun, data);
            }
            // BY LZW: 充电记录上报事件 -> 平台0x3B上传交易记录。
            if (cJSON_IsString(evt) &&
                std::strcmp(evt->valuestring, "update_record") == 0 &&
                cJSON_IsObject(data)) {
                refreshTradeRecordFeeModelCache(gun, data);
                std::vector<uint8_t> body;
                // BY LZW: update_record may not carry v2g/isDischarge; keep cached business type when absent.
                const bool isDischarge = jsonHasDischargeFlag(root, data)
                        ? jsonIsDischargePayload(root, data)
                        : m_gunRuntimeData[gun].lastBusinessIsDischarge;
                m_gunRuntimeData[gun].lastBusinessIsDischarge = isDischarge;
                const std::string tradeNo = jsonGetString(data, "tradeNo");
                if (isDischarge) {
                    if (buildDischargeRecordBodyFromUpdateRecord(gun, data, body) && !body.empty()) {
                        if (sendAndTrackTradeRecord(gun, kCmdDischargeTradeRecord, tradeNo, body)) {
                            trackOfflineReplayTradeIfNeeded(tradeNo);
                        }
                    }
                } else {
                    if (buildChargeRecordBodyFromUpdateRecord(gun, data, body) && !body.empty()) {
                        if (sendAndTrackTradeRecord(gun, kCmdUploadTradeRecord, tradeNo, body)) {
                            trackOfflineReplayTradeIfNeeded(tradeNo);
                        }
                    }
                }
            }
            // BY LZW: 即插即充鉴权请求中，0x31/0x32 单枪VIN鉴权已降级到二期；当前仅保留并充二期骨架。
            if (cJSON_IsString(evt) &&
                std::strcmp(evt->valuestring, "plug_and_charge_auth_request") == 0 &&
                cJSON_IsObject(data)) {
                int mergeChargeFlag = 0;
                (void)jsonGetInt(data, "mergeChargeFlag", mergeChargeFlag);
                if (mergeChargeFlag != 0) {
                    if (!kYlcEnablePhase2MergeCharge) {
                        m_logSender.info("platform_merge_vin_start_apply", "phase2_disabled");
                        cJSON_Delete(root);
                        return true;
                    }
                    const uint8_t leftGun = static_cast<uint8_t>(gun & static_cast<uint8_t>(~0x01));
                    const uint8_t rightGun = static_cast<uint8_t>(leftGun + 1);
                    bool sentAny = false;

                    const std::vector<uint8_t> leftBody = buildMergeVinStartApplyBody(leftGun, data);
                    if (!leftBody.empty()) {
                        (void)sendPlatformFrame(kCmdMergeChargeApply, leftBody);
                        sentAny = true;
                    }

                    if (rightGun < m_gunRuntimeData.size()) {
                        const std::vector<uint8_t> rightBody = buildMergeVinStartApplyBody(rightGun, data);
                        if (!rightBody.empty()) {
                            (void)sendPlatformFrame(kCmdMergeChargeApply, rightBody);
                            sentAny = true;
                        }
                    }

                    if (!sentAny) {
                        m_logSender.warn("platform_vin_start_apply", "build_merge_body_fail");
                    }
                } else {
                    m_logSender.info("platform_vin_start_apply", "phase2_disabled");
                }
            }
            cJSON_Delete(root);
        } else if (root) {
            cJSON_Delete(root);
        }
    }
    // BY LZW: 平台上报发送链路后续在此扩展。
    return true;
}

bool CommProcess::handleLogicFeeForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun < m_gunRuntimeData.size()) {
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root && cJSON_IsObject(root)) {
            cJSON* data = cJSON_GetObjectItem(root, "data");
            if (cJSON_IsObject(data)) {
                if (jsonHasDischargeFlag(root, data)) {
                    m_gunRuntimeData[gun].lastBusinessIsDischarge = jsonIsDischargePayload(root, data);
                }
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
    // BY LZW: 预留平台上报发送链路。
    return true;
}

bool CommProcess::handlePileDataForPlatform(uint8_t gun, const std::string& payload)
{
    if (gun < m_gunRuntimeData.size()) {
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root && cJSON_IsObject(root)) {
            cJSON* type = cJSON_GetObjectItem(root, "type");
            cJSON* data = cJSON_GetObjectItem(root, "data");
            if (cJSON_IsString(type) && type->valuestring &&
                (std::strcmp(type->valuestring, "parking_lock_status") == 0 ||
                 std::strcmp(type->valuestring, "parkingLock") == 0) &&
                cJSON_IsObject(data)) {
                if (kYlcEnablePhase2ParkingLock) {
                    (void)handleParkingLockStatusEvent(gun, data);
                } else {
                    m_logSender.info("platform_parking_lock_status", "phase2_disabled");
                }
                cJSON_Delete(root);
                return true;
            }
            if (cJSON_IsString(type) && std::strcmp(type->valuestring, "yx") == 0 && cJSON_IsObject(data)) {
                GunRuntimeData& rd = m_gunRuntimeData[gun];
                bool changed = false;
                if (jsonHasDischargeFlag(root, data)) {
                    rd.lastBusinessIsDischarge = jsonIsDischargePayload(root, data);
                }

                // BY LZW: 遥信字段更新辅助：仅当新值与缓存值不同才写入，并标记changed=true用于触发“变位即送”。
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
                {
                    cJSON* v = cJSON_GetObjectItem(data, "vehicleConnectStatus");
                    if (v && cJSON_IsNumber(v)) {
                        int n = v->valueint;
                        if (n < 0) n = 0;
                        if (n > 255) n = 255;
                        const uint8_t nv = static_cast<uint8_t>(n);
                        const bool connectChanged = (rd.yxVehicleConnectStatus != nv) ||
                                                    !rd.yxVehicleConnectStatusValid;
                        if (connectChanged) {
                            rd.yxVehicleConnectStatus = nv;
                            rd.yxVehicleConnectStatusValid = true;
                            changed = true;
                            // BY LZW: 0x34未插枪失败后，60秒内检测到重新连接则补送0x33成功。
                            handleRemoteStartVehicleReconnect(gun);
                        }
                    }
                }
                updateU8("vinReq", rd.yxVinReq);
                {
                    cJSON* v = cJSON_GetObjectItem(data, "gunSeatStatus");
                    if (v && cJSON_IsNumber(v)) {
                        int n = v->valueint;
                        if (n < 0) n = 0;
                        if (n > 255) n = 255;
                        const uint8_t nv = static_cast<uint8_t>(n);
                        if (rd.yxGunSeatStatus != nv || !rd.yxGunSeatStatusValid) {
                            rd.yxGunSeatStatus = nv;
                            rd.yxGunSeatStatusValid = true;
                            changed = true;
                        }
                    }
                }
                updateU8("electronicLockStatus", rd.yxElectronicLockStatus);
                updateU8("dcContactorStatus", rd.yxDcContactorStatus);
                updateU8("otherFault", rd.yxOtherFault);

                const char* fineFaultKeys[] = {
                    "smokeFault", "acInputBreakerFault", "dcBusContactorFault", "dcBusFuseFault",
                    "electronicLockFault", "fanFault", "lightningArresterFault", "insulationMonitorFault",
                    "insulationMonitorAlarm", "batteryReverseFault", "guideFault", "guideVoltageAbnormal",
                    "pileOverTempFault", "cabinetOverTempFault", "interfaceOverTempFault", "gunOverTempAlarm",
                    "gunNotReturnAlarm", "bmsCommFault", "inputOverVoltageFault", "inputUnderVoltageFault",
                    "dcBusOverVoltageFault", "dcBusUnderVoltageFault", "dcBusOverCurrentFault",
                    "moduleFault", "moduleCommFault", "moduleAcInputFault", "moduleAcOverVoltageFault",
                    "moduleAcUnderVoltageFault", "moduleAcPhaseLossFault", "moduleDcOverVoltageFault",
                    "moduleDcUnderVoltageFault", "moduleDcOverCurrentFault", "moduleDcShortFault",
                    "moduleOverTempFault", "moduleFanFault", "moduleStartFail", "moduleShutdownFail",
                    "moduleAddrAbnormal", "acInputContactorFault", "acInputContactorStickFault",
                    "dcContactorStickFault", "auxPowerFault", "noIdleModuleFault", "meterCommFault",
                    "meterDataAbnormal", "cpFault", "emergencyStopFault",
                    "bridgeContactorFault", "bridgeContactorStickFault", "powerCtrlCommFault",
                    "switchModuleCommFault", "bmsFaultByCtrl", "bmsSendFaultInfo", "peBreakFault",
                    "pileDoorFault", "cabinetDoorFault", "dischargeFault", "pileOverTempAlarm",
                    "cabinetOverTempAlarm", "powerCtrlCommTimeout", "prechargeVoltageFail",
                    // BY LZW: YLC附录13硬件故障扩展预留输入字段；当前上游未必发布，发布后可直接参与映射。
                    "leakageFault", "groundFault", "shortCircuitFault", "leakageSelfCheckFault",
                    "diodeCheckAbnormal", "cc1Fault", "pileCommTimeoutFault", "bcpParamMismatchFault",
                    "batteryVoltageMismatchFault", "outputVoltageOverMaxFault", "networkOfflineFault",
                    "fourGOfflineFault", "moduleLongIdleFault", "moduleNoCurrentFault",
                    "insulationOutsideVoltageFault", "bhmVoltageTooLowFault", "bsmBatteryAbnormalFault",
                    "broAbnormalFault", "modulePathAOverCurrentFault", "modulePathBOverCurrentFault",
                    "modulePathUnbalanceFault", "modulePrimaryOverCurrentFault", "moduleCenterVoltageFault",
                    "dcHardwireFault", "batteryOverVoltageFault", "batteryUnderVoltageFault",
                    "sciCommFault", "canCommFault", "contactorSwitchFault", "moduleOverTemp2Fault",
                    "workModeFault", "fastSwitchFault", "reverseInputOverCurrentFault", "acInputUnbalanceFault",
                    "acFrequencyFault", "inputOverCurrentFault", "inputOcpFault", "dcBusUnbalanceFault",
                    "pfcCommFault", "totalBusOverVoltageFault", "totalBusUnderVoltageFault",
                    "acInputPeakOverVoltageFault", "cardReaderCommFault", "rc10CommFault",
                    "pfcOverTempFault", "modeSwitchFault", "pfcHardwireFault", "chargeCurrentOverBmsLimitFault",
                    "eepromFault", "pileLockedFault", "vinMatchFail"
                };
                for (size_t i = 0; i < sizeof(fineFaultKeys) / sizeof(fineFaultKeys[0]); ++i) {
                    cJSON* v = cJSON_GetObjectItem(data, fineFaultKeys[i]);
                    if (v && cJSON_IsNumber(v)) {
                        int n = v->valueint;
                        if (n < 0) n = 0;
                        if (n > 255) n = 255;
                        const uint8_t nv = static_cast<uint8_t>(n);
                        if (rd.yxFaultFields[fineFaultKeys[i]] != nv) {
                            rd.yxFaultFields[fineFaultKeys[i]] = nv;
                            changed = true;
                        }
                    }
                }
                if (changed && gun < m_runtimeChangedByGun.size()) {
                    m_runtimeChangedByGun[gun] = 1;
                }
            }
            // BY LZW: 缓存遥测电压/电流（来自 yc）。
            if (cJSON_IsString(type) && std::strcmp(type->valuestring, "yc") == 0 && cJSON_IsObject(data)) {
                GunRuntimeData& rd = m_gunRuntimeData[gun];
                if (jsonHasDischargeFlag(root, data)) {
                    rd.lastBusinessIsDischarge = jsonIsDischargePayload(root, data);
                }
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
    // BY LZW: 平台上报发送链路后续在此扩展。
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
        (std::strcmp(type->valuestring, "parking_lock_status") == 0 ||
         std::strcmp(type->valuestring, "parkingLock") == 0) &&
        cJSON_IsObject(data)) {
        if (kYlcEnablePhase2ParkingLock) {
            (void)handleParkingLockStatusEvent(gun, data);
        } else {
            m_logSender.info("platform_parking_lock_status", "phase2_disabled");
        }
    } else if (cJSON_IsString(type) && type->valuestring &&
        (std::strcmp(type->valuestring, "plug_vin_report") == 0 ||
         std::strcmp(type->valuestring, "vin_info") == 0) &&
        cJSON_IsObject(data)) {
        (void)handlePlugVinReportEvent(gun, data);
    } else if (cJSON_IsString(type) && type->valuestring &&
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
        rd.startCompleteData.nominalEnergy = getU16("nominalEnergy", 0);
        rd.startCompleteData.soc = getU16("soc", 0);
        rd.startCompleteData.vin = getAscii("vin");
        getBytes("bmsSoftwareVersion", rd.startCompleteData.bmsSoftwareVersion.data(), rd.startCompleteData.bmsSoftwareVersion.size());
        rd.startCompleteData.insulationFault = getU8("insulationMonitorFault", 0);
        rd.stopCompleteFields.clear();

        // BY LZW: pile_controller start_complete has no v2g/isDischarge, so preserve YLC cached discharge flag.
        const bool isDischarge = jsonHasDischargeFlag(root, data)
                ? jsonIsDischargePayload(root, data)
                : rd.lastBusinessIsDischarge;
        rd.lastBusinessIsDischarge = isDischarge;
        if (isDischarge) {
            rd.lastDischargeCmdType = 0x01;
            const std::vector<uint8_t> resultBody = buildDischargeCmdResultBody(gun, 0x01, data);
            if (!resultBody.empty()) {
                (void)sendPlatformFrame(kCmdDischargeCmdResult, resultBody);
            }
        } else {
            const std::vector<uint8_t> body = buildStartChargeResultBody(gun);
            const bool suppressRemoteStartAck = rd.suppressNextStartCompleteRemoteStartAck;
            if (!body.empty() && !suppressRemoteStartAck) {
                (void)sendPlatformFrame(kCmdRemoteStartAck, body);
            } else if (suppressRemoteStartAck) {
                m_logSender.info("platform_remote_start_ack_suppressed",
                                 std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                                 ",tradeNo=" + rd.orderNo);
            }
            rd.suppressNextStartCompleteRemoteStartAck = false;

            const uint8_t ylcFailReason = mapYlcRemoteStartFailReason(static_cast<int>(rd.startCompleteData.failReason));
            const bool startFailUnplug = (rd.startCompleteData.successFlag != 0 && ylcFailReason == 0x05);
            if (startFailUnplug) {
                const bool canWaitReconnect =
                        !rd.remoteStartReconnectWaitActive &&
                        rd.remoteStartReconnectDeadline != std::chrono::steady_clock::time_point() &&
                        std::chrono::steady_clock::now() < rd.remoteStartReconnectDeadline &&
                        !rd.remoteStartReconnectPayload.empty();
                if (canWaitReconnect) {
                    (void)beginRemoteStartReconnectWait(gun, -1, false);
                } else if (!rd.remoteStartReconnectWaitActive) {
                    clearRemoteStartReconnectWait(gun);
                }
            } else {
                clearRemoteStartReconnectWait(gun);
            }
            // BY LZW: YLC 5.2 启动成功后上送 0x15/0x17；启动失败只上送0x33结果。
            if (rd.startCompleteData.successFlag == 0) {
                const std::vector<uint8_t> brmBody = buildBrmBody(gun);
                if (!brmBody.empty()) {
                    (void)sendPlatformFrame(kCmdBrm, brmBody);
                }
                const std::vector<uint8_t> bcpBody = buildBcpBody(gun);
                if (!bcpBody.empty()) {
                    (void)sendPlatformFrame(kCmdBcp, bcpBody);
                }
            }
        }
    } else if (cJSON_IsString(type) && type->valuestring &&
        (std::strcmp(type->valuestring, "charge_error") == 0 ||
         std::strcmp(type->valuestring, "gbt_error") == 0) &&
        cJSON_IsObject(data)) {
        const std::vector<uint8_t> errBody = buildChargeErrorBody(gun, data);
        if (!errBody.empty()) {
            (void)sendPlatformFrame(kCmdChargeError, errBody);
        }
    } else if (cJSON_IsString(type) && type->valuestring &&
               std::strcmp(type->valuestring, "stop_complete") == 0 &&
               cJSON_IsObject(data)) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        rd.stopCompleteFields.clear();
        const char* stopKeys[] = {
            "bmsStopReason", "bmsChargeFaultReason", "bmsStopErrorReason",
            "chargerStopReason", "chargerStopFaultReason", "chargerStopErrorReason",
            "timeoutChargerId", "timeoutChargerId00", "timeoutChargerIdAA",
            "timeoutTimeSync", "timeoutChargeReady", "timeoutChargeStatus",
            "timeoutChargeStop", "timeoutChargeStat", "timeoutBmsVehicleId",
            "timeoutBatteryParams", "timeoutBmsReady", "timeoutBatteryStatus",
            "timeoutBatteryReq", "timeoutBmsStop", "timeoutBmsStat",
            "timeoutCml", "timeoutBcl", "timeoutBcs", "timeoutBsm", "timeoutBst",
            "stopSoc", "cellMinVoltage", "cellMaxVoltage", "reason"
        };
        for (size_t i = 0; i < sizeof(stopKeys) / sizeof(stopKeys[0]); ++i) {
            int v = 0;
            if (jsonGetInt(data, stopKeys[i], v)) {
                if (v < 0) v = 0;
                if (v > 0xFFFF) v = 0xFFFF;
                rd.stopCompleteFields[stopKeys[i]] = static_cast<uint16_t>(v);
            }
        }

        // BY LZW: pile_controller stop_complete has no v2g/isDischarge, so preserve YLC cached discharge flag.
        const bool isDischarge = jsonHasDischargeFlag(root, data)
                ? jsonIsDischargePayload(root, data)
                : rd.lastBusinessIsDischarge;
        rd.lastBusinessIsDischarge = isDischarge;
        if (isDischarge) {
            rd.lastDischargeCmdType = 0x02;
            const std::vector<uint8_t> resultBody = buildDischargeCmdResultBody(gun, 0x02, data);
            if (!resultBody.empty()) {
                (void)sendPlatformFrame(kCmdDischargeCmdResult, resultBody);
            }
        } else {
            // BY LZW: 停止完成后补送0x19结束阶段报文（BSD/CSD汇总）。
            const std::vector<uint8_t> endStageBody = buildChargeEndStageBody(gun, data);
            if (!endStageBody.empty()) {
                (void)sendPlatformFrame(kCmdChargeEndStage, endStageBody);
            }
            const std::vector<uint8_t> errBody = buildChargeErrorBody(gun, data);
            if (!errBody.empty()) {
                (void)sendPlatformFrame(kCmdChargeError, errBody);
            }
            // BY LZW: 停止完成后上送0x1D BST报文。
            const std::vector<uint8_t> bstBody = buildBstBody(gun, data);
            if (!bstBody.empty()) {
                (void)sendPlatformFrame(kCmdBst, bstBody);
            }
            // BY LZW: 停止完成后上送0x21 CST报文。
            const std::vector<uint8_t> cstBody = buildCstBody(gun, data);
            if (!cstBody.empty()) {
                (void)sendPlatformFrame(kCmdCst, cstBody);
            }
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

    body.clear();
    body.reserve(158U);

    const std::string tradeNo = jsonGetString(data, "tradeNo");
    std::string vinCode = jsonGetString(data, "vinCode");
    if (vinCode.empty()) {
        vinCode = jsonGetString(data, "vin");
    }
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
    const std::string feeModelIdFromRecord = jsonGetString(data, "feeModelId");
    cJSON* partElectArr = cJSON_GetObjectItem(data, "partElect");
    cJSON* chargeFeeArr = cJSON_GetObjectItem(data, "chargeFee");
    cJSON* serviceFeeArr = cJSON_GetObjectItem(data, "serviceFee");

    // BY LZW: 0x3B交易流水号不再强制校验32位/桩号/枪号，平台0x34下发什么交易号就沿用什么交易号组帧。
    if (tradeNo.empty()) {
        m_logSender.warn("ylc_charge_record_empty_trade_no",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)));
        return false;
    }
    if (!isValidYlcRecordTime(startTime) || !isValidYlcRecordTime(endTime)) {
        m_logSender.warn("ylc_charge_record_invalid_time",
                         std::string("tradeNo=") + tradeNo +
                         ",start=" + std::to_string(static_cast<unsigned long long>(startTime)) +
                         ",end=" + std::to_string(static_cast<unsigned long long>(endTime)));
        return false;
    }

    auto arraySizeOrZero = [](cJSON* arr) -> int {
        return cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    };
    auto getArrayNumber = [](cJSON* arr, int index) -> double {
        if (!cJSON_IsArray(arr) || index < 0 || index >= cJSON_GetArraySize(arr)) {
            return 0.0;
        }
        cJSON* item = cJSON_GetArrayItem(arr, index);
        return (item && cJSON_IsNumber(item)) ? item->valuedouble : 0.0;
    };

    int periodCount = arraySizeOrZero(partElectArr);
    if (periodCount > 0 && arraySizeOrZero(chargeFeeArr) > 0) {
        periodCount = std::min(periodCount, arraySizeOrZero(chargeFeeArr));
    }
    if (periodCount > 0 && arraySizeOrZero(serviceFeeArr) > 0) {
        periodCount = std::min(periodCount, arraySizeOrZero(serviceFeeArr));
    }
    if (periodCount > 48) {
        periodCount = 48;
    }

    const FeeModel* matchedFeeModel = nullptr;
    const char* matchedFeeModelSource = "none";
    bool recordCacheHasRequestedId = false;
    if (gun < m_tradeRecordFeeModelByGun.size()) {
        const FeeModel& recordFeeModel = m_tradeRecordFeeModelByGun[gun];
        recordCacheHasRequestedId =
            (!feeModelIdFromRecord.empty() &&
             !recordFeeModel.feeModelId.empty() &&
             feeModelIdFromRecord == recordFeeModel.feeModelId);
        if (!feeModelIdFromRecord.empty() &&
            !recordFeeModel.feeModelId.empty() &&
            feeModelIdFromRecord == recordFeeModel.feeModelId &&
            recordFeeModel.segFlag.size() >= static_cast<size_t>(periodCount) &&
            recordFeeModel.chargeFee.size() >= static_cast<size_t>(periodCount) &&
            recordFeeModel.serviceFee.size() >= static_cast<size_t>(periodCount)) {
            matchedFeeModel = &recordFeeModel;
            matchedFeeModelSource = "trade_record_cache";
        }
    }
    if (gun < m_feeModelByGun.size()) {
        const FeeModel& localFeeModel = m_feeModelByGun[gun];
        if (!matchedFeeModel &&
            !recordCacheHasRequestedId &&
            !feeModelIdFromRecord.empty() &&
            !localFeeModel.feeModelId.empty() &&
            feeModelIdFromRecord == localFeeModel.feeModelId &&
            localFeeModel.segFlag.size() >= static_cast<size_t>(periodCount) &&
            localFeeModel.chargeFee.size() >= static_cast<size_t>(periodCount) &&
            localFeeModel.serviceFee.size() >= static_cast<size_t>(periodCount)) {
            matchedFeeModel = &localFeeModel;
            matchedFeeModelSource = "runtime_cache";
        }
    }
    if (!feeModelIdFromRecord.empty()) {
        std::ostringstream oss;
        oss << "gun=" << static_cast<int>(gun)
            << ",req=" << feeModelIdFromRecord
            << ",periodCount=" << periodCount
            << ",source=" << matchedFeeModelSource;
        if (matchedFeeModel) {
            oss << ",hit=" << matchedFeeModel->feeModelId
                << ",timeNum=" << static_cast<int>(matchedFeeModel->timeNum)
                << ",timeSeg=" << matchedFeeModel->timeSeg.size();
        }
        m_logSender.info("trade_record_fee_model_match", oss.str());
        if (m_config.debugTcp) {
            std::cout << "[Comm][FEE][MATCH] " << oss.str() << std::endl;
        }
    }
    if (!feeModelIdFromRecord.empty() && std::strcmp(matchedFeeModelSource, "runtime_cache") == 0) {
        m_logSender.warn("trade_record_fee_model_runtime_fallback",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                         ",feeModelId=" + feeModelIdFromRecord);
    } else if (!feeModelIdFromRecord.empty() && !matchedFeeModel) {
        m_logSender.warn("trade_record_fee_model_miss_fallback",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                         ",feeModelId=" + feeModelIdFromRecord +
                         ",periodCount=" + std::to_string(periodCount));
    }

    double catEnergy[4] = {0.0, 0.0, 0.0, 0.0};
    double catChargeFee[4] = {0.0, 0.0, 0.0, 0.0};
    double catServiceFee[4] = {0.0, 0.0, 0.0, 0.0};
    bool hasCategoryDetail = false;

    auto addCategory = [&](size_t idx, double energy, double chargeFee, double serviceFee) {
        if (idx > 3U) {
            idx = 2U; // 平
        }
        catEnergy[idx] += energy;
        catChargeFee[idx] += chargeFee;
        catServiceFee[idx] += serviceFee;
        hasCategoryDetail = true;
    };

    if (periodCount == 4) {
        // BY LZW: update_record 若已给出4项，按尖/峰/平/谷汇总直接使用，不再按48段模型二次聚合。
        for (int i = 0; i < 4; ++i) {
            addCategory(static_cast<size_t>(i),
                        getArrayNumber(partElectArr, i),
                        getArrayNumber(chargeFeeArr, i),
                        getArrayNumber(serviceFeeArr, i));
        }
    } else if (periodCount > 0 && matchedFeeModel) {
        for (int i = 0; i < periodCount; ++i) {
            unsigned int flag = matchedFeeModel->segFlag[static_cast<size_t>(i)];
            if (flag < 1U || flag > 4U) {
                flag = 3U;
            }
            addCategory(static_cast<size_t>(flag - 1U),
                        getArrayNumber(partElectArr, i),
                        getArrayNumber(chargeFeeArr, i),
                        getArrayNumber(serviceFeeArr, i));
        }
    } else if (periodCount > 0) {
        for (int i = 0; i < std::min(periodCount, 4); ++i) {
            addCategory(static_cast<size_t>(i),
                        getArrayNumber(partElectArr, i),
                        getArrayNumber(chargeFeeArr, i),
                        getArrayNumber(serviceFeeArr, i));
        }
    }
    if (!hasCategoryDetail) {
        addCategory(2U, totalElect, totalPowerCost, totalServCost);
    }

    uint32_t categoryPrice[4] = {0U, 0U, 0U, 0U};
    bool priceSet[4] = {false, false, false, false};
    if (matchedFeeModel) {
        const size_t n = std::min(matchedFeeModel->segFlag.size(),
                                  std::min(matchedFeeModel->chargeFee.size(), matchedFeeModel->serviceFee.size()));
        for (size_t i = 0; i < n; ++i) {
            unsigned int flag = matchedFeeModel->segFlag[i];
            if (flag < 1U || flag > 4U) {
                continue;
            }
            const size_t idx = static_cast<size_t>(flag - 1U);
            if (!priceSet[idx]) {
                categoryPrice[idx] =
                    static_cast<uint32_t>(matchedFeeModel->chargeFee[i] + matchedFeeModel->serviceFee[i]);
                priceSet[idx] = true;
            }
        }
    }

    auto derivePriceRaw = [](uint32_t amountRaw, uint32_t energyRaw) -> uint32_t {
        if (energyRaw == 0U) {
            return 0U;
        }
        const uint64_t raw = (static_cast<uint64_t>(amountRaw) * 100000ULL + energyRaw / 2U) /
                             static_cast<uint64_t>(energyRaw);
        return raw > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<uint32_t>(raw);
    };

    uint32_t categoryEnergyRaw[4] = {0U, 0U, 0U, 0U};
    uint32_t categoryAmountRaw[4] = {0U, 0U, 0U, 0U};
    for (int i = 0; i < 4; ++i) {
        categoryEnergyRaw[i] = scaleToU32(catEnergy[i], 10000.0);
        categoryAmountRaw[i] = scaleToU32(catChargeFee[i] + catServiceFee[i], 10000.0);
        if (!priceSet[i]) {
            categoryPrice[i] = derivePriceRaw(categoryAmountRaw[i], categoryEnergyRaw[i]);
        }
    }

    // BY LZW: 0x3B 严格按协议 8.7 固定 31 项字段组帧，body 目标长度 158 字节。
    appendBcdFixed(body, tradeNo, 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(intToBcdByte(gunNo));
    appendCp56Time2aFromYmdHms(body, startTime);
    appendCp56Time2aFromYmdHms(body, endTime);

    for (int i = 0; i < 4; ++i) {
        appendU32LE(body, categoryPrice[i]);
        appendU32LE(body, categoryEnergyRaw[i]);
        appendU32LE(body, categoryEnergyRaw[i]);
        appendU32LE(body, categoryAmountRaw[i]);
    }

    appendU40LE(body, scaleToU64(sumStart, 10000.0));
    appendU40LE(body, scaleToU64(sumEnd, 10000.0));
    appendU32LE(body, scaleToU32(totalElect, 10000.0));
    appendU32LE(body, scaleToU32(totalElect, 10000.0));
    const double totalAmount = (totalCost > 0.0) ? totalCost : (totalPowerCost + totalServCost);
    appendU32LE(body, scaleToU32(totalAmount, 10000.0));
    appendAsciiFixed(body, sanitizeVin17(vinCode), 17, 0x00);

    uint8_t tradeFlag = 0x01; // app启动
    if (startType == 2) tradeFlag = 0x02;      // 卡启动
    else if (startType == 4) tradeFlag = 0x04; // 离线卡启动
    else if (startType == 5) tradeFlag = 0x05; // VIN码启动
    body.push_back(tradeFlag);
    appendCp56Time2aFromYmdHms(body, endTime);

    const uint8_t platformStopReason = mapYlcChargeTradeStopReason(reason);
    body.push_back(platformStopReason);
    if (m_config.debugTcp) {
        std::cout << "[Comm][TRADE][STOP_REASON] gun=" << static_cast<int>(gun)
                  << " mqtt=0x" << std::hex << reason
                  << " platform=0x" << static_cast<int>(platformStopReason)
                  << std::dec << std::endl;
    }

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

    if (gun < m_gunRuntimeData.size()) {
        m_gunRuntimeData[gun].pendingRecordTradeNo = tradeNo;
    }

    if (body.size() != 158U) {
        m_logSender.warn("ylc_charge_record_body_len",
                         std::string("len=") + std::to_string(body.size()));
    }
    return true;
}

bool CommProcess::loadFeeModelFromDbFile(const std::string& feeModelId, FeeModel& feeModel)
{
    feeModel = FeeModel();
    if (feeModelId.empty()) {
        return false;
    }

    const std::string dbPath = m_config.feeDbPath.empty() ? "/mnt/nandflash/data/feemodel.db" : m_config.feeDbPath;
    if (m_config.debugTcp) {
        std::cout << "[Comm][FEE][DB_LOOKUP] feeModelId=" << feeModelId
                  << " dbPath=" << dbPath << std::endl;
    }
    sqlite3* db = NULL;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK || !db) {
        m_logSender.warn("trade_record_fee_model_db_open_fail", feeModelId + "@" + dbPath);
        if (db) {
            sqlite3_close(db);
        }
        return false;
    }

    const char* sql =
        "SELECT feeModelId, timeNum, timeSeg, segFlag, chargeFee, serviceFee "
        "FROM tbFeeModel WHERE feeModelId=? ORDER BY id DESC LIMIT 1";
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK || !stmt) {
        m_logSender.warn("trade_record_fee_model_db_prepare_fail", feeModelId);
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, feeModelId.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto parseUIntList = [](const char* text, std::vector<unsigned int>& out) {
            out.clear();
            if (!text) {
                return;
            }
            std::istringstream iss(text);
            std::string token;
            while (std::getline(iss, token, ';')) {
                if (!token.empty()) {
                    out.push_back(static_cast<unsigned int>(std::strtoul(token.c_str(), NULL, 10)));
                }
            }
        };
        auto parseStrList = [](const char* text, std::vector<std::string>& out) {
            out.clear();
            if (!text) {
                return;
            }
            std::istringstream iss(text);
            std::string token;
            while (std::getline(iss, token, ';')) {
                if (!token.empty()) {
                    while (token.size() < 4U) token.insert(token.begin(), '0');
                    out.push_back(token.substr(0, 4U));
                }
            }
        };

        const char* modelIdText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        feeModel.feeModelId = modelIdText ? modelIdText : feeModelId;
        feeModel.timeNum = static_cast<unsigned char>(sqlite3_column_int(stmt, 1));
        parseStrList(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)), feeModel.timeSeg);
        parseUIntList(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)), feeModel.segFlag);
        parseUIntList(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)), feeModel.chargeFee);
        parseUIntList(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)), feeModel.serviceFee);
        found = (!feeModel.feeModelId.empty() &&
                 feeModel.timeNum > 0 &&
                 !feeModel.timeSeg.empty() &&
                 feeModel.chargeFee.size() >= feeModel.timeSeg.size() &&
                 feeModel.serviceFee.size() >= feeModel.timeSeg.size());
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    if (found) {
        std::ostringstream oss;
        oss << "req=" << feeModelId
            << ",hit=" << feeModel.feeModelId
            << ",timeNum=" << static_cast<int>(feeModel.timeNum)
            << ",timeSeg=" << feeModel.timeSeg.size()
            << ",chargeFee=" << feeModel.chargeFee.size()
            << ",serviceFee=" << feeModel.serviceFee.size();
        m_logSender.info("trade_record_fee_model_db_hit", oss.str());
        if (m_config.debugTcp) {
            std::cout << "[Comm][FEE][DB_HIT] " << oss.str() << std::endl;
        }
    } else {
        m_logSender.warn("trade_record_fee_model_db_miss", feeModelId);
        if (m_config.debugTcp) {
            std::cout << "[Comm][FEE][DB_MISS] feeModelId=" << feeModelId << std::endl;
        }
    }
    return found;
}

bool CommProcess::loadLatestFeeModelFromDbFile(FeeModel& feeModel)
{
    feeModel = FeeModel();
    const std::string dbPath = m_config.feeDbPath.empty() ? "/mnt/nandflash/data/feemodel.db" : m_config.feeDbPath;
    if (m_config.debugTcp) {
        std::cout << "[Comm][FEE][DB_LATEST_LOOKUP] dbPath=" << dbPath << std::endl;
    }

    sqlite3* db = NULL;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK || !db) {
        m_logSender.warn("fee_model_preload_fail", std::string("open_fail@") + dbPath);
        if (db) {
            sqlite3_close(db);
        }
        return false;
    }

    const char* sql =
        "SELECT feeModelId, timeNum, timeSeg, segFlag, chargeFee, serviceFee "
        "FROM tbFeeModel ORDER BY id DESC LIMIT 1";
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK || !stmt) {
        m_logSender.warn("fee_model_preload_fail", "prepare_latest_fail");
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return false;
    }

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto parseUIntList = [](const char* text, std::vector<unsigned int>& out) {
            out.clear();
            if (!text) {
                return;
            }
            std::istringstream iss(text);
            std::string token;
            while (std::getline(iss, token, ';')) {
                if (!token.empty()) {
                    out.push_back(static_cast<unsigned int>(std::strtoul(token.c_str(), NULL, 10)));
                }
            }
        };
        auto parseStrList = [](const char* text, std::vector<std::string>& out) {
            out.clear();
            if (!text) {
                return;
            }
            std::istringstream iss(text);
            std::string token;
            while (std::getline(iss, token, ';')) {
                if (!token.empty()) {
                    while (token.size() < 4U) token.insert(token.begin(), '0');
                    out.push_back(token.substr(0, 4U));
                }
            }
        };

        const char* modelIdText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        feeModel.feeModelId = modelIdText ? modelIdText : "";
        feeModel.timeNum = static_cast<unsigned char>(sqlite3_column_int(stmt, 1));
        parseStrList(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)), feeModel.timeSeg);
        parseUIntList(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)), feeModel.segFlag);
        parseUIntList(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)), feeModel.chargeFee);
        parseUIntList(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)), feeModel.serviceFee);
        found = isYlcFeeModelReady(feeModel);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    if (!found) {
        m_logSender.warn("fee_model_preload_miss", dbPath);
        if (m_config.debugTcp) {
            std::cout << "[Comm][FEE][DB_LATEST_MISS] dbPath=" << dbPath << std::endl;
        }
        feeModel = FeeModel();
    } else if (m_config.debugTcp) {
        std::cout << "[Comm][FEE][DB_LATEST_HIT] feeModelId=" << feeModel.feeModelId
                  << " timeNum=" << static_cast<int>(feeModel.timeNum) << std::endl;
    }
    return found;
}

void CommProcess::preloadLatestFeeModelFromDb()
{
    FeeModel latest;
    if (!loadLatestFeeModelFromDbFile(latest)) {
        return;
    }

    const std::string ylcModelNo = ylcFeeModelNoFromLocalId(latest.feeModelId);
    for (size_t i = 0; i < m_feeModelByGun.size(); ++i) {
        m_feeModelByGun[i] = latest;
        if (i < m_ylcFeeModelNoByGun.size()) {
            m_ylcFeeModelNoByGun[i] = ylcModelNo;
        }
    }

    std::ostringstream oss;
    oss << "feeModelId=" << latest.feeModelId
        << ",ylcFeeModelNo=" << ylcModelNo
        << ",timeNum=" << static_cast<int>(latest.timeNum)
        << ",guns=" << m_feeModelByGun.size();
    m_logSender.info("fee_model_preload_ok", oss.str());
}

void CommProcess::refreshTradeRecordFeeModelCache(uint8_t gun, cJSON* data)
{
    if (!data || gun >= m_tradeRecordFeeModelByGun.size()) {
        return;
    }

    const std::string feeModelId = jsonGetString(data, "feeModelId");
    if (feeModelId.empty()) {
        m_tradeRecordFeeModelByGun[gun] = FeeModel();
        m_logSender.warn("trade_record_fee_model_empty", std::string("gun=") + std::to_string(static_cast<int>(gun)));
        return;
    }

    FeeModel feeModel;
    if (loadFeeModelFromDbFile(feeModelId, feeModel)) {
        m_tradeRecordFeeModelByGun[gun] = feeModel;
        m_logSender.info("trade_record_fee_model_cache_update",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                         ",feeModelId=" + feeModel.feeModelId);
    } else {
        m_tradeRecordFeeModelByGun[gun] = FeeModel();
        m_logSender.warn("trade_record_fee_model_load_fail", feeModelId);
    }
}

bool CommProcess::isValidYlcTradeNo(const std::string& tradeNo, uint8_t gun) const
{
    if (tradeNo.size() != 32U) {
        return false;
    }
    bool allZero = true;
    for (size_t i = 0; i < tradeNo.size(); ++i) {
        if (tradeNo[i] < '0' || tradeNo[i] > '9') {
            return false;
        }
        if (tradeNo[i] != '0') {
            allZero = false;
        }
    }
    if (allZero) {
        return false;
    }

    const std::string pileCode = normalizePileCode14(m_config.cdzNo);
    if (pileCode.size() == 14U && tradeNo.substr(0, 14U) != pileCode) {
        return false;
    }
    char gunNoText[3] = {0};
    std::snprintf(gunNoText, sizeof(gunNoText), "%02u", static_cast<unsigned int>(gun) + 1U);
    return tradeNo.substr(14U, 2U) == gunNoText;
}

bool CommProcess::isValidYlcRecordTime(uint64_t ymdhms) const
{
    if (ymdhms < 20000101000000ULL || ymdhms > 20991231235959ULL) {
        return false;
    }
    char buf[15] = {0};
    std::snprintf(buf, sizeof(buf), "%014llu", static_cast<unsigned long long>(ymdhms));
    auto d2 = [](char a, char b) -> int {
        if (a < '0' || a > '9' || b < '0' || b > '9') {
            return -1;
        }
        return (a - '0') * 10 + (b - '0');
    };
    const int month = d2(buf[4], buf[5]);
    const int day = d2(buf[6], buf[7]);
    const int hour = d2(buf[8], buf[9]);
    const int minute = d2(buf[10], buf[11]);
    const int second = d2(buf[12], buf[13]);
    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }
    static const int kDaysInMonth[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return day <= kDaysInMonth[month - 1];
}

bool CommProcess::sendAndTrackTradeRecord(uint8_t gun,
                                          uint8_t cmd,
                                          const std::string& tradeNo,
                                          const std::vector<uint8_t>& body)
{
    if (tradeNo.empty() || body.empty()) {
        return false;
    }
    if (cmd != kCmdUploadTradeRecord && cmd != kCmdDischargeTradeRecord) {
        return false;
    }
    if (!sendPlatformFrame(cmd, body)) {
        return false;
    }

    PendingTradeRecordUpload pending;
    pending.active = true;
    pending.gun = gun;
    pending.cmd = cmd;
    pending.tradeNo = tradeNo;
    pending.protocolTradeNo = protocolTradeNoFromLocal(tradeNo, 16U);
    pending.body = body;
    pending.sendCount = 1U;
    pending.normalRetryCount = 0U;
    pending.maxNormalRetryCount = 3U;
    pending.finalResendDone = false;
    pending.firstSendTime = std::chrono::steady_clock::now();
    pending.lastSendTime = pending.firstSendTime;
    m_pendingTradeRecordUploads[tradeNo] = pending;

    const std::string protocolKey = tradeRecordUploadKey(cmd, pending.protocolTradeNo);
    std::map<std::string, std::string>::iterator protocolIt = m_tradeRecordOriginalByProtocolKey.find(protocolKey);
    if (protocolIt != m_tradeRecordOriginalByProtocolKey.end() && protocolIt->second != tradeNo) {
        m_logSender.warn("ykc_trade_record_protocol_map_conflict",
                         std::string("key=") + protocolKey +
                         ",oldTradeNo=" + protocolIt->second +
                         ",newTradeNo=" + tradeNo);
    }
    m_tradeRecordOriginalByProtocolKey[protocolKey] = tradeNo;
    m_logSender.info("ykc_trade_record_protocol_map",
                     std::string("cmd=0x") + (cmd == kCmdUploadTradeRecord ? "3B" : "E9") +
                     ",protocolTradeNo=" + pending.protocolTradeNo +
                     ",originalTradeNo=" + tradeNo);

    if (gun < m_gunRuntimeData.size()) {
        m_gunRuntimeData[gun].pendingRecordTradeNo = tradeNo;
    }
    m_logSender.info("ylc_trade_record_sent",
                     std::string("cmd=0x") + (cmd == kCmdUploadTradeRecord ? "3B" : "E9") +
                     ",gun=" + std::to_string(static_cast<int>(gun)) +
                     ",tradeNo=" + tradeNo);
    return true;
}

void CommProcess::drivePendingTradeRecordUploads(const std::chrono::steady_clock::time_point& now)
{
    if (m_pendingTradeRecordUploads.empty()) {
        return;
    }
    if (m_loginState != LOGIN_ONLINE && m_loginState != LOGIN_UPLOAD_OFFLINE_DATA) {
        return;
    }

    for (std::map<std::string, PendingTradeRecordUpload>::iterator it = m_pendingTradeRecordUploads.begin();
         it != m_pendingTradeRecordUploads.end();) {
        PendingTradeRecordUpload& pending = it->second;
        if (!pending.active || pending.body.empty()) {
            m_tradeRecordOriginalByProtocolKey.erase(tradeRecordUploadKey(pending.cmd, pending.protocolTradeNo));
            it = m_pendingTradeRecordUploads.erase(it);
            continue;
        }

        bool shouldSend = false;
        const char* stage = "";
        if (pending.normalRetryCount < pending.maxNormalRetryCount &&
            now - pending.lastSendTime >= std::chrono::seconds(30)) {
            shouldSend = true;
            stage = "normal_30s_retry";
        } else if (!pending.finalResendDone &&
                   now - pending.firstSendTime >= std::chrono::seconds(300)) {
            shouldSend = true;
            stage = "final_5min_resend";
        }

        if (!shouldSend) {
            ++it;
            continue;
        }

        if (sendPlatformFrame(pending.cmd, pending.body)) {
            pending.lastSendTime = now;
            ++pending.sendCount;
            if (std::strcmp(stage, "normal_30s_retry") == 0) {
                ++pending.normalRetryCount;
            } else {
                pending.finalResendDone = true;
            }
            m_logSender.warn("ylc_trade_record_resend",
                             std::string(stage) +
                             ",cmd=0x" + (pending.cmd == kCmdUploadTradeRecord ? "3B" : "E9") +
                             ",tradeNo=" + pending.tradeNo +
                             ",sendCount=" + std::to_string(static_cast<int>(pending.sendCount)));
        } else {
            m_logSender.warn("ylc_trade_record_resend_fail", pending.tradeNo);
        }

        if (pending.finalResendDone) {
            m_logSender.warn("ylc_trade_record_resend_stop",
                             std::string("tradeNo=") + pending.tradeNo +
                             ",sendCount=" + std::to_string(static_cast<int>(pending.sendCount)) +
                             ",local_record_kept_unconfirmed");
            m_tradeRecordOriginalByProtocolKey.erase(tradeRecordUploadKey(pending.cmd, pending.protocolTradeNo));
            it = m_pendingTradeRecordUploads.erase(it);
        } else {
            ++it;
        }
    }
}

void CommProcess::cacheRemoteStartReconnectContext(uint8_t gun,
                                                   cJSON* startData,
                                                   const std::chrono::steady_clock::time_point& now)
{
    if (gun >= m_gunRuntimeData.size() || !startData) {
        return;
    }
    GunRuntimeData& rd = m_gunRuntimeData[gun];
    rd.remoteStartReconnectWaitActive = false;
    rd.remoteStartReconnectTradeNo = jsonGetString(startData, "orderNo");
    if (rd.remoteStartReconnectTradeNo.empty()) {
        rd.remoteStartReconnectTradeNo = jsonGetString(startData, "tradeNo");
    }
    rd.remoteStartReconnectPayload.clear();
    char* payload = cJSON_PrintUnformatted(startData);
    if (payload) {
        rd.remoteStartReconnectPayload = payload;
        std::free(payload);
    }
    rd.remoteStartReconnectDeadline = now + std::chrono::seconds(60);
}

bool CommProcess::beginRemoteStartReconnectWait(uint8_t gun, int seqOverride, bool sendInitialFail)
{
    if (gun >= m_gunRuntimeData.size()) {
        return false;
    }
    GunRuntimeData& rd = m_gunRuntimeData[gun];
    if (rd.remoteStartReconnectTradeNo.empty()) {
        rd.remoteStartReconnectTradeNo = rd.orderNo;
    }
    if (rd.remoteStartReconnectTradeNo.empty()) {
        return false;
    }

    rd.remoteStartReconnectWaitActive = true;
    if (sendInitialFail) {
        const std::vector<uint8_t> failBody = buildRemoteStartResultBody(gun,
                                                                         rd.remoteStartReconnectTradeNo,
                                                                         0x00,
                                                                         0x05);
        if (!failBody.empty()) {
            (void)sendPlatformFrame(kCmdRemoteStartAck, failBody, seqOverride);
        }
    }
    m_logSender.warn("platform_remote_start_unplug_wait",
                     std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                     ",tradeNo=" + rd.remoteStartReconnectTradeNo);
    return true;
}

void CommProcess::clearRemoteStartReconnectWait(uint8_t gun)
{
    if (gun >= m_gunRuntimeData.size()) {
        return;
    }
    GunRuntimeData& rd = m_gunRuntimeData[gun];
    rd.remoteStartReconnectWaitActive = false;
    rd.remoteStartReconnectTradeNo.clear();
    rd.remoteStartReconnectPayload.clear();
    rd.remoteStartReconnectDeadline = std::chrono::steady_clock::time_point();
}

void CommProcess::handleRemoteStartVehicleReconnect(uint8_t gun)
{
    if (gun >= m_gunRuntimeData.size()) {
        return;
    }
    GunRuntimeData& rd = m_gunRuntimeData[gun];
    if (!rd.remoteStartReconnectWaitActive ||
        !rd.yxVehicleConnectStatusValid ||
        rd.yxVehicleConnectStatus == 0) {
        return;
    }

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (rd.remoteStartReconnectDeadline != std::chrono::steady_clock::time_point() &&
        now > rd.remoteStartReconnectDeadline) {
        m_logSender.warn("platform_remote_start_reconnect_late",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                         ",tradeNo=" + rd.remoteStartReconnectTradeNo);
        clearRemoteStartReconnectWait(gun);
        return;
    }

    if (!m_platformConnected || rd.remoteStartReconnectPayload.empty()) {
        m_logSender.warn("platform_remote_start_reconnect_drop",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                         ",connected=" + (m_platformConnected ? "1" : "0"));
        clearRemoteStartReconnectWait(gun);
        return;
    }

    cJSON* data = cJSON_Parse(rd.remoteStartReconnectPayload.c_str());
    if (!data || !cJSON_IsObject(data)) {
        if (data) {
            cJSON_Delete(data);
        }
        m_logSender.warn("platform_remote_start_reconnect_payload_invalid",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)));
        clearRemoteStartReconnectWait(gun);
        return;
    }

    const std::string tradeNo = rd.remoteStartReconnectTradeNo;
    if (!publishPlatCommand(gun, "start_charge", data)) {
        cJSON_Delete(data);
        m_logSender.warn("platform_remote_start_reconnect_publish_fail",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                         ",tradeNo=" + tradeNo);
        clearRemoteStartReconnectWait(gun);
        return;
    }
    cJSON_Delete(data);

    const std::vector<uint8_t> okBody = buildRemoteStartResultBody(gun, tradeNo, 0x01, 0x00);
    if (!okBody.empty()) {
        (void)sendPlatformFrame(kCmdRemoteStartAck, okBody);
        rd.suppressNextStartCompleteRemoteStartAck = true;
    }
    m_logSender.info("platform_remote_start_reconnect_success",
                     std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                     ",tradeNo=" + tradeNo);
    clearRemoteStartReconnectWait(gun);
}

void CommProcess::drivePendingRemoteStartReconnect(const std::chrono::steady_clock::time_point& now)
{
    for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
        const uint8_t gun = static_cast<uint8_t>(i);
        GunRuntimeData& rd = m_gunRuntimeData[i];
        if (!rd.remoteStartReconnectWaitActive) {
            continue;
        }
        if (rd.remoteStartReconnectDeadline == std::chrono::steady_clock::time_point() ||
            now < rd.remoteStartReconnectDeadline) {
            continue;
        }
        m_logSender.warn("platform_remote_start_reconnect_timeout",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                         ",tradeNo=" + rd.remoteStartReconnectTradeNo);
        clearRemoteStartReconnectWait(gun);
    }
}

void CommProcess::clearPendingTradeRecordUpload(const std::string& tradeNo, bool confirmed)
{
    if (tradeNo.empty()) {
        return;
    }
    std::map<std::string, PendingTradeRecordUpload>::iterator it = m_pendingTradeRecordUploads.find(tradeNo);
    if (it != m_pendingTradeRecordUploads.end()) {
        const uint8_t gun = it->second.gun;
        const std::string protocolKey = tradeRecordUploadKey(it->second.cmd, it->second.protocolTradeNo);
        m_tradeRecordOriginalByProtocolKey.erase(protocolKey);
        if (gun < m_gunRuntimeData.size() && m_gunRuntimeData[gun].pendingRecordTradeNo == tradeNo) {
            m_gunRuntimeData[gun].pendingRecordTradeNo.clear();
        }
        m_pendingTradeRecordUploads.erase(it);
    } else {
        for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
            if (m_gunRuntimeData[i].pendingRecordTradeNo == tradeNo) {
                if (confirmed) {
                    m_gunRuntimeData[i].pendingRecordTradeNo.clear();
                }
                break;
            }
        }
    }
    m_logSender.info("ylc_trade_record_pending_clear",
                     std::string("tradeNo=") + tradeNo +
                     ",confirmed=" + (confirmed ? "1" : "0"));
}

bool CommProcess::isGunChargingForRemoteStop(uint8_t gun) const
{
    if (gun >= m_gunRuntimeData.size()) {
        return false;
    }
    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    // BY LZW: 0x36是充电远程停机，只允许当前明确为充电/停机中且非放电业务时下发内部stop_charge。
    return rd.gunStatus == 0x03U && !rd.lastBusinessIsDischarge;
}

uint32_t CommProcess::precheckYlcDischargeStartAckResult(uint8_t gun, cJSON* startData) const
{
    if (gun >= m_gunRuntimeData.size() || !startData) {
        return 0xFFU;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    if (rd.gunStatus == 0x00U ||
        (rd.yxVehicleConnectStatusValid && rd.yxVehicleConnectStatus == 0U)) {
        return 2U; // 未插枪
    }
    if (rd.gunStatus == 0x03U && !rd.lastBusinessIsDischarge) {
        return 3U; // 已有充电业务
    }
    if (rd.gunStatus == 0x03U && rd.lastBusinessIsDischarge) {
        return 6U; // 已有放电业务
    }
    if (rd.gunStatus == 0x01U ||
        rd.yxTotalFault != 0U ||
        rd.yxOtherFault != 0U ||
        rd.yxEmergencyStopFault != 0U) {
        return 1U; // 放电桩故障
    }

    int dischargeStrategy = 0;
    if (!jsonGetInt(startData, "dischargeStrategy", dischargeStrategy) || dischargeStrategy != 4) {
        return 0xFFU; // 非法策略/其它原因
    }
    int strategyParam = 0;
    if (!jsonGetInt(startData, "dischargeStrategyParam", strategyParam) ||
        strategyParam < 1 || strategyParam > 100) {
        return 0xFFU; // 非法策略参数/其它原因
    }

    if (gun >= m_feeModelByGun.size()) {
        return 4U;
    }
    const FeeModel& feeModel = m_feeModelByGun[gun];
    const bool feeModelReady =
            (!feeModel.feeModelId.empty()) &&
            (feeModel.timeNum > 0) &&
            (feeModel.timeSeg.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.chargeFee.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.serviceFee.size() >= static_cast<size_t>(feeModel.timeNum));
    if (!feeModelReady) {
        return 4U; // 计费策略未配置
    }

    return 0U;
}

uint8_t CommProcess::mapYlcRemoteStartFailReason(int failReason) const
{
    // BY LZW: 0x33 失败原因只有 0x00~0x05，内部细分原因只做大类折算。
    const int code = failReason & 0xFFFF;
    if (code == 0) {
        return 0x00;
    }
    if (code == 0x01 || code == 0x39) {
        return 0x05; // 未插枪/控制导引异常按平台“未插枪”处理。
    }
    return 0x03; // 其它启动失败均按设备故障。
}

uint8_t CommProcess::mapYlcChargeTradeStopReason(int mqttReason) const
{
    // BY LZW: YLC 0x3B 停止原因是 BIN1，码表为 PDF 附录 13.1 的 0x40~0x95。
    if (mqttReason == 0 || mqttReason == 1) {
        return 0x40; // APP/远程停止兜底。
    }
    if (mqttReason >= 0x40 && mqttReason <= 0x95) {
        return static_cast<uint8_t>(mqttReason);
    }

    const int stage = mqttReason & 0xF0000;
    const int code = mqttReason & 0xFFFF;
    if (stage == 0x10000) {
        switch (code) {
        case 0x01: return 0x4B; // 控制导引断开
        case 0x03: return 0x50; // 急停
        case 0x05: return 0x51; // 防雷器异常
        case 0x07: return 0x4C; // 断路器跳位
        case 0x0B: return 0x53; // 桩过温
        case 0x0C: return 0x53; // 枪头过温
        case 0x0D: return 0x55; // 电子锁异常
        case 0x0E: return 0x57; // 绝缘异常
        case 0x0F: return 0x54; // 电池反接
        case 0x10: return 0x56; // 合闸失败
        case 0x17: return 0x4F; // 充电模块故障
        case 0x18: return 0x4A; // 直流过压按控制系统故障
        case 0x19: return 0x4A; // 直流欠压按控制系统故障
        case 0x1A: return 0x4A; // 直流过流按控制系统故障
        case 0x22: return 0x5A; // BRM超时
        case 0x24: return 0x5B; // BCP超时
        case 0x26: return 0x62; // BRO_00超时/异常
        case 0x27: return 0x5C; // BRO_AA超时
        case 0x29: return 0x5E; // BCL超时
        case 0x2A: return 0x5D; // BCS超时
        case 0x2E: return 0x63; // 主机配置/功控通信超时
        case 0x31: return 0x4A; // 协议版本不匹配按控制系统故障
        case 0x3D: return 0x64; // 预充调压失败/未准备就绪
        case 0x42: return 0x5C; // BRO准备就绪超时
        case 0x43: return 0x52; // BMS未就绪/通信异常
        default: return 0x90;
        }
    }
    if (stage == 0x20000) {
        switch (code) {
        case 0x01: return 0x45; // 本地/手动正常停止
        case 0x02: return 0x6A; // 系统故障闭锁
        case 0x03: return 0x6A; // 主控故障
        case 0x04: return 0x64; // 启动完成超时，按未准备就绪
        case 0x05: return 0x6B; // 导引断开
        case 0x06: return 0x6A; // 通信超时
        case 0x08: return 0x72; // 急停
        case 0x0A: return 0x73; // 防雷
        case 0x0C: return 0x6C; // 断路器
        case 0x0D: return 0x6F; // 交流保护/拒动
        case 0x0E: return 0x6F; // 交流保护/粘连
        case 0x0F: return 0x6F; // 交流输入异常
        case 0x10: return 0x8A; // 桩过温
        case 0x11: return 0x8B; // 枪头过温
        case 0x12: return 0x77; // 电子锁
        case 0x13: return 0x70; // 直流保护
        case 0x14: return 0x93; // 继电器黏连
        case 0x1A: return 0x71; // 模块故障
        case 0x1B: return 0x8C; // 过压
        case 0x1C: return 0x8D; // 欠压
        case 0x1D: return 0x8E; // 过流
        case 0x1E: return 0x78; // 输出短路
        case 0x1F: return 0x85; // BCL超时
        case 0x20: return 0x84; // BCS超时
        case 0x21: return 0x86; // BSM超时
        case 0x22: return 0x7D; // 单体电压过高
        case 0x23: return 0x7F; // 单体电压过低/异常
        case 0x24: return 0x41; // SOC达到100%
        case 0x25: return 0x81; // SOC过低/电池状态异常
        case 0x26: return 0x7A; // 充电过电流
        case 0x27: return 0x7C; // 电池组过温
        case 0x28: return 0x90; // 不属于YLC停因，落到未知
        case 0x2A: return 0x41; // BMS正常终止
        case 0x2B: return 0x82; // BMS异常终止
        case 0x2C: return 0x82; // BMS其它异常
        case 0x2F: return 0x92; // 接地/PE
        case 0x30: return 0x91; // CP故障
        case 0x35: return 0x40; // APP要求停止
        case 0x39: return 0x89; // CCS/对侧超时
        case 0x3D: return 0x82; // BMS通信异常
        default: return 0x90;
        }
    }
    return 0x90;
}

uint32_t CommProcess::mapYlcDischargeStartFailReason(int failReason, uint8_t gun) const
{
    if (failReason == 0) {
        return 0U;
    }
    if (gun != 0xFF && gun < m_gunRuntimeData.size()) {
        const GunRuntimeData& rd = m_gunRuntimeData[gun];
        if (rd.yxVehicleConnectStatus == 0 || rd.gunStatus == 0x00) {
            return 2U; // 未插枪
        }
        if (rd.gunStatus == 0x01 ||
            rd.yxTotalFault != 0 ||
            rd.yxOtherFault != 0 ||
            rd.yxEmergencyStopFault != 0) {
            return 1U; // 放电桩故障
        }
        if (rd.gunStatus == 0x03 && !rd.lastBusinessIsDischarge) {
            return 3U; // 已有充电业务
        }
        if (rd.lastBusinessIsDischarge) {
            return 6U; // 已有放电业务
        }
    }
    if (failReason == 2 || failReason == 0x05 || failReason == 0x10001) {
        return 2U;
    }
    if (failReason == 3) {
        return 3U;
    }
    if (failReason == 4) {
        return 4U;
    }
    if (failReason == 6) {
        return 6U;
    }
    return 0xFFU;
}

uint32_t CommProcess::mapYlcDischargeEndReason(cJSON* data) const
{
    int reason = 0;
    if (data && (jsonGetInt(data, "dischargeEndReason", reason) ||
                 jsonGetInt(data, "dischargeStopReason", reason) ||
                 jsonGetInt(data, "reason", reason))) {
        if (reason >= 0x91 && reason <= 0x93) {
            return static_cast<uint32_t>(reason);
        }
    }

    int endSoc = -1;
    if (data && !jsonGetInt(data, "endSoc", endSoc)) {
        (void)jsonGetInt(data, "stopSoc", endSoc);
    }
    if (endSoc >= 0 && endSoc < 10) {
        return 0x92U;
    }
    int targetSoc = -1;
    if (data && (jsonGetInt(data, "dischargeTargetSoc", targetSoc) ||
                 jsonGetInt(data, "strategySoc", targetSoc)) &&
        endSoc >= 0 && targetSoc > 0 && endSoc <= targetSoc) {
        return 0x93U;
    }
    return 0x91U;
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

    // BY LZW: 优先按 IPv4 字面量处理，兼容现有配置。
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(m_config.masterPort));
    if (::inet_pton(AF_INET, m_config.masterHost.c_str(), &addr.sin_addr) == 1) {
        connected = (::connect(m_tcpFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    } else {
        // BY LZW: master_host 支持域名，解析成功后按 IPv4 地址逐个尝试连接。
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
    // BY LZW: 协议要求桩与平台断链重连后序列号归0；平台帧专用序列号从0重新发送。
    m_platformSeq = 0;
    m_lastLoginAction = std::chrono::steady_clock::now();
    m_lastHeartbeat = std::chrono::steady_clock::now();
    m_lastHeartbeatRecv = std::chrono::steady_clock::now();
    m_heartbeatCounter = 0;
    m_lastChargeInfoReport = std::chrono::steady_clock::now();
    m_lastChargeInfoReportByGun.assign(static_cast<size_t>(m_config.gunCount), m_lastChargeInfoReport);
    m_runtimeChangedByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_forcePluggedChargeInfoByGun.assign(static_cast<size_t>(m_config.gunCount), 0);
    m_tcpRxCache.clear();
    resetCryptoSession();
    resetOfflineReplayState();
    m_logSender.info("platform_tcp_connected", m_config.masterHost + ":" + std::to_string(m_config.masterPort));
    return true;
}

void CommProcess::closePlatformTcp()
{
    if (m_tcpFd >= 0) {
        // BY LZW: 主动双向关闭平台 TCP，避免异常恢复时旧连接残留在半开状态影响重连。
        ::shutdown(m_tcpFd, SHUT_RDWR);
        ::close(m_tcpFd);
        m_tcpFd = -1;
    }
    m_platformConnected = false;
    m_loginState = LOGIN_IDLE;
    m_platformSeq = 0;
    m_tcpRxCache.clear();
    m_lastHeartbeatRecv = std::chrono::steady_clock::time_point();
    m_heartbeatCounter = 0;
    // BY LZW: 返回登录流程时增加 10 秒限流，避免链路异常时连续刷 0x01 登录请求。
    m_nextLoginAllowedTime = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
        clearRemoteStartReconnectWait(static_cast<uint8_t>(i));
        m_gunRuntimeData[i].suppressNextStartCompleteRemoteStartAck = false;
    }
    // BY LZW: TCP断开不直接判平台通信故障，统一由连续3次心跳未应答触发 offline 事件。
    resetCryptoSession();
    resetOfflineReplayState();
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

uint16_t CommProcess::calcYlcCrc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int j = 0; j < 8; ++j) {
            if ((crc & 0x0001U) != 0U) {
                crc = static_cast<uint16_t>((crc >> 1) ^ kYlcCrcPoly);
            } else {
                crc = static_cast<uint16_t>(crc >> 1);
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

bool CommProcess::bcdToDigitStringStrict(const uint8_t* data, size_t len, std::string& out)
{
    out.clear();
    if (!data && len > 0U) {
        return false;
    }
    out.reserve(len * 2U);
    for (size_t i = 0; i < len; ++i) {
        const uint8_t hi = static_cast<uint8_t>((data[i] >> 4) & 0x0F);
        const uint8_t lo = static_cast<uint8_t>(data[i] & 0x0F);
        if (hi > 9U || lo > 9U) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<char>('0' + hi));
        out.push_back(static_cast<char>('0' + lo));
    }
    return true;
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
    // BY LZW: 按平台下发时间执行系统校时（失败仅记录日志）。
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
    // BY LZW: 上电/离线恢复后生成16字节会话密钥A（后续用于SM4）。
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
    // BY LZW: 0x02登录应答：桩编码(7) + 登录结果(1) + 公钥(130)。
    // BY LZW: 平台在登录失败场景也可能回最新SM2公钥，收到后要立即更新并固化。
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

std::vector<uint8_t> CommProcess::buildPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body, int seqOverride)
{
    // BY LZW: YLC帧：0x68 + 长度(1) + 序列号(2,LE) + 加密标志(1) + 帧类型(1) + 消息体(N) + CRC(2,LE)。
    // BY LZW: 长度字段仅包含“序列号域 + 加密标志 + 帧类型标志 + 消息体”。
    std::vector<uint8_t> frame;
    frame.reserve(kYlcMinFrameLen + body.size());
    frame.push_back(kYlcFrameHead);

    std::vector<uint8_t> payload;
    uint8_t encryptFlag = kYlcEncryptPlain;
    if (!encryptYlcBody(cmd, body, m_config.ylcEncryptEnable, payload, encryptFlag)) {
        m_logSender.warn("platform_tx_3des_unsupported", std::string("cmd=") + std::to_string(cmd));
        return std::vector<uint8_t>();
    }
    const size_t dataLen = kYlcFixedDataLen + payload.size();
    if (dataLen > kYlcMaxDataLen || (!isDischargeFrameCmd(cmd) && dataLen > kYkcChargeMaxDataLen)) {
        m_logSender.warn("platform_tx_frame_too_long",
                         std::string("cmd=") + std::to_string(cmd) +
                         ",dataLen=" + std::to_string(dataLen));
        return std::vector<uint8_t>();
    }
    const uint16_t seq = (seqOverride >= 0 && seqOverride <= 0xFFFF)
            ? static_cast<uint16_t>(seqOverride)
            : static_cast<uint16_t>((m_platformSeq++) & 0xFFFF);
    frame.push_back(static_cast<uint8_t>(dataLen));

    // BY LZW: 序列号域低字节在前。
    frame.push_back(static_cast<uint8_t>(seq & 0xFF));
    frame.push_back(static_cast<uint8_t>((seq >> 8) & 0xFF));

    frame.push_back(encryptFlag);
    frame.push_back(cmd);
    frame.insert(frame.end(), payload.begin(), payload.end());

    const uint16_t crc = calcYlcCrc16(frame.data() + 2, dataLen);
    // BY LZW: YLC CRC覆盖序列号域到消息体，低字节在前。
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return frame;
}

bool CommProcess::sendPlatformFrame(uint8_t cmd, const std::vector<uint8_t>& body, int seqOverride)
{
    if (m_tcpFd < 0) {
        return false;
    }
    const std::vector<uint8_t> frame = buildPlatformFrame(cmd, body, seqOverride);
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
    // BY LZW: YLC 0x01 登录认证请求体。首版默认明文，不走 Zhongshihua2.0 的 SM2/SM4 登录密钥流程。
    std::vector<uint8_t> body;
    body.reserve(30);

    // BY LZW: 1) 桩编码 BCD7。
    appendPileCodeBcd7(body, m_config.cdzNo);
    // BY LZW: 2) 桩类型，YLC PDF：0x00直流，0x01交流。
    body.push_back(m_config.chargerType);
    // BY LZW: 3) 枪数量。
    body.push_back(m_config.gunCount);
    // BY LZW: 4) 通信协议版本，YLC PDF：版本号乘10，V1.5=>0x0F。
    body.push_back(encodeYlcProtocolVersionBin1(m_config.ylcChargeProtocolVersion));
    // BY LZW: 5) 程序版本，ASCII8，不足补0，超长截断。
    const std::string mainVer = m_config.ylcProgramVersion.empty() ? "1.0.0" : m_config.ylcProgramVersion;
    for (size_t i = 0; i < 8U; ++i) {
        body.push_back(i < mainVer.size() ? static_cast<uint8_t>(mainVer[i]) : 0x00);
    }
    // BY LZW: 6) 网络类型。
    body.push_back(static_cast<uint8_t>(std::max(0, std::min(255, m_config.ylcNetworkType))));
    // BY LZW: 7) SIM ICCID，BCD10，不足补0。
    appendBcdFixed(body, m_config.ylcSimIccid, 10);
    // BY LZW: 8) 运营商。
    body.push_back(static_cast<uint8_t>(std::max(0, std::min(255, m_config.ylcOperatorId))));
    return body;
}

std::vector<uint8_t> CommProcess::buildFeeModelRequestBody(uint8_t gunNoBcd) const
{
    // BY LZW: YLC 0x09 计费模型请求信息体：桩编号BCD(7字节)。gunNoBcd兼容保留，不写入。
    (void)gunNoBcd;
    std::vector<uint8_t> body;
    appendPileCodeBcd7(body, m_config.cdzNo);
    return body;
}

std::vector<uint8_t> CommProcess::buildFeeModelCheckRequestBody() const
{
    // BY LZW: YLC 0x05 计费模型校验：桩编号BCD7 + 当前平台模型号BCD2；无模型时填0000。
    std::vector<uint8_t> body;
    appendPileCodeBcd7(body, m_config.cdzNo);

    std::string modelNo = "0000";
    for (size_t i = 0; i < m_ylcFeeModelNoByGun.size(); ++i) {
        if (!m_ylcFeeModelNoByGun[i].empty() && m_ylcFeeModelNoByGun[i] != "0000") {
            modelNo = m_ylcFeeModelNoByGun[i];
            break;
        }
    }
    appendBcdFixed(body, modelNo, 2);
    return body;
}

std::vector<uint8_t> CommProcess::buildTimeSyncRequestBody() const
{
    // BY LZW: YLC 0x55 对时设置应答：桩编号BCD7 + 当前时间CP56Time2a(7)。
    std::vector<uint8_t> body;
    appendPileCodeBcd7(body, m_config.cdzNo);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    std::time_t tt = static_cast<std::time_t>(tv.tv_sec);
    std::tm* tmv = std::localtime(&tt);
    const uint16_t ms = static_cast<uint16_t>((tmv ? tmv->tm_sec : 0) * 1000 + (tv.tv_usec / 1000));
    body.push_back(static_cast<uint8_t>(ms & 0xFF));
    body.push_back(static_cast<uint8_t>((ms >> 8) & 0xFF));
    body.push_back(static_cast<uint8_t>(tmv ? (tmv->tm_min & 0x3F) : 0));
    body.push_back(static_cast<uint8_t>(tmv ? (tmv->tm_hour & 0x1F) : 0));
    body.push_back(static_cast<uint8_t>(tmv ? (tmv->tm_mday & 0x1F) : 1));
    body.push_back(static_cast<uint8_t>(tmv ? ((tmv->tm_mon + 1) & 0x0F) : 1));
    body.push_back(static_cast<uint8_t>(tmv ? ((tmv->tm_year + 1900) % 100) : 26));
    return body;
}

std::vector<uint8_t> CommProcess::buildHeartbeatBody()
{
    // BY ZF: 云快充V1.6 0x03心跳：桩编号BCD7 + 枪号BCD1 + 枪状态BIN1。
    std::vector<uint8_t> body;
    appendPileCodeBcd7(body, m_config.cdzNo);
    uint8_t gunNoBcd = 0x00;
    uint8_t gunStatus = 0x00;
    for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
        if (mapHeartbeatGunFaultStatus(m_gunRuntimeData[i]) == 0x01) {
            gunNoBcd = bcdByte(static_cast<int>(i + 1));
            gunStatus = 0x01;
            break;
        }
    }
    if (gunNoBcd == 0x00) {
        for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
            if (m_gunRuntimeData[i].gunStatus != 0x00) {
                gunNoBcd = bcdByte(static_cast<int>(i + 1));
                gunStatus = 0x00;
                break;
            }
        }
    }
    if (gunNoBcd == 0x00 && !m_gunRuntimeData.empty()) {
        gunNoBcd = bcdByte(1);
    }
    body.push_back(gunNoBcd);
    body.push_back(gunStatus);
    return body;
}

uint8_t CommProcess::mapHeartbeatGunFaultStatus(const GunRuntimeData& rd) const
{
    if (rd.yxTotalFault != 0U ||
        rd.yxTotalAlarm != 0U ||
        rd.yxEmergencyStopFault != 0U ||
        rd.yxOtherFault != 0U) {
        return 0x01;
    }
    for (std::map<std::string, uint8_t>::const_iterator it = rd.yxFaultFields.begin();
         it != rd.yxFaultFields.end(); ++it) {
        if (it->second != 0U) {
            return 0x01;
        }
    }
    return 0x00;
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

    // BY LZW: 0x15 BRM 上送：交易流水号 + 桩编号 + 枪号。
    appendBcdFixed(body, rd.orderNo, 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));

    // BY LZW: BRM(BMS)通信协议版本号（3字节）。
    // BY LZW: 按协议示例 V1.10 => 0x01 0x01 0x00，上送3字节原值。
    body.insert(body.end(),
                rd.startCompleteData.pileBmsVersion.begin(),
                rd.startCompleteData.pileBmsVersion.end());

    // BY LZW: BRM 基础字段。
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
    // BY LZW: 预留1字节，当前固定0x00。
    body.push_back(0x00);
    fillAsciiFixed(body, rd.startCompleteData.vin, 17);
    body.insert(body.end(), rd.startCompleteData.bmsSoftwareVersion.begin(), rd.startCompleteData.bmsSoftwareVersion.end());
    if (body.size() != 73U) {
        body.clear();
    }
    return body;
}

std::vector<uint8_t> CommProcess::buildBcpBody(uint8_t gun) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];

    // BY LZW: 0x17 BCP参数配置报文。
    // 1) 交易流水号 BCD16
    appendBcdFixed(body, rd.orderNo, 16);
    // 2) 桩编号 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1（1..n）
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));

    // 4) BMS单体动力蓄电池最高允许充电电压（0.01V/位）
    appendU16LE(body, rd.startCompleteData.cellMaxChargeVoltage);
    // 5) BMS最高允许充电电流（0.1A/位，按协议-400A偏移）
    {
        int bmsMaxCurrentRaw = static_cast<int>(rd.startCompleteData.maxAllowChargeCurrent) + 4000;
        if (bmsMaxCurrentRaw < 0) {
            bmsMaxCurrentRaw = 0;
        }
        if (bmsMaxCurrentRaw > 65535) {
            bmsMaxCurrentRaw = 65535;
        }
        appendU16LE(body, static_cast<uint16_t>(bmsMaxCurrentRaw));
    }
    // 6) BMS动力蓄电池标称总能量（0.1kWh/位）
    appendU16LE(body, rd.startCompleteData.nominalEnergy);
    // 7) BMS最高允许充电总电压（0.1V/位）
    appendU16LE(body, rd.startCompleteData.bmsMaxChargeVoltage);
    // 8) BMS最高允许温度（偏移50，原始值直传）
    body.push_back(rd.startCompleteData.maxAllowTemp + 50);
    // 9) BMS整车动力蓄电池荷电状态SOC（0.1%/位）
    appendU16LE(body, rd.startCompleteData.soc);
    // 10) BMS整车动力蓄电池当前电池总电压（0.1V/位）
    appendU16LE(body, rd.startCompleteData.currentTotalVoltage);
    // 11) CML电桩最高输出电压（0.1V/位）
    appendU16LE(body, rd.startCompleteData.pileMaxOutputVoltage);
    // 12) CML电桩最低输出电压（0.1V/位）
    appendU16LE(body, rd.startCompleteData.pileMinOutputVoltage);
    // 13) CML电桩最大输出电流（0.1A/位，按协议-400A偏移）
    {
        int pileMaxCurrentRaw = static_cast<int>(rd.startCompleteData.pileMaxOutputCurrent) + 4000;
        if (pileMaxCurrentRaw < 0) {
            pileMaxCurrentRaw = 0;
        }
        if (pileMaxCurrentRaw > 65535) {
            pileMaxCurrentRaw = 65535;
        }
        appendU16LE(body, static_cast<uint16_t>(pileMaxCurrentRaw));
    }
    // 14) CML电桩最小输出电流（0.1A/位，按协议-400A偏移）
    {
        int pileMinCurrentRaw = static_cast<int>(rd.startCompleteData.pileMinOutputCurrent) + 4000;
        if (pileMinCurrentRaw < 0) {
            pileMinCurrentRaw = 0;
        }
        if (pileMinCurrentRaw > 65535) {
            pileMinCurrentRaw = 65535;
        }
        appendU16LE(body, static_cast<uint16_t>(pileMinCurrentRaw));
    }
    if (body.size() != 45U) {
        body.clear();
    }
    return body;
}

std::vector<uint8_t> CommProcess::buildChargeEndStageBody(uint8_t gun, cJSON* stopCompleteData) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size() || !stopCompleteData) {
        return body;
    }

    // BY LZW: 结束阶段关键量直接取 stop_complete 事件，累计时间/电量取 feeData 运行态缓存。
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

    // BY LZW: 0x19 上传充电结束阶段报文。
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
    // BY LZW: feeData.chargedTime 当前缓存单位为秒，0x19 要求分钟。
    const uint16_t chargedMinutes = static_cast<uint16_t>(std::max(0.0, rd.chargedTime / 60.0));
    // 9) 本次充电累计充电时间 BIN2（min）
    appendU16LE(body, chargedMinutes);
    // BY LZW: feeData.totalEnergy 当前缓存单位为kWh，0x19 要求0.1kWh。
    int energyRaw = static_cast<int>(rd.totalEnergy * 10.0);
    if (energyRaw < 0) energyRaw = 0;
    if (energyRaw > 0xFFFF) energyRaw = 0xFFFF;
    // 10) 本次充电电量 BIN2（0.1kWh）
    appendU16LE(body, static_cast<uint16_t>(energyRaw));
    // 11) 电桩充电机编号 BIN4；当前无独立充电机编号时沿用枪配置ID，缺省填0。
    uint32_t chargerNo = 0U;
    if (gun < m_config.gunIdList.size()) {
        chargerNo = static_cast<uint32_t>(m_config.gunIdList[gun]);
    }
    appendU32LE(body, chargerNo);
    if (body.size() != 39U) {
        body.clear();
    }
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
    // BY LZW: 0x1D BST停车中止上送报文。
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
    if (body.size() != 28U) {
        body.clear();
    }
    return body;
}

std::vector<uint8_t> CommProcess::buildChargeErrorBody(uint8_t gun, cJSON* errorData) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size() || !errorData) {
        return body;
    }

    auto getFlag = [errorData](const char* key) -> bool {
        cJSON* n = cJSON_GetObjectItem(errorData, key);
        return n && cJSON_IsNumber(n) && n->valueint != 0;
    };
    auto get2Bit = [&getFlag](const char* key1, const char* key2 = nullptr) -> uint8_t {
        if (getFlag(key1) || (key2 && getFlag(key2))) {
            return 0x01U; // 01=超时；10=不可信状态当前无独立来源
        }
        return 0x00U;
    };
    auto set2 = [](uint8_t& b, int shift, uint8_t val) {
        b = static_cast<uint8_t>(b | ((val & 0x03U) << shift));
    };

    uint8_t err[8] = {0};
    set2(err[0], 0, get2Bit("timeoutChargerId00", "timeoutChargerId"));
    set2(err[0], 2, get2Bit("timeoutChargerIdAA"));
    set2(err[1], 0, get2Bit("timeoutTimeSync", "timeoutCml"));
    set2(err[1], 2, get2Bit("timeoutChargeReady"));
    set2(err[2], 0, get2Bit("timeoutChargeStatus"));
    set2(err[2], 2, get2Bit("timeoutChargeStop"));
    set2(err[3], 0, get2Bit("timeoutChargeStat"));
    set2(err[4], 0, get2Bit("timeoutBmsVehicleId"));
    set2(err[5], 0, get2Bit("timeoutBatteryParams"));
    set2(err[5], 2, get2Bit("timeoutBmsReady"));
    set2(err[6], 0, get2Bit("timeoutBatteryStatus", "timeoutBcs"));
    set2(err[6], 2, get2Bit("timeoutBatteryReq", "timeoutBcl"));
    set2(err[6], 4, get2Bit("timeoutBmsStop", "timeoutBst"));
    set2(err[7], 0, get2Bit("timeoutBmsStat"));

    bool hasError = false;
    for (size_t i = 0; i < sizeof(err); ++i) {
        if (err[i] != 0) {
            hasError = true;
            break;
        }
    }
    if (!hasError) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    appendBcdFixed(body, rd.orderNo, 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    body.insert(body.end(), err, err + sizeof(err));
    if (body.size() != 32U) {
        body.clear();
    }
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

    // BY LZW: 0x23 上送充电过程BMS需求与充电机输出报文（15秒周期）。
    // 1) 交易流水号 BCD16
    appendBcdFixed(body, rd.orderNo, 16);
    // 2) 桩编号 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1（1..n）
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));

    // 4) BCL BMS电压需求（0.1V）
    appendU16LE(body, clampU16(static_cast<int>(rd.bmsReqVoltage * 10.0)));
    // 5) BCL BMS电流需求（0.1A，按协议-400A偏移）
    appendU16LE(body, clampU16(static_cast<int>(rd.bmsReqCurrent * 10.0 + 4000.0)));
    // 6) BCL BMS充电模式
    body.push_back(clampU8(rd.ycChargeMode));
    // 7) BCS BMS当前电压测量值（0.1V）
    appendU16LE(body, clampU16(static_cast<int>(rd.bmsMeasuredVoltage * 10.0)));
    // 8) BCS BMS当前电流测量值（0.1A，按协议-400A偏移）
    appendU16LE(body, clampU16(static_cast<int>(rd.bmsMeasuredCurrent * 10.0 + 4000.0)));
    // BY LZW: 当前仅缓存最高/最低单体编号，分组号无来源，先按0上送。
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
    // 13) CCS 电桩电流输出值（0.1A，按协议-400A偏移）
    appendU16LE(body, clampU16(static_cast<int>(rd.current * 10.0 + 4000.0)));
    // 14) CCS 累计充电时间（min）
    appendU16LE(body, clampU16(static_cast<int>(rd.chargedTime / 60.0)));
    if (body.size() != 44U) {
        body.clear();
    }
    return body;
}

std::vector<uint8_t> CommProcess::buildCstBody(uint8_t gun, cJSON* stopCompleteData) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    auto getU8 = [stopCompleteData](const char* key, uint8_t defVal) -> uint8_t {
        cJSON* n = stopCompleteData ? cJSON_GetObjectItem(stopCompleteData, key) : nullptr;
        if (!n || !cJSON_IsNumber(n)) {
            return defVal;
        }
        int v = n->valueint;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<uint8_t>(v);
    };
    auto getU16 = [stopCompleteData](const char* key, uint16_t defVal) -> uint16_t {
        cJSON* n = stopCompleteData ? cJSON_GetObjectItem(stopCompleteData, key) : nullptr;
        if (!n || !cJSON_IsNumber(n)) {
            return defVal;
        }
        int v = n->valueint;
        if (v < 0) v = 0;
        if (v > 0xFFFF) v = 0xFFFF;
        return static_cast<uint16_t>(v);
    };

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    // BY LZW: 0x21 CST充电机中止上送报文。
    // 1) 交易流水号 BCD16
    appendBcdFixed(body, rd.orderNo, 16);
    // 2) 桩编号 BCD7
    appendPileCodeBcd7(body, m_config.cdzNo);
    // 3) 枪号 BCD1（1..n）
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    // 4) 充电机中止充电原因 BIN1
    body.push_back(getU8("chargerStopReason", 0));
    // 5) 充电机中止充电故障原因 BIN2
    appendU16LE(body, getU16("chargerStopFaultReason", 0));
    // 6) 充电机中止充电错误原因 BIN1
    body.push_back(getU8("chargerStopErrorReason", 0));
    if (body.size() != 28U) {
        body.clear();
    }
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

    // BY LZW: 0x25 BSM上送报文（充电中15秒周期）。
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
    bsmStatusWord |= static_cast<uint16_t>(0x01U << 12); // g15
    // g16 预留默认00
    appendU16LE(body, bsmStatusWord);
    if (body.size() != 31U) {
        body.clear();
    }
    return body;
}

std::vector<uint8_t> CommProcess::buildMergeVinStartApplyBody(uint8_t gun, cJSON* data)
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

    int plugAndChargeFlag = 0x02;
    (void)jsonGetInt(data, "plugAndChargeFlag", plugAndChargeFlag);
    int mergeChargeFlag = 0x01;
    (void)jsonGetInt(data, "mergeChargeFlag", mergeChargeFlag);

    // BY LZW: 并充序号与主辅枪标记不再依赖 MQTT 传参。
    const uint8_t leftGun = static_cast<uint8_t>(gun & static_cast<uint8_t>(~0x01));
    const uint8_t masterGunFlag = 0x00;
    std::string mergeSeq;
    if (leftGun < m_gunRuntimeData.size()) {
        mergeSeq = m_gunRuntimeData[leftGun].pendingVinAuthMergeSeq;
    }
    if (mergeSeq.empty()) {
        const std::time_t nowSec = std::time(nullptr);
        std::tm tmv;
        localtime_r(&nowSec, &tmv);
        char seqBuf[16];
        std::snprintf(seqBuf, sizeof(seqBuf), "%02d%02d%02d%02d%02d%02d",
                      (tmv.tm_year + 1900) % 100, tmv.tm_mon + 1, tmv.tm_mday,
                      tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        mergeSeq = seqBuf;
        if (leftGun < m_gunRuntimeData.size()) {
            m_gunRuntimeData[leftGun].pendingVinAuthMergeSeq = mergeSeq;
        }
    }

    GunRuntimeData& rd = m_gunRuntimeData[gun];
    rd.pendingVinAuthVin = vin;
    rd.pendingVinAuthPlugAndChargeFlag = static_cast<uint8_t>(plugAndChargeFlag & 0xFF);
    rd.pendingVinAuthMergeChargeFlag = static_cast<uint8_t>(mergeChargeFlag & 0xFF);
    rd.pendingVinAuthMasterGunFlag = masterGunFlag;
    rd.pendingVinAuthMergeSeq = mergeSeq;

    // BY LZW: 0xA1 并充VIN请求启动：在0xA5基础上追加主辅枪标记 + 并充序号BCD6。
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    body.push_back(0x03);
    body.push_back(0x00);
    body.insert(body.end(), 8, 0x00);
    body.insert(body.end(), 16, 0x00);
    appendAsciiFixed(body, vin, 17, 0x00);
    body.push_back(static_cast<uint8_t>(masterGunFlag & 0xFF));
    appendBcdFixed(body, mergeSeq, 6);
    return body;
}

std::vector<uint8_t> CommProcess::buildPlugVinReportBody(uint8_t gun) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }
    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    if (rd.pendingPlugVinReportVin.size() != 17U) {
        return body;
    }

    // BY LZW: 0x71 插枪VIN上报：桩编号BCD7 + 枪号BCD1 + SOC BIN1 + VIN ASCII17，VIN正序上传。
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    body.push_back(rd.pendingPlugVinReportSoc);
    body.insert(body.end(), rd.pendingPlugVinReportVin.begin(), rd.pendingPlugVinReportVin.end());
    return body;
}

bool CommProcess::handlePlugVinReportEvent(uint8_t gun, cJSON* data)
{
    if (!data || gun >= m_gunRuntimeData.size()) {
        return false;
    }

    std::string vin = jsonGetStringCompat(data, "vin", "vinCode");
    vin = sanitizeVin17(vin);
    if (vin.size() != 17U) {
        m_logSender.warn("platform_plug_vin_report", "invalid_vin");
        return false;
    }

    int soc = static_cast<int>(m_gunRuntimeData[gun].soc + 0.5);
    (void)jsonGetInt(data, "soc", soc);
    if (soc < 0) {
        soc = 0;
    }
    if (soc > 100) {
        soc = 100;
    }

    GunRuntimeData& rd = m_gunRuntimeData[gun];
    rd.pendingPlugVinReportVin = vin;
    rd.pendingPlugVinReportSoc = static_cast<uint8_t>(soc);
    rd.plugVinReportActive = true;
    rd.plugVinReportSendCount = 0;
    rd.lastPlugVinReportTime = std::chrono::steady_clock::time_point();

    (void)sendPlugVinReportNow(gun);
    m_logSender.info("platform_plug_vin_report",
                     std::string("event_cached gun=") + std::to_string(static_cast<int>(gun)));
    return true;
}

bool CommProcess::sendPlugVinReportNow(uint8_t gun)
{
    if (gun >= m_gunRuntimeData.size()) {
        return false;
    }
    GunRuntimeData& rd = m_gunRuntimeData[gun];
    if (!rd.plugVinReportActive || rd.pendingPlugVinReportVin.size() != 17U) {
        return false;
    }
    if (!m_platformConnected.load() || m_loginState != LOGIN_ONLINE) {
        return false;
    }
    if (rd.plugVinReportSendCount >= 4U) {
        rd.plugVinReportActive = false;
        m_logSender.warn("platform_plug_vin_report", "max_retry_reached");
        return false;
    }

    const std::vector<uint8_t> body = buildPlugVinReportBody(gun);
    if (body.empty()) {
        return false;
    }
    if (!sendPlatformFrame(kCmdPlugVinReport, body)) {
        return false;
    }
    rd.plugVinReportSendCount = static_cast<uint8_t>(rd.plugVinReportSendCount + 1U);
    rd.lastPlugVinReportTime = std::chrono::steady_clock::now();
    return true;
}

void CommProcess::restartPlugVinReport(uint8_t gun)
{
    if (gun >= m_gunRuntimeData.size()) {
        return;
    }
    GunRuntimeData& rd = m_gunRuntimeData[gun];
    if (rd.pendingPlugVinReportVin.size() != 17U) {
        m_logSender.info("platform_plug_vin_query", "no_cached_vin");
        return;
    }
    rd.plugVinReportActive = true;
    rd.plugVinReportSendCount = 0;
    rd.lastPlugVinReportTime = std::chrono::steady_clock::time_point();
    (void)sendPlugVinReportNow(gun);
}

void CommProcess::stopPlugVinReport(uint8_t gun)
{
    if (gun >= m_gunRuntimeData.size()) {
        return;
    }
    GunRuntimeData& rd = m_gunRuntimeData[gun];
    rd.plugVinReportActive = false;
    rd.plugVinReportSendCount = 0;
}

void CommProcess::drivePlugVinReportRetries(const std::chrono::steady_clock::time_point& now)
{
    if (!m_platformConnected.load() || m_loginState != LOGIN_ONLINE) {
        return;
    }
    for (uint8_t gun = 0; gun < m_gunRuntimeData.size(); ++gun) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        if (!rd.plugVinReportActive) {
            continue;
        }
        if (rd.plugVinReportSendCount == 0U ||
            now - rd.lastPlugVinReportTime >= std::chrono::seconds(3)) {
            (void)sendPlugVinReportNow(gun);
        }
    }
}

std::vector<uint8_t> CommProcess::buildParkingLockStatusBody(uint8_t gun, cJSON* data) const
{
    std::vector<uint8_t> body;
    if (!data || gun >= m_gunRuntimeData.size()) {
        return body;
    }

    auto getField = [data](const char* k1, const char* k2, const char* k3, int defVal) -> int {
        int v = defVal;
        if ((k1 && jsonGetInt(data, k1, v)) ||
            (k2 && jsonGetInt(data, k2, v)) ||
            (k3 && jsonGetInt(data, k3, v))) {
            return v;
        }
        return defVal;
    };

    int lockStatus = getField("lockStatus", "parkingLockStatus", "status", 0xFF);
    int vehicleStatus = getField("vehicleStatus", "parkingStatus", "vehicleExist", 0x00);
    int batteryPercent = getField("batteryPercent", "battery", "electricity", 0);
    int alarmStatus = getField("alarmStatus", "alarm", "faultStatus", 0x00);
    if (batteryPercent < 0) batteryPercent = 0;
    if (batteryPercent > 100) batteryPercent = 100;

    // BY LZW: 0x61 地锁状态上送：桩编号BCD7 + 枪号BIN1 + 地锁状态 + 车位状态 + 电量 + 报警 + 4字节预留。
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(static_cast<uint8_t>(gun + 1U));
    body.push_back(static_cast<uint8_t>(lockStatus & 0xFF));
    body.push_back(static_cast<uint8_t>(vehicleStatus & 0xFF));
    body.push_back(static_cast<uint8_t>(batteryPercent & 0xFF));
    body.push_back(static_cast<uint8_t>(alarmStatus & 0xFF));
    body.insert(body.end(), 4, 0x00);
    return body;
}

bool CommProcess::handleParkingLockStatusEvent(uint8_t gun, cJSON* data)
{
    if (!kYlcEnablePhase2ParkingLock) {
        m_logSender.info("platform_parking_lock_status", "phase2_disabled");
        return false;
    }
    const std::vector<uint8_t> body = buildParkingLockStatusBody(gun, data);
    if (body.empty()) {
        m_logSender.warn("platform_parking_lock_status", "build_body_fail");
        return false;
    }
    const bool ok = sendPlatformFrame(kCmdParkingLockStatus, body);
    m_logSender.info("platform_parking_lock_status", ok ? "send_ok" : "send_fail");
    return ok;
}

std::vector<uint8_t> CommProcess::buildRemoteStartAckBody(uint8_t gun, uint8_t result) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }
    // BY LZW: 0x33 远程启动充电结果骨架：交易流水号BCD16 + 桩编号BCD7 + 枪号BCD1 + 启动结果BCD1 + 失败原因BIN1。
    appendBcdFixed(body, m_gunRuntimeData[gun].orderNo, 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    body.push_back(static_cast<uint8_t>(result));
    body.push_back(static_cast<uint8_t>(result == 0x01 ? 0x00 : 0x03));
    return body;
}

std::vector<uint8_t> CommProcess::buildRemoteMergeStartAckBody(uint8_t gun, const std::string& orderNo, uint8_t result, uint8_t failReason, const std::string& mergeSeq) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }
    // BY LZW: 0xA3 远程并充启动应答：交易流水号BCD16 + 桩编号BCD7 + 枪号BCD1 + 启动结果BCD1 + 失败原因BIN1 + 主辅枪标记BIN1 + 并充序号BCD6。
    appendBcdFixed(body, orderNo, 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    body.push_back(static_cast<uint8_t>(result));
    body.push_back(static_cast<uint8_t>(failReason));
    body.push_back(0x00);
    appendBcdFixed(body, mergeSeq, 6);
    return body;
}

bool CommProcess::sendRemoteStopAck(uint8_t gunNoBcd, uint8_t result, uint8_t failReason, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(10);
    // BY LZW: 0x35 远程停止充电应答：桩编号BCD7 + 枪号BCD1 + 停止结果BCD1 + 失败原因BIN1。
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(gunNoBcd);
    body.push_back(result);
    body.push_back(failReason);
    return sendPlatformFrame(kCmdRemoteStopAck, body, seqOverride);
}

bool CommProcess::sendDischargeStartAck(const std::string& tradeNo, uint8_t gunNoBcd, uint32_t result, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(28);
    appendBcdFixed(body, tradeNo, 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(gunNoBcd);
    appendU32LE(body, result);
    return sendPlatformFrame(kCmdDischargeStartAck, body, seqOverride);
}

bool CommProcess::sendDischargeStopAck(uint8_t gunNoBcd, uint8_t result, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(9);
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(gunNoBcd);
    body.push_back(result);
    return sendPlatformFrame(kCmdDischargeStopAck, body, seqOverride);
}

bool CommProcess::sendBalanceUpdateAck(const std::vector<uint8_t>& physicalCard, uint8_t result, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(16);
    // BY LZW: 0x41 余额更新应答：桩编号BCD7 + 物理卡号BIN8 + 修改结果BIN1；PDF定义0x00为成功。
    appendPileCodeBcd7(body, m_config.cdzNo);
    if (physicalCard.size() >= 8U) {
        body.insert(body.end(), physicalCard.begin(), physicalCard.begin() + 8);
    } else {
        body.insert(body.end(), 8, 0x00);
    }
    body.push_back(result);
    return sendPlatformFrame(kCmdBalanceUpdateAck, body, seqOverride);
}

bool CommProcess::sendOfflineCardSyncAck(uint8_t result, uint8_t failReason, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(9);
    // BY LZW: 0x43 离线卡同步应答：桩编号BCD7 + 保存结果BIN1 + 失败原因BIN1；PDF定义0x01为成功。
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(result);
    body.push_back(failReason);
    return sendPlatformFrame(kCmdOfflineCardSyncAck, body, seqOverride);
}

bool CommProcess::sendOfflineCardClearAck(const std::vector<std::vector<uint8_t> >& physicalCards, bool success, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(7U + physicalCards.size() * 10U);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const uint8_t mark = success ? 0x01 : 0x00;
    const uint8_t failReason = success ? 0x00 : 0x01;
    for (size_t i = 0; i < physicalCards.size(); ++i) {
        if (physicalCards[i].size() >= 8U) {
            body.insert(body.end(), physicalCards[i].begin(), physicalCards[i].begin() + 8);
        } else {
            body.insert(body.end(), 8, 0x00);
        }
        body.push_back(mark);
        body.push_back(failReason);
    }
    return sendPlatformFrame(kCmdOfflineCardClearAck, body, seqOverride);
}

bool CommProcess::sendOfflineCardQueryAck(const std::vector<std::vector<uint8_t> >& physicalCards, bool exists, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(7U + physicalCards.size() * 9U);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const uint8_t result = exists ? 0x01 : 0x00;
    for (size_t i = 0; i < physicalCards.size(); ++i) {
        if (physicalCards[i].size() >= 8U) {
            body.insert(body.end(), physicalCards[i].begin(), physicalCards[i].begin() + 8);
        } else {
            body.insert(body.end(), 8, 0x00);
        }
        body.push_back(result);
    }
    return sendPlatformFrame(kCmdOfflineCardQueryAck, body, seqOverride);
}

bool CommProcess::sendWorkParamSetAck(uint8_t result, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(8);
    // BY ZF: 云快充V1.6 0x51工作参数设置应答：桩编号BCD7 + 设置结果BIN1。
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(result);
    return sendPlatformFrame(kCmdWorkParamSetAck, body, seqOverride);
}

bool CommProcess::sendParkingLockCtrlAck(uint8_t gun, uint8_t result, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(13);
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(static_cast<uint8_t>(gun + 1U));
    body.push_back(result);
    body.insert(body.end(), 4, 0x00);
    return sendPlatformFrame(kCmdParkingLockCtrlAck, body, seqOverride);
}

bool CommProcess::sendRemoteRebootAck(uint8_t result, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(8);
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(result);
    return sendPlatformFrame(kCmdRemoteRebootAck, body, seqOverride);
}

bool CommProcess::sendRemoteUpdateAck(uint8_t result, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(8);
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(result);
    return sendPlatformFrame(kCmdRemoteUpdateAck, body, seqOverride);
}

bool CommProcess::sendRemoteLogRequestAck(uint8_t result, int seqOverride)
{
    std::vector<uint8_t> body;
    body.reserve(8);
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(result);
    return sendPlatformFrame(kCmdRemoteLogRequestAck, body, seqOverride);
}

std::vector<uint8_t> CommProcess::buildStartChargeResultBody(uint8_t gun) const
{
    if (gun >= m_gunRuntimeData.size()) {
        return std::vector<uint8_t>();
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    const int successFlag = static_cast<int>(rd.startCompleteData.successFlag);
    const uint8_t result = static_cast<uint8_t>(successFlag == 0 ? 0x01 : 0x00);
    const uint8_t failReason = static_cast<uint8_t>(
        successFlag == 0 ? 0x00 : mapYlcRemoteStartFailReason(static_cast<int>(rd.startCompleteData.failReason)));
    return buildRemoteStartResultBody(gun, rd.orderNo, result, failReason);
}

std::vector<uint8_t> CommProcess::buildRemoteStartResultBody(uint8_t gun,
                                                             const std::string& tradeNo,
                                                             uint8_t result,
                                                             uint8_t failReason) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    body.reserve(32);
    // BY LZW: 1) 交易流水号 BCD16，优先使用启动阶段缓存的订单号。
    appendBcdFixed(body, tradeNo, 16);
    // BY LZW: 2) 桩编号 BCD7。
    appendPileCodeBcd7(body, m_config.cdzNo);
    // BY LZW: 3) 枪号 BCD1（按1..n上送）。
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    // BY LZW: 4) 启动结果 BCD1：0x00失败，0x01成功。
    body.push_back(intToBcdByte(result == 0x01 ? 1 : 0));
    // BY LZW: 5) 失败原因 BIN1（成功时填0x00）。
    body.push_back(static_cast<uint8_t>(result == 0x01 ? 0x00 : failReason));
    return body;
}

std::vector<uint8_t> CommProcess::buildQrCodeSetAckBody(uint8_t result) const
{
    std::vector<uint8_t> body;
    // BY LZW: 0xF1 二维码设置应答：桩编号BCD7 + 下发结果BCD1。
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(intToBcdByte(result == 0x00 ? 0 : 1));
    return body;
}

std::vector<uint8_t> CommProcess::buildFeeModelSetAckBody(uint8_t gunNoBcd, uint8_t result) const
{
    (void)gunNoBcd;
    std::vector<uint8_t> body;
    // BY LZW: YLC PDF 9.6：0x57 计费模型设置应答：桩编号BCD7 + 设置结果BIN1。
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(result);
    return body;
}

uint16_t CommProcess::buildYlcOriginalHardwareFaultBits(uint8_t gun) const
{
    if (gun >= m_gunRuntimeData.size()) {
        return 0U;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    auto fault = [&rd](const char* key) -> bool {
        std::map<std::string, uint8_t>::const_iterator it = rd.yxFaultFields.find(key);
        return it != rd.yxFaultFields.end() && it->second != 0;
    };
    auto setBit = [](uint16_t& bits, uint8_t bitNo, bool on) {
        if (on && bitNo < 16U) {
            bits |= static_cast<uint16_t>(1U << bitNo);
        }
    };

    uint16_t bits = 0x0000U;
    // BY LZW: 严格按YLC附录13“0x13原始故障原因”16 bit位置映射，Bit0预留固定0。
    setBit(bits, 1, rd.yxEmergencyStopFault != 0 || fault("emergencyStopFault"));
    setBit(bits, 2, fault("noIdleModuleFault"));
    setBit(bits, 3, fault("cabinetOverTempFault") || fault("pileOverTempFault") ||
                    fault("cabinetOverTempAlarm") || fault("pileOverTempAlarm"));
    setBit(bits, 4, fault("lightningArresterFault"));
    setBit(bits, 5, fault("moduleCommFault") || fault("powerCtrlCommFault") ||
                    fault("switchModuleCommFault"));
    setBit(bits, 6, fault("insulationMonitorFault") || fault("insulationMonitorAlarm") ||
                    rd.startCompleteData.insulationFault != 0);
    setBit(bits, 7, fault("meterCommFault"));
    setBit(bits, 8, fault("cardReaderCommFault"));
    setBit(bits, 9, fault("rc10CommFault"));
    setBit(bits, 10, fault("fanFault") || fault("moduleFanFault"));
    setBit(bits, 11, fault("dcBusFuseFault"));
    setBit(bits, 12, fault("dcBusContactorFault") || rd.yxDcContactorStatus == 0x02);
    setBit(bits, 13, fault("pileDoorFault") || fault("cabinetDoorFault"));
    setBit(bits, 14, fault("acInputContactorStickFault"));
    setBit(bits, 15, fault("moduleDcShortFault") || fault("shortCircuitFault"));
    return bits;
}

std::vector<uint8_t> CommProcess::buildYlcHardwareFaultBits(uint8_t gun) const
{
    std::vector<uint8_t> bits(32U, 0x00);
    if (gun >= m_gunRuntimeData.size()) {
        return bits;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    auto fault = [&rd](const char* key) -> bool {
        std::map<std::string, uint8_t>::const_iterator it = rd.yxFaultFields.find(key);
        return it != rd.yxFaultFields.end() && it->second != 0;
    };
    auto stop = [&rd](const char* key) -> uint16_t {
        std::map<std::string, uint16_t>::const_iterator it = rd.stopCompleteFields.find(key);
        return (it != rd.stopCompleteFields.end()) ? it->second : 0U;
    };

    // BY LZW: 严格按YLC附录13的0x13扩展故障原因Byte/Bit位置映射。
    setYlcFaultBit(bits, 0, 0, fault("leakageFault"));
    setYlcFaultBit(bits, 0, 1, fault("guideFault") || fault("guideVoltageAbnormal") || fault("cpFault"));
    setYlcFaultBit(bits, 0, 2, fault("inputOverVoltageFault") || fault("dcBusOverVoltageFault") || fault("moduleDcOverVoltageFault"));
    setYlcFaultBit(bits, 0, 3, fault("inputUnderVoltageFault") || fault("dcBusUnderVoltageFault") || fault("moduleDcUnderVoltageFault"));
    setYlcFaultBit(bits, 0, 4, fault("moduleDcOverCurrentFault") || fault("dcBusOverCurrentFault"));
    setYlcFaultBit(bits, 0, 5, fault("groundFault") || fault("peBreakFault"));
    setYlcFaultBit(bits, 0, 6, fault("shortCircuitFault") || fault("moduleDcShortFault"));
    setYlcFaultBit(bits, 0, 7, fault("leakageSelfCheckFault"));

    setYlcFaultBit(bits, 1, 0, fault("dcContactorStickFault") || fault("acInputContactorStickFault") ||
                             fault("bridgeContactorStickFault"));
    setYlcFaultBit(bits, 1, 1, fault("diodeCheckAbnormal"));
    setYlcFaultBit(bits, 1, 2, fault("pileOverTempFault") || fault("cabinetOverTempFault") ||
                             fault("pileOverTempAlarm") || fault("cabinetOverTempAlarm"));

    setYlcFaultBit(bits, 2, 0, fault("cc1Fault"));
    setYlcFaultBit(bits, 2, 1, fault("bmsCommFault") || stop("timeoutBmsVehicleId") != 0 ||
                             stop("timeoutBmsReady") != 0 || stop("timeoutBcs") != 0 ||
                             stop("timeoutBcl") != 0 || stop("timeoutBst") != 0 ||
                             stop("timeoutBsm") != 0);
    setYlcFaultBit(bits, 2, 2, fault("pileCommTimeoutFault") || fault("powerCtrlCommTimeout") ||
                             stop("timeoutTimeSync") != 0 || stop("timeoutChargeReady") != 0 ||
                             stop("timeoutChargeStatus") != 0 || stop("timeoutChargeStop") != 0 ||
                             stop("timeoutChargeStat") != 0);
    setYlcFaultBit(bits, 2, 3, rd.yxEmergencyStopFault != 0 || fault("emergencyStopFault"));
    setYlcFaultBit(bits, 2, 4, fault("electronicLockFault") || rd.yxElectronicLockStatus == 0x02);
    setYlcFaultBit(bits, 2, 5, fault("meterCommFault"));
    setYlcFaultBit(bits, 2, 6, fault("insulationMonitorFault") || fault("insulationMonitorAlarm") ||
                             rd.startCompleteData.insulationFault != 0);
    setYlcFaultBit(bits, 2, 7, fault("fanFault") || fault("moduleFanFault"));

    setYlcFaultBit(bits, 3, 0, fault("bcpParamMismatchFault"));
    setYlcFaultBit(bits, 3, 1, fault("moduleCommFault") || fault("powerCtrlCommFault") ||
                             fault("switchModuleCommFault"));
    setYlcFaultBit(bits, 3, 2, fault("dcContactorStickFault") || fault("acInputContactorStickFault") ||
                             fault("bridgeContactorStickFault"));
    setYlcFaultBit(bits, 3, 3, fault("dcBusContactorFault") || fault("acInputContactorFault") ||
                             fault("bridgeContactorFault") || rd.yxDcContactorStatus == 0x02);
    setYlcFaultBit(bits, 3, 4, fault("groundFault") || fault("peBreakFault"));
    setYlcFaultBit(bits, 3, 5, fault("prechargeVoltageFail"));
    setYlcFaultBit(bits, 3, 6, fault("batteryVoltageMismatchFault"));
    setYlcFaultBit(bits, 3, 7, fault("outputVoltageOverMaxFault") || fault("dcBusOverVoltageFault") ||
                             fault("moduleDcOverVoltageFault"));

    setYlcFaultBit(bits, 4, 0, fault("interfaceOverTempFault") || fault("gunOverTempAlarm"));
    setYlcFaultBit(bits, 4, 1, fault("networkOfflineFault") || fault("fourGOfflineFault"));
    setYlcFaultBit(bits, 4, 2, fault("meterDataAbnormal"));
    setYlcFaultBit(bits, 4, 3, fault("moduleLongIdleFault"));
    setYlcFaultBit(bits, 4, 4, fault("moduleNoCurrentFault"));
    setYlcFaultBit(bits, 4, 5, fault("batteryReverseFault"));
    setYlcFaultBit(bits, 4, 6, fault("insulationOutsideVoltageFault"));
    setYlcFaultBit(bits, 4, 7, fault("bhmVoltageTooLowFault"));

    setYlcFaultBit(bits, 5, 0, fault("bsmBatteryAbnormalFault") || fault("bmsFaultByCtrl") ||
                             fault("bmsSendFaultInfo"));
    setYlcFaultBit(bits, 5, 1, fault("broAbnormalFault"));
    setYlcFaultBit(bits, 5, 2, fault("moduleDcOverVoltageFault"));
    setYlcFaultBit(bits, 5, 3, fault("moduleDcUnderVoltageFault"));
    setYlcFaultBit(bits, 5, 4, fault("modulePathAOverCurrentFault"));
    setYlcFaultBit(bits, 5, 5, fault("modulePathBOverCurrentFault"));
    setYlcFaultBit(bits, 5, 6, fault("modulePathUnbalanceFault"));
    setYlcFaultBit(bits, 5, 7, fault("modulePrimaryOverCurrentFault"));

    setYlcFaultBit(bits, 6, 0, fault("moduleDcOverCurrentFault"));
    setYlcFaultBit(bits, 6, 1, fault("moduleDcShortFault"));
    setYlcFaultBit(bits, 6, 2, fault("moduleDcOverVoltageFault"));
    setYlcFaultBit(bits, 6, 3, fault("moduleDcOverCurrentFault"));
    setYlcFaultBit(bits, 6, 4, fault("moduleCenterVoltageFault"));
    setYlcFaultBit(bits, 6, 5, fault("dcHardwireFault"));
    setYlcFaultBit(bits, 6, 6, fault("moduleOverTempFault"));
    setYlcFaultBit(bits, 6, 7, fault("batteryOverVoltageFault"));

    setYlcFaultBit(bits, 7, 0, fault("batteryUnderVoltageFault"));
    setYlcFaultBit(bits, 7, 1, fault("sciCommFault"));
    setYlcFaultBit(bits, 7, 2, fault("canCommFault"));
    setYlcFaultBit(bits, 7, 3, fault("dischargeFault"));
    setYlcFaultBit(bits, 7, 4, fault("contactorSwitchFault"));
    setYlcFaultBit(bits, 7, 5, fault("moduleOverTemp2Fault"));
    setYlcFaultBit(bits, 7, 6, fault("workModeFault"));
    setYlcFaultBit(bits, 7, 7, fault("fastSwitchFault"));

    setYlcFaultBit(bits, 8, 0, fault("reverseInputOverCurrentFault"));
    setYlcFaultBit(bits, 8, 1, fault("moduleAcOverVoltageFault") || fault("inputOverVoltageFault"));
    setYlcFaultBit(bits, 8, 2, fault("moduleAcUnderVoltageFault") || fault("inputUnderVoltageFault"));
    setYlcFaultBit(bits, 8, 3, fault("acInputUnbalanceFault"));
    setYlcFaultBit(bits, 8, 4, fault("moduleAcPhaseLossFault"));
    setYlcFaultBit(bits, 8, 5, fault("acFrequencyFault"));
    setYlcFaultBit(bits, 8, 6, fault("inputOverCurrentFault"));
    setYlcFaultBit(bits, 8, 7, fault("inputOcpFault"));

    setYlcFaultBit(bits, 9, 0, fault("dcBusOverVoltageFault"));
    setYlcFaultBit(bits, 9, 1, fault("dcBusUnderVoltageFault"));
    setYlcFaultBit(bits, 9, 2, fault("dcBusUnbalanceFault"));
    setYlcFaultBit(bits, 9, 3, fault("moduleAcPhaseLossFault"));
    setYlcFaultBit(bits, 9, 4, fault("pfcCommFault"));
    setYlcFaultBit(bits, 9, 5, fault("totalBusOverVoltageFault") || fault("dcBusOverVoltageFault"));
    setYlcFaultBit(bits, 9, 6, fault("totalBusUnderVoltageFault") || fault("dcBusUnderVoltageFault"));
    setYlcFaultBit(bits, 9, 7, fault("acInputPeakOverVoltageFault"));

    setYlcFaultBit(bits, 11, 0, fault("pfcOverTempFault"));
    setYlcFaultBit(bits, 11, 1, fault("modeSwitchFault"));
    setYlcFaultBit(bits, 11, 2, fault("pfcHardwireFault"));
    setYlcFaultBit(bits, 11, 3, fault("chargeCurrentOverBmsLimitFault"));
    setYlcFaultBit(bits, 11, 4, fault("cardReaderCommFault"));
    setYlcFaultBit(bits, 11, 5, fault("eepromFault"));
    setYlcFaultBit(bits, 11, 6, fault("pileLockedFault"));
    setYlcFaultBit(bits, 11, 7, fault("vinMatchFail"));

    setYlcFaultBit(bits, 13, 1, fault("fanFault") || fault("moduleFanFault"));
    setYlcFaultBit(bits, 13, 4, fault("cabinetOverTempFault") || fault("moduleOverTempFault"));
    setYlcFaultBit(bits, 13, 5, fault("pileOverTempFault") || fault("cabinetOverTempAlarm"));
    setYlcFaultBit(bits, 15, 0, fault("auxPowerFault"));

    // BY LZW: Byte18~Byte22 仅在当前交易 stop_complete 明确给出 BMS 原因/超时字段时置位。
    const uint16_t bmsStopReason = stop("bmsStopReason");
    const uint16_t bmsFaultReason = stop("bmsChargeFaultReason");
    const uint16_t bmsErrorReason = stop("bmsStopErrorReason");
    setYlcFaultBit(bits, 18, 0, bmsStopReason != 0);
    setYlcFaultBit(bits, 18, 1, (bmsFaultReason & 0x0001U) != 0);
    setYlcFaultBit(bits, 18, 2, (bmsFaultReason & 0x0002U) != 0);
    setYlcFaultBit(bits, 18, 3, (bmsFaultReason & 0x0004U) != 0);
    setYlcFaultBit(bits, 18, 4, (bmsFaultReason & 0x0008U) != 0);
    setYlcFaultBit(bits, 18, 5, (bmsFaultReason & 0x0010U) != 0);
    setYlcFaultBit(bits, 19, 0, bmsErrorReason != 0);
    setYlcFaultBit(bits, 19, 1, (bmsFaultReason & 0x0020U) != 0);
    setYlcFaultBit(bits, 19, 2, (bmsFaultReason & 0x0040U) != 0);
    setYlcFaultBit(bits, 19, 3, (bmsFaultReason & 0x0080U) != 0);

    setYlcFaultBit(bits, 20, 0, stop("timeoutTimeSync") != 0);
    setYlcFaultBit(bits, 20, 1, stop("timeoutChargeReady") != 0);
    setYlcFaultBit(bits, 20, 2, stop("timeoutChargeStatus") != 0 || stop("timeoutCml") != 0);
    setYlcFaultBit(bits, 20, 3, stop("timeoutChargeStop") != 0 || stop("timeoutBst") != 0);
    setYlcFaultBit(bits, 20, 4, stop("timeoutChargeStat") != 0);
    setYlcFaultBit(bits, 20, 5, stop("timeoutBmsVehicleId") != 0);
    setYlcFaultBit(bits, 20, 6, stop("timeoutBatteryParams") != 0);
    setYlcFaultBit(bits, 21, 0, stop("timeoutBmsReady") != 0);
    setYlcFaultBit(bits, 21, 1, stop("timeoutBatteryStatus") != 0 || stop("timeoutBcs") != 0);
    setYlcFaultBit(bits, 21, 2, stop("timeoutBatteryReq") != 0 || stop("timeoutBcl") != 0);
    setYlcFaultBit(bits, 21, 3, stop("timeoutBmsStop") != 0);
    setYlcFaultBit(bits, 21, 4, stop("timeoutBmsStat") != 0);
    setYlcFaultBit(bits, 21, 5, stop("timeoutBsm") != 0);
    // BY LZW: Byte23~Byte31 协议未定义，无平台补充表，固定0。
    return bits;
}

std::vector<uint8_t> CommProcess::buildChargeInfoBody(uint8_t gun)
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size() || gun >= m_config.gunIdList.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    body.reserve(60U);

    uint8_t ylcStatus = rd.gunStatus;
    if (ylcStatus > 0x03U) {
        ylcStatus = 0x02U;
    }
    const bool charging = (ylcStatus == 0x03U);

    // BY LZW: 待机/非运行态实时信息不再携带上一笔交易流水号，避免平台把待机0x13误关联到旧订单。
    appendBcdFixed(body, charging ? rd.orderNo : std::string(), 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(intToBcdByte(static_cast<int>(gun) + 1));
    body.push_back(ylcStatus);

    uint8_t gunSeatStatus = 0x02U;
    if (rd.yxGunSeatStatusValid) {
        if (rd.yxGunSeatStatus == 0x00U) {
            gunSeatStatus = 0x00U;
        } else if (rd.yxGunSeatStatus == 0x01U) {
            gunSeatStatus = 0x01U;
        } else {
            gunSeatStatus = 0x02U;
        }
    }
    body.push_back(gunSeatStatus);
    const bool forcePlugged = (gun < m_forcePluggedChargeInfoByGun.size() &&
                               m_forcePluggedChargeInfoByGun[gun] != 0);
    body.push_back((forcePlugged || rd.yxVehicleConnectStatus) ? 0x01 : 0x00);

    const double reportVoltage = charging ? rd.voltage : 0.0;
    const double reportCurrent = charging ? std::fabs(rd.current) : 0.0;
    const double reportInterfaceTemp = charging ? rd.interfaceTemp1 : -50.0;
    const double reportSoc = charging ? rd.soc : 0.0;
    const double reportBatteryMaxTemp = charging ? rd.batteryMaxTemp : -50.0;
    const double reportChargedTimeSec = charging ? rd.chargedTime : 0.0;
    const double reportRemainTimeMin = charging ? rd.estimatedRemainTime : 0.0;
    const double reportTotalEnergy = charging ? rd.totalEnergy : 0.0;
    const double reportElectricAmount = charging ? rd.electricAmount : 0.0;
    const double reportServiceAmount = charging ? rd.serviceAmount : 0.0;

    appendU16LE(body, scaleToU16(reportVoltage, 10.0));
    appendU16LE(body, scaleToU16(reportCurrent, 10.0));
    body.push_back(clampToU8(static_cast<int>(reportInterfaceTemp + 50.0)));
    body.insert(body.end(), 8, 0x00);

    body.push_back(clampToU8(static_cast<int>(reportSoc + 0.5)));
    body.push_back(clampToU8(static_cast<int>(reportBatteryMaxTemp + 50.0)));
    appendU16LE(body, scaleToU16(reportChargedTimeSec / 60.0, 1.0));
    appendU16LE(body, scaleToU16(reportRemainTimeMin, 1.0));
    appendU32LE(body, scaleToU32(reportTotalEnergy, 10000.0));
    appendU32LE(body, scaleToU32(reportTotalEnergy, 10000.0));
    appendU32LE(body, scaleToU32(reportElectricAmount + reportServiceAmount, 10000.0));
    appendU16LE(body, buildYlcOriginalHardwareFaultBits(gun));
    // BY ZF: 云快充V1.6 0x13到2字节硬件故障结束，不追加易联桩扩展分时/S2/故障码。

    return body;
}

std::vector<uint8_t> CommProcess::buildDischargeInfoBody(uint8_t gun)
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size() || gun >= m_config.gunIdList.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    body.reserve(208U);

    const bool isDischarging = rd.lastBusinessIsDischarge && rd.gunStatus == 0x03U;

    // BY LZW: 待机/非放电态实时信息不再携带上一笔交易流水号，避免平台把待机0xE7误关联到旧订单。
    appendBcdFixed(body, isDischarging ? rd.orderNo : std::string(), 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    body.push_back(intToBcdByte(static_cast<int>(gun) + 1));

    body.push_back(0x01); // 放电枪类型：1=直流。
    uint8_t workStatus = isDischarging ? 0x04U : 0x02U;
    if (rd.gunStatus == 0x00U) {
        workStatus = 0x00U;
    } else if (rd.yxTotalFault != 0U || rd.yxOtherFault != 0U || rd.yxEmergencyStopFault != 0U) {
        workStatus = 0x01U;
    } else if (!rd.lastBusinessIsDischarge && rd.gunStatus == 0x03U) {
        workStatus = 0x03U;
    }
    body.push_back(workStatus);
    body.push_back(rd.yxVehicleConnectStatus ? 0x01 : 0x00);

    const double reportSoc = isDischarging ? rd.soc : 0.0;
    const double reportDischargeTimeSec = isDischarging ? rd.chargedTime : 0.0;
    const double reportTotalEnergy = isDischarging ? rd.totalEnergy : 0.0;
    const double reportElectricAmount = isDischarging ? rd.electricAmount : 0.0;
    const double reportServiceAmount = isDischarging ? rd.serviceAmount : 0.0;
    const double reportVoltage = isDischarging ? rd.voltage : 0.0;
    const double reportCurrent = isDischarging ? std::fabs(rd.current) : 0.0;

    body.push_back(clampToU8(static_cast<int>(reportSoc + 0.5)));
    appendU32LE(body, scaleToU32(reportDischargeTimeSec / 60.0, 1.0));
    appendU32LE(body, scaleToU32(reportTotalEnergy, 10000.0));
    appendU32LE(body, scaleToU32(reportElectricAmount + reportServiceAmount, 10000.0));

    appendU16LE(body, scaleToU16(reportVoltage, 10.0));
    appendU16LE(body, scaleToU16(reportCurrent, 10.0));

    // BY LZW: 当前运行态无交流三相拆分来源，直流放电首版交流字段置0。
    for (int i = 0; i < 6; ++i) {
        appendU16LE(body, 0U);
    }

    appendAsciiFixed(body, rd.chargeUserNo, 32, 0x00);
    const double meterEnd = rd.meterEnergy;
    const double meterStart = (meterEnd > rd.totalEnergy) ? (meterEnd - rd.totalEnergy) : 0.0;
    appendU64LE(body, scaleToU64(meterStart, 1000.0));
    appendU64LE(body, scaleToU64(meterEnd, 1000.0));

    const double dischargePowerKw = std::fabs(reportVoltage * reportCurrent) / 1000.0;
    appendU32LE(body, scaleToU32(dischargePowerKw, 10.0)); // BY LZW: 0xE7 放电功率按0.1kW编码。
    body.push_back(clampToU8(static_cast<int>(rd.interfaceTemp1 + 50.0)));

    std::string vin = rd.pendingPlugVinReportVin;
    if (vin.empty()) {
        vin = rd.pendingVinAuthVin;
    }
    appendAsciiFixed(body, sanitizeVin17(vin), 17, 0x00);

    double catEnergy[4] = {0.0, 0.0, 0.0, 0.0};
    double catElectricAmount[4] = {0.0, 0.0, 0.0, 0.0};
    double catServiceAmount[4] = {0.0, 0.0, 0.0, 0.0};
    const FeeModel* liveFeeModel = (gun < m_feeModelByGun.size()) ? &m_feeModelByGun[gun] : nullptr;
    if (!isDischarging) {
        // BY LZW: 0xE7协议要求待机时SOC、时长、电量、金额、电压/电流和分时费用置零，运行态缓存保留给交易记录使用。
    } else if (rd.feeSegments.size() == 4U) {
        for (size_t i = 0; i < 4U; ++i) {
            catEnergy[i] = rd.feeSegments[i].energyKwh;
            catElectricAmount[i] = rd.feeSegments[i].electricAmount;
            catServiceAmount[i] = rd.feeSegments[i].serviceAmount;
        }
    } else if (!rd.feeSegments.empty() &&
               liveFeeModel &&
               liveFeeModel->segFlag.size() >= rd.feeSegments.size()) {
        for (size_t i = 0; i < rd.feeSegments.size(); ++i) {
            unsigned int flag = liveFeeModel->segFlag[i];
            if (flag < 1U || flag > 4U) {
                flag = 3U;
            }
            const size_t idx = static_cast<size_t>(flag - 1U);
            catEnergy[idx] += rd.feeSegments[i].energyKwh;
            catElectricAmount[idx] += rd.feeSegments[i].electricAmount;
            catServiceAmount[idx] += rd.feeSegments[i].serviceAmount;
        }
    } else {
        catEnergy[2] = reportTotalEnergy;
        catElectricAmount[2] = reportElectricAmount;
        catServiceAmount[2] = reportServiceAmount;
    }

    for (int i = 0; i < 4; ++i) {
        appendU32LE(body, scaleToU32(catEnergy[i], 10000.0));
    }
    for (int i = 0; i < 4; ++i) {
        appendU32LE(body, scaleToU32(catElectricAmount[i], 10000.0));
    }
    for (int i = 0; i < 4; ++i) {
        appendU32LE(body, scaleToU32(catServiceAmount[i], 10000.0));
    }

    const std::vector<uint8_t> faultBits = buildYlcHardwareFaultBits(gun);
    appendU16LE(body, buildYlcOriginalHardwareFaultBits(gun));
    body.insert(body.end(), faultBits.begin(), faultBits.end());
    if (body.size() != 208U) {
        m_logSender.warn("ylc_discharge_info_body_len",
                         std::string("len=") + std::to_string(body.size()));
    }
    return body;
}

std::vector<uint8_t> CommProcess::buildDischargeCmdResultBody(uint8_t gun, uint8_t cmdType, cJSON* eventData) const
{
    std::vector<uint8_t> body;
    if (gun >= m_gunRuntimeData.size()) {
        return body;
    }

    const GunRuntimeData& rd = m_gunRuntimeData[gun];
    appendBcdFixed(body, rd.orderNo, 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    body.push_back(cmdType);

    int successFlag = 1;
    if (eventData) {
        (void)jsonGetInt(eventData, "successFlag", successFlag);
    }
    int failReason = 0;
    if (eventData) {
        (void)jsonGetInt(eventData, "chargeFailReason", failReason);
        if (failReason == 0) {
            (void)jsonGetInt(eventData, "failReason", failReason);
        }
    }
    if (successFlag != 0 && failReason == 0) {
        failReason = 0xFF;
    }
    const uint32_t result = (successFlag == 0) ? 0x00000000U : mapYlcDischargeStartFailReason(failReason, gun);
    appendU32LE(body, result);
    return body;
}

bool CommProcess::buildDischargeRecordBodyFromUpdateRecord(uint8_t gun, cJSON* data, std::vector<uint8_t>& body)
{
    if (!data || gun >= m_gunRuntimeData.size()) {
        return false;
    }

    body.clear();
    body.reserve(170U);

    const std::string tradeNo = jsonGetString(data, "tradeNo");
    const std::string feeModelIdFromRecord = jsonGetString(data, "feeModelId");
    const std::string cardNumber = jsonGetString(data, "cardNumber");
    std::string vin = jsonGetString(data, "vinCode");
    if (vin.empty()) {
        vin = jsonGetString(data, "vin");
    }

    auto getNumberAny = [data](const char* a, const char* b, const char* c) {
        double v = 0.0;
        if (a && jsonGetNumber(data, a, v)) return v;
        if (b && jsonGetNumber(data, b, v)) return v;
        if (c && jsonGetNumber(data, c, v)) return v;
        return 0.0;
    };
    auto getIntAny = [data](const char* a, const char* b, const char* c, int defVal) {
        int v = defVal;
        if (a && jsonGetInt(data, a, v)) return v;
        if (b && jsonGetInt(data, b, v)) return v;
        if (c && jsonGetInt(data, c, v)) return v;
        return defVal;
    };
    auto getTimeAny = [data](const char* a, const char* b, const char* c) {
        double v = 0.0;
        if (a && jsonGetNumber(data, a, v)) return static_cast<uint64_t>(v);
        if (b && jsonGetNumber(data, b, v)) return static_cast<uint64_t>(v);
        if (c && jsonGetNumber(data, c, v)) return static_cast<uint64_t>(v);
        return 0ULL;
    };
    auto getArrayNumberDirect = [](cJSON* arr, int index) -> double {
        if (!arr || !cJSON_IsArray(arr) || index < 0 || index >= cJSON_GetArraySize(arr)) {
            return 0.0;
        }
        cJSON* item = cJSON_GetArrayItem(arr, index);
        return (item && cJSON_IsNumber(item)) ? item->valuedouble : 0.0;
    };
    auto arraySizeOrZero = [](cJSON* arr) -> int {
        return cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    };

    const uint64_t startTime = getTimeAny("dischargeStartTime", "chargeStartTime", "startTime");
    const uint64_t endTime = getTimeAny("dischargeEndTime", "chargeEndTime", "endTime");
    double durationSec = getNumberAny("dischargeDuration", "chargeDuration", "chargedTime");
    if (durationSec <= 0.0 && endTime > startTime) {
        durationSec = static_cast<double>((endTime - startTime) / 1000ULL);
    }

    const int startSoc = getIntAny("startSoc", "beginSoc", nullptr, 0);
    const int endSoc = getIntAny("endSoc", "stopSoc", nullptr, 0);
    const int startType = getIntAny("startType", "chargeMode", nullptr, 1);
    const double totalElect = getNumberAny("dischargeElect", "totalElect", "totalEnergy");
    const double totalCost = getNumberAny("dischargeTotalCost", "totalCost", "totalAmount");
    const double totalPowerCost = getNumberAny("dischargePowerCost", "totalPowerCost", "electricAmount");
    const double totalServCost = getNumberAny("dischargeServCost", "totalServCost", "serviceAmount");
    const double sumStart = getNumberAny("dischargeSumStart", "sumStart", "meterStart");
    const double sumEnd = getNumberAny("dischargeSumEnd", "sumEnd", "meterEnd");
    cJSON* partElectArr = cJSON_GetObjectItem(data, "partElect");
    cJSON* chargeFeeArr = cJSON_GetObjectItem(data, "chargeFee");
    cJSON* serviceFeeArr = cJSON_GetObjectItem(data, "serviceFee");

    // BY LZW: 0xE9交易流水号与0x3B保持一致，不再强制校验32位/桩号/枪号，沿用启动侧交易号组帧。
    if (tradeNo.empty()) {
        m_logSender.warn("ylc_discharge_record_empty_trade_no",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)));
        return false;
    }
    if (!isValidYlcRecordTime(startTime) || !isValidYlcRecordTime(endTime)) {
        m_logSender.warn("ylc_discharge_record_invalid_time",
                         std::string("tradeNo=") + tradeNo +
                         ",start=" + std::to_string(static_cast<unsigned long long>(startTime)) +
                         ",end=" + std::to_string(static_cast<unsigned long long>(endTime)));
        return false;
    }

    const FeeModel* matchedFeeModel = nullptr;
    const char* matchedFeeModelSource = "none";
    if (gun < m_tradeRecordFeeModelByGun.size()) {
        const FeeModel& recordFeeModel = m_tradeRecordFeeModelByGun[gun];
        if (!feeModelIdFromRecord.empty() &&
            !recordFeeModel.feeModelId.empty() &&
            feeModelIdFromRecord == recordFeeModel.feeModelId) {
            matchedFeeModel = &recordFeeModel;
            matchedFeeModelSource = "trade_record_cache";
        }
    }
    if (!matchedFeeModel && gun < m_feeModelByGun.size()) {
        const FeeModel& localFeeModel = m_feeModelByGun[gun];
        if (!feeModelIdFromRecord.empty() &&
            !localFeeModel.feeModelId.empty() &&
            feeModelIdFromRecord == localFeeModel.feeModelId) {
            matchedFeeModel = &localFeeModel;
            matchedFeeModelSource = "runtime_cache";
        }
    }
    if (!feeModelIdFromRecord.empty()) {
        std::ostringstream oss;
        oss << "gun=" << static_cast<int>(gun)
            << ",req=" << feeModelIdFromRecord
            << ",source=" << matchedFeeModelSource;
        if (matchedFeeModel) {
            oss << ",hit=" << matchedFeeModel->feeModelId
                << ",timeNum=" << static_cast<int>(matchedFeeModel->timeNum)
                << ",timeSeg=" << matchedFeeModel->timeSeg.size();
        }
        m_logSender.info("discharge_record_fee_model_match", oss.str());
        if (std::strcmp(matchedFeeModelSource, "runtime_cache") == 0) {
            m_logSender.warn("discharge_record_fee_model_runtime_fallback", oss.str());
        } else if (!matchedFeeModel) {
            m_logSender.warn("discharge_record_fee_model_miss", oss.str());
        }
    } else {
        m_logSender.warn("discharge_record_fee_model_empty",
                         std::string("gun=") + std::to_string(static_cast<int>(gun)));
    }

    double catEnergy[4] = {0.0, 0.0, 0.0, 0.0};
    double catElectricAmount[4] = {0.0, 0.0, 0.0, 0.0};
    double catServiceAmount[4] = {0.0, 0.0, 0.0, 0.0};
    bool hasCategoryDetail = false;
    auto addCategory = [&](size_t idx, double energy, double electricAmount, double serviceAmount) {
        if (idx > 3U) {
            idx = 2U;
        }
        catEnergy[idx] += energy;
        catElectricAmount[idx] += electricAmount;
        catServiceAmount[idx] += serviceAmount;
        hasCategoryDetail = true;
    };
    int periodCount = arraySizeOrZero(partElectArr);
    if (periodCount > 0 && arraySizeOrZero(chargeFeeArr) > 0) {
        periodCount = std::min(periodCount, arraySizeOrZero(chargeFeeArr));
    }
    if (periodCount > 0 && arraySizeOrZero(serviceFeeArr) > 0) {
        periodCount = std::min(periodCount, arraySizeOrZero(serviceFeeArr));
    }
    if (periodCount > 48) {
        periodCount = 48;
    }
    if (periodCount == 4) {
        for (int i = 0; i < 4; ++i) {
            addCategory(static_cast<size_t>(i),
                        getArrayNumberDirect(partElectArr, i),
                        getArrayNumberDirect(chargeFeeArr, i),
                        getArrayNumberDirect(serviceFeeArr, i));
        }
    } else if (periodCount > 0 && matchedFeeModel &&
               matchedFeeModel->segFlag.size() >= static_cast<size_t>(periodCount)) {
        for (int i = 0; i < periodCount; ++i) {
            unsigned int flag = matchedFeeModel->segFlag[static_cast<size_t>(i)];
            if (flag < 1U || flag > 4U) {
                flag = 3U;
            }
            addCategory(static_cast<size_t>(flag - 1U),
                        getArrayNumberDirect(partElectArr, i),
                        getArrayNumberDirect(chargeFeeArr, i),
                        getArrayNumberDirect(serviceFeeArr, i));
        }
    } else if (periodCount > 0) {
        for (int i = 0; i < std::min(periodCount, 4); ++i) {
            addCategory(static_cast<size_t>(i),
                        getArrayNumberDirect(partElectArr, i),
                        getArrayNumberDirect(chargeFeeArr, i),
                        getArrayNumberDirect(serviceFeeArr, i));
        }
    }
    if (!hasCategoryDetail) {
        addCategory(2U, totalElect, totalPowerCost, totalServCost);
    }

    appendBcdFixed(body, tradeNo, 16);
    appendPileCodeBcd7(body, m_config.cdzNo);
    const int gunNo = static_cast<int>(gun) + 1;
    body.push_back(static_cast<uint8_t>(((gunNo / 10) << 4) | (gunNo % 10)));
    appendCp56Time2aFromYmdHms(body, startTime);
    appendCp56Time2aFromYmdHms(body, endTime);
    appendU32LE(body, scaleToU32(durationSec, 1.0));
    appendAsciiFixed(body, cardNumber, 32, 0x00);
    body.push_back(clampToU8(startSoc));
    body.push_back(clampToU8(endSoc));
    appendU32LE(body, mapYlcDischargeEndReason(data));
    appendAsciiFixed(body, sanitizeVin17(vin), 17, 0x00);
    appendU32LE(body, scaleToU32(totalElect, 10000.0));
    appendU64LE(body, scaleToU64(sumStart, 1000.0));
    appendU64LE(body, scaleToU64(sumEnd, 1000.0));

    uint8_t tradeFlag = 1; // 远程启动-插枪
    if (startType == 0 || startType == 2 || startType == 3 || startType == 4) {
        tradeFlag = static_cast<uint8_t>(startType);
    }
    body.push_back(tradeFlag);
    const double totalAmount = (totalCost > 0.0) ? totalCost : (totalPowerCost + totalServCost);
    appendU32LE(body, scaleToU32(totalAmount, 10000.0));

    for (int i = 0; i < 4; ++i) {
        appendU32LE(body, scaleToU32(catEnergy[i], 10000.0));
    }
    for (int i = 0; i < 4; ++i) {
        appendU32LE(body, scaleToU32(catElectricAmount[i], 10000.0));
    }
    for (int i = 0; i < 4; ++i) {
        appendU32LE(body, scaleToU32(catServiceAmount[i], 10000.0));
    }

    if (gun < m_gunRuntimeData.size()) {
        m_gunRuntimeData[gun].pendingRecordTradeNo = tradeNo;
    }
    if (body.size() != 170U) {
        m_logSender.warn("ylc_discharge_record_body_len",
                         std::string("len=") + std::to_string(body.size()));
    }
    return true;
}

void CommProcess::reportChargeInfoPeriodic()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const uint8_t count = static_cast<uint8_t>(m_gunRuntimeData.size());
    for (uint8_t gun = 0; gun < count; ++gun) {
        if (gun >= m_lastChargeInfoReportByGun.size() || gun >= m_runtimeChangedByGun.size()) {
            continue;
        }
        const bool forceSend = (m_runtimeChangedByGun[gun] != 0) ||
                               (gun < m_forcePluggedChargeInfoByGun.size() && m_forcePluggedChargeInfoByGun[gun] != 0);
        const bool charging = (m_gunRuntimeData[gun].gunStatus == 0x03);
        const bool discharging = m_gunRuntimeData[gun].lastBusinessIsDischarge && charging;
        const std::chrono::seconds interval = discharging
            ? std::chrono::seconds(30)
            : (charging ? std::chrono::seconds(15) : std::chrono::seconds(300));
        if (!forceSend && (now - m_lastChargeInfoReportByGun[gun] < interval)) {
            continue;
        }
        const std::vector<uint8_t> body = discharging ? buildDischargeInfoBody(gun) : buildChargeInfoBody(gun);
        if (!body.empty()) {
            sendPlatformFrame(discharging ? kCmdDischargeInfo : kCmdChargeInfo, body);
            // BY LZW: 充电中每15秒同步上送0x23 BCL/BCS/CCS信息。
            if (charging && !discharging) {
                const std::vector<uint8_t> bclBcsCcsBody = buildBclBcsCcsBody(gun);
                if (!bclBcsCcsBody.empty()) {
                    sendPlatformFrame(kCmdBclBcsCcs, bclBcsCcsBody);
                }
            }
            // BY LZW: 充电中每15秒同步上送0x25 BSM信息。
            if (charging && !discharging) {
                const std::vector<uint8_t> bsmBody = buildBsmBody(gun);
                if (!bsmBody.empty()) {
                    sendPlatformFrame(kCmdBsm, bsmBody);
                }
            }
            m_lastChargeInfoReportByGun[gun] = now;
            m_runtimeChangedByGun[gun] = 0;
            if (gun < m_forcePluggedChargeInfoByGun.size()) {
                m_forcePluggedChargeInfoByGun[gun] = 0;
            }
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
        if (m_nextLoginAllowedTime.time_since_epoch().count() != 0 &&
            now < m_nextLoginAllowedTime) {
            break;
        }
        // BY LZW: YLC 首版默认明文登录，不准备 Zhongshihua2.0 的 SM2/SM4 登录上下文。
        resetCryptoSession();
        resetOfflineReplayState();
        m_loginState = LOGIN_REQ_AUTH;
        m_lastLoginAction = now - std::chrono::seconds(m_config.loginRetrySec);
        break;
    case LOGIN_UPLOAD_OFFLINE_DATA:
        driveOfflineReplayState(now);
        break;
    case LOGIN_REQ_FEE_MODEL: {
        if (now - m_lastLoginAction >= std::chrono::seconds(m_config.loginRetrySec)) {
            if (sendPlatformFrame(kCmdFeeModelReq, buildFeeModelRequestBody(0x00))) {
                m_logSender.info("platform_login_step", "fee_model_req_sent");
            } else {
                closePlatformTcp();
            }
            m_lastLoginAction = now;
        }
        break;
    }
    case LOGIN_REQ_TIME_SYNC:
        // BY LZW: YLC 对时由平台0x56主动下发；兼容状态不主动发包，直接回到在线。
        m_loginState = LOGIN_ONLINE;
        break;
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
    case LOGIN_REQ_FEE_MODEL_CHECK:
        if (now - m_lastLoginAction >= std::chrono::seconds(m_config.loginRetrySec)) {
            if (sendPlatformFrame(kCmdFeeModelCheckReq, buildFeeModelCheckRequestBody())) {
                m_logSender.info("platform_login_step", "fee_model_check_req_sent");
            } else {
                closePlatformTcp();
            }
            m_lastLoginAction = now;
        }
        break;
    case LOGIN_ONLINE: {
        if (now - m_lastHeartbeat >= std::chrono::seconds(m_config.tcpHeartbeatSec)) {
            if (!sendPlatformFrame(kCmdHeartbeat, buildHeartbeatBody())) {
                m_logSender.warn("platform_heartbeat", "send_fail");
                if (m_config.debugTcp) {
                    std::cout << "[Comm][TCP][CLOSE_REASON] heartbeat_send_fail" << std::endl;
                }
                closePlatformTcp();
            } else {
                m_lastHeartbeat = now;
                if (m_heartbeatCounter < 3U) {
                    ++m_heartbeatCounter;
                }
                if (!m_config.offlineRunMode &&
                    !m_platformOfflineTimeoutReported &&
                    m_heartbeatCounter >= 3U) {
                    m_platformOfflineTimeoutReported = true;
                    if (m_platformOnlineEventActive) {
                        m_platformOnlineEventActive = false;
                    }
                    publishPlatformLinkEvent(false, "heartbeat_timeout");
                    // BY LZW: YLC按协议连续3次未收到0x04心跳回复判超时，超时后保持原断链重登动作。
                    closePlatformTcp();
                    break;
                }
            }
        }
        bool hasRuntimeChange = false;
        for (size_t i = 0; i < m_runtimeChangedByGun.size(); ++i) {
            if (m_runtimeChangedByGun[i] != 0) {
                hasRuntimeChange = true;
                break;
            }
        }
        // BY LZW: 外层每15秒调度一次；任一枪运行态变化时立即触发一次调度。
        if (hasRuntimeChange || now - m_lastChargeInfoReport >= std::chrono::seconds(15)) {
            reportChargeInfoPeriodic();
            m_lastChargeInfoReport = now;
        }
        drivePlugVinReportRetries(now);
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
    drivePendingTradeRecordUploads(now);
    drivePendingRemoteStartReconnect(now);
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

    while (m_tcpRxCache.size() >= kYlcMinFrameLen) {
        // BY LZW: 同步头 0x68。
        if (static_cast<uint8_t>(m_tcpRxCache[0]) != kYlcFrameHead) {
            m_tcpRxCache.erase(m_tcpRxCache.begin());
            continue;
        }
        const size_t dataLen = static_cast<size_t>(static_cast<uint8_t>(m_tcpRxCache[1]));
        if (dataLen > kYlcMaxDataLen) {
            m_logSender.warn("platform_rx_frame_too_long", std::to_string(dataLen));
            m_tcpRxCache.erase(m_tcpRxCache.begin());
            continue;
        }
        const size_t frameLen = kYlcFrameHeadLen + kYlcLengthLen + dataLen + kYlcCrcLen;
        if (dataLen < kYlcFixedDataLen || frameLen < kYlcMinFrameLen) {
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
    if (!frame || frameLen < kYlcMinFrameLen) {
        return;
    }
    if (frame[0] != kYlcFrameHead) {
        return;
    }
    const size_t dataLen = static_cast<size_t>(frame[1]);
    if (dataLen < kYlcFixedDataLen ||
        dataLen > kYlcMaxDataLen ||
        frameLen != kYlcFrameHeadLen + kYlcLengthLen + dataLen + kYlcCrcLen) {
        return;
    }
    const uint16_t rxSeq = static_cast<uint16_t>(frame[2] | (static_cast<uint16_t>(frame[3]) << 8));
    // BY LZW: 帧尾CRC按小端序读取。
    const uint16_t recvCrc = static_cast<uint16_t>(
                static_cast<uint16_t>(frame[frameLen - 2]) |
                (static_cast<uint16_t>(frame[frameLen - 1]) << 8));
    const uint16_t calcCrc = calcYlcCrc16(frame + 2, dataLen);
    if (recvCrc != calcCrc) {
        if (m_config.debugTcp) {
            std::cout << "[Comm][TCP][RX_CRC_FAIL] recv=0x" << std::hex << recvCrc
                      << " calc=0x" << calcCrc << std::dec
                      << " len=" << frameLen << std::endl;
        }
        return;
    }
    const uint8_t encryptFlag = frame[4];
    const uint8_t cmd = frame[5];
    if (!isDischargeFrameCmd(cmd) && dataLen > kYkcChargeMaxDataLen) {
        m_logSender.warn("platform_rx_frame_too_long",
                         std::string("cmd=") + std::to_string(cmd) +
                         ",dataLen=" + std::to_string(dataLen));
        return;
    }
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][RX_FRAME] cmd=0x" << std::hex << static_cast<int>(cmd) << std::dec
                  << "(" << platformCmdName(cmd) << ")"
                  << " len=" << frameLen << std::endl;
    }
    if (dataLen < kYlcFixedDataLen) {
        return;
    }
    const size_t bodyLen = dataLen - kYlcFixedDataLen;
    const uint8_t* bodyRaw = (bodyLen > 0) ? (frame + 6) : nullptr;
    std::vector<uint8_t> decBody;
    if (!decryptYlcBody(encryptFlag, bodyRaw, bodyLen, decBody)) {
        if (m_config.debugTcp) {
            std::cout << "[Comm][TCP][RX_BODY_DEC_FAIL] cmd=0x" << std::hex << static_cast<int>(cmd) << std::dec
                      << " encFlag=0x" << std::hex << static_cast<int>(encryptFlag) << std::dec
                      << " bodyLen=" << bodyLen
                      << std::endl;
        }
        if (encryptFlag == kYlcEncrypt3Des) {
            m_logSender.warn("platform_rx_3des_unsupported", std::string("cmd=") + std::to_string(cmd));
        } else {
            m_logSender.warn("platform_rx_encrypt_flag_unknown",
                             std::string("cmd=") + std::to_string(cmd) +
                             " encFlag=" + std::to_string(encryptFlag));
        }
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
    // BY LZW: 关键业务指令接收留痕，便于联调追踪关键时刻。
    switch (cmd) {
    case kCmdMergeChargeApply:
    case kCmdMergeChargeApplyAck:
    case kCmdMergeStartReply:
    case kCmdRemoteMergeStart:
    case kCmdStartApply:
    case kCmdStartApplyAck:
    case kCmdRemoteStartAck:
    case kCmdRemoteStartCmd:
    case kCmdRemoteStopAck:
    case kCmdRemoteStopCmd:
    case kCmdUploadTradeRecord:
    case kCmdRecordConfirm:
    case kCmdBalanceUpdate:
    case kCmdOfflineCardSync:
    case kCmdOfflineCardClear:
    case kCmdOfflineCardQuery:
    case kCmdWorkParamSet:
    case kCmdParkingLockCtrl:
    case kCmdRemoteReboot:
    case kCmdRemoteUpdate:
    case kCmdRemoteLogRequest:
    case kCmdPlugVinReportAck:
    case kCmdPlugVinQuery:
    case kCmdFeeModelCheckAck:
    case kCmdFeeModelAck:
    case kCmdFeeModelSet:
    case kCmdQrCodeSet:
    case kCmdReadChargeInfo:
    case kCmdDischargeStartCmd:
    case kCmdDischargeStopCmd:
    case kCmdReadDischargeInfo:
    case kCmdDischargeRecordConfirm:
        {
            std::ostringstream oss;
            oss << "cmd=0x" << std::hex << std::uppercase << static_cast<int>(cmd);
            m_logSender.info("plat_cmd_rx", oss.str());
        }
        break;
    default:
        break;
    }

    if (cmd == kCmdBalanceUpdate) {
        uint8_t gun = 0;
        std::vector<uint8_t> physicalCard;
        cJSON* data = nullptr;
        uint8_t result = 0x01; // BY LZW: 0x41 PDF定义0x00成功，0x01设备编号错误，0x02卡号错误。
        if (parseYlcBalanceUpdate042(body, decBodyLen, gun, physicalCard, &data)) {
            const bool ok = publishPlatCommand(gun, "balance_update", data);
            result = ok ? 0x00 : 0x02;
            m_logSender.info("platform_balance_update",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                             ",publish=" + (ok ? "ok" : "fail"));
        } else {
            if (body && decBodyLen >= 16U) {
                physicalCard.assign(body + 8, body + 16);
            }
            m_logSender.warn("platform_balance_update", "parse_or_validate_fail");
        }
        (void)sendBalanceUpdateAck(physicalCard, result, rxSeq);
        if (data) {
            cJSON_Delete(data);
        }
        return;
    }

    if (cmd == kCmdOfflineCardSync) {
        cJSON* data = nullptr;
        uint8_t result = 0x00;
        uint8_t failReason = 0x01;
        if (parseYlcOfflineCardSync044(body, decBodyLen, &data)) {
            const bool ok = publishPlatCommand(0, "offline_card_sync", data);
            result = ok ? 0x01 : 0x00;
            failReason = ok ? 0x00 : 0x02;
            m_logSender.info("platform_offline_card_sync", ok ? "publish_ok" : "publish_fail");
        } else {
            m_logSender.warn("platform_offline_card_sync", "parse_or_validate_fail");
        }
        (void)sendOfflineCardSyncAck(result, failReason, rxSeq);
        if (data) {
            cJSON_Delete(data);
        }
        return;
    }

    if (cmd == kCmdOfflineCardClear) {
        std::vector<std::vector<uint8_t> > physicalCards;
        cJSON* data = nullptr;
        bool ok = false;
        if (parseYlcOfflineCardCards(body, decBodyLen, 24U, "clearCardCount", physicalCards, &data)) {
            cJSON_AddNumberToObject(data, "rawFrameType", 0x46);
            ok = publishPlatCommand(0, "offline_card_clear", data);
            m_logSender.info("platform_offline_card_clear", ok ? "publish_ok" : "publish_fail");
        } else {
            m_logSender.warn("platform_offline_card_clear", "parse_or_validate_fail");
        }
        (void)sendOfflineCardClearAck(physicalCards, ok, rxSeq);
        if (data) {
            cJSON_Delete(data);
        }
        return;
    }

    if (cmd == kCmdOfflineCardQuery) {
        std::vector<std::vector<uint8_t> > physicalCards;
        cJSON* data = nullptr;
        bool ok = false;
        if (parseYlcOfflineCardCards(body, decBodyLen, 26U, "queryCardCount", physicalCards, &data)) {
            cJSON_AddNumberToObject(data, "rawFrameType", 0x48);
            ok = publishPlatCommand(0, "offline_card_query", data);
            m_logSender.info("platform_offline_card_query", ok ? "publish_ok" : "publish_fail");
        } else {
            m_logSender.warn("platform_offline_card_query", "parse_or_validate_fail");
        }
        (void)sendOfflineCardQueryAck(physicalCards, ok, rxSeq);
        if (data) {
            cJSON_Delete(data);
        }
        return;
    }

    if (cmd == kCmdWorkParamSet) {
        uint8_t gun = 0;
        cJSON* data = nullptr;
        uint8_t result = 0x00;
        if (parseYlcWorkParamSet052(body, decBodyLen, gun, &data)) {
            const bool ok = publishPlatCommand(gun, "power_ctrl", data);
            result = ok ? 0x01 : 0x00;
            m_logSender.info("platform_work_param_set", ok ? "publish_ok" : "publish_fail");
        } else {
            m_logSender.warn("platform_work_param_set", "parse_or_validate_fail");
        }
        (void)sendWorkParamSetAck(result, rxSeq);
        if (data) {
            cJSON_Delete(data);
        }
        return;
    }

    if (cmd == kCmdParkingLockCtrl) {
        if (!kYlcEnablePhase2ParkingLock) {
            m_logSender.info("platform_parking_lock_ctrl", "phase2_disabled");
            return;
        }
        uint8_t gun = 0;
        cJSON* data = nullptr;
        uint8_t result = 0x00;
        if (parseYlcParkingLockCtrl062(body, decBodyLen, gun, &data)) {
            const bool ok = publishPlatCommand(gun, "parking_lock_ctrl", data);
            result = ok ? 0x01 : 0x00;
            m_logSender.info("platform_parking_lock_ctrl", ok ? "publish_ok" : "publish_fail");
        } else {
            m_logSender.warn("platform_parking_lock_ctrl", "parse_or_validate_fail");
        }
        (void)sendParkingLockCtrlAck(gun, result, rxSeq);
        if (data) {
            cJSON_Delete(data);
        }
        return;
    }

    if (cmd == kCmdRemoteReboot) {
        cJSON* data = nullptr;
        uint8_t result = 0x00;
        if (parseYlcRemoteReboot092(body, decBodyLen, &data)) {
            const bool ok = publishPlatCommand(0, "remote_reboot", data);
            result = ok ? 0x01 : 0x00;
            m_logSender.info("platform_remote_reboot", ok ? "publish_ok" : "publish_fail");
        } else {
            m_logSender.warn("platform_remote_reboot", "parse_or_validate_fail");
        }
        (void)sendRemoteRebootAck(result, rxSeq);
        if (data) {
            cJSON_Delete(data);
        }
        return;
    }

    if (cmd == kCmdRemoteUpdate) {
        cJSON* data = nullptr;
        uint8_t result = 0x01;
        if (parseYlcRemoteUpdate094(body, decBodyLen, &data)) {
            const bool ok = publishPlatCommand(0, "remote_update", data);
            result = ok ? 0x00 : 0x01;
            m_logSender.info("platform_remote_update", ok ? "publish_ok" : "publish_fail");
        } else {
            m_logSender.warn("platform_remote_update", "parse_or_validate_fail");
        }
        (void)sendRemoteUpdateAck(result, rxSeq);
        if (data) {
            cJSON_Delete(data);
        }
        return;
    }

    if (cmd == kCmdRemoteLogRequest) {
        cJSON* data = nullptr;
        uint8_t result = 0x01;
        if (parseYlcRemoteLogRequest096(body, decBodyLen, &data)) {
            const bool ok = publishPlatCommand(0, "remote_log_request", data);
            result = ok ? 0x00 : 0x01;
            m_logSender.info("platform_remote_log_request", ok ? "publish_ok" : "publish_fail");
        } else {
            m_logSender.warn("platform_remote_log_request", "parse_or_validate_fail");
        }
        (void)sendRemoteLogRequestAck(result, rxSeq);
        if (data) {
            cJSON_Delete(data);
        }
        return;
    }

    if (cmd == kCmdPlugVinReportAck) {
        uint8_t gun = 0;
        if (parseYlcPileGunBody(body, decBodyLen, gun)) {
            stopPlugVinReport(gun);
            m_logSender.info("platform_plug_vin_report_ack_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
        } else {
            m_logSender.warn("platform_plug_vin_report_ack", "parse_or_validate_fail");
        }
        return;
    }

    if (cmd == kCmdPlugVinQuery) {
        uint8_t gun = 0;
        if (parseYlcPileGunBody(body, decBodyLen, gun)) {
            restartPlugVinReport(gun);
            m_logSender.info("platform_plug_vin_query_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
        } else {
            m_logSender.warn("platform_plug_vin_query", "parse_or_validate_fail");
        }
        return;
    }

    if (cmd == kCmdFeeModelCheckAck) {
        uint8_t checkResult = 0x01;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (!parseFeeModelCheckAck006(body, decBodyLen, checkResult)) {
            m_logSender.warn("platform_login_step", "fee_model_check_ack_parse_fail");
            m_loginState = LOGIN_REQ_FEE_MODEL_CHECK;
            m_lastLoginAction = now - std::chrono::seconds(m_config.loginRetrySec);
            return;
        }
        if (checkResult == 0x00) {
            // BY LZW: 按协议5.1上电流程图，0x06确认模型一致后先补传离线存储数据，再进入在线态。
            enterOfflineReplayState(now, LOGIN_ONLINE, "fee_model_check_same_offline_replay");
            m_logSender.info("platform_login_step", "fee_model_check_same");
        } else {
            // BY LZW: 模型不一致时同样先完成离线数据补传，之后再发送0x09请求新计费模型。
            enterOfflineReplayState(now, LOGIN_REQ_FEE_MODEL, "fee_model_check_need_update_offline_replay");
            m_logSender.info("platform_login_step", "fee_model_check_need_update");
        }
        return;
    }

    if (cmd == kCmdFeeModelAck) {
        bool anyCharging = false;
        for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
            if (m_gunRuntimeData[i].gunStatus == 0x03) {
                anyCharging = true;
                break;
            }
        }
        FeeModel feeModel;
        std::string ylcFeeModelNo;
        if (!parseFeeModelAck00A(body, decBodyLen, feeModel, &ylcFeeModelNo)) {
            m_logSender.warn("platform_login_step", "fee_model_ack_parse_fail");
            return;
        }

        // BY LZW: 0x0A 计费模型按整桩生效，统一更新全部枪。
        bool publishOk = true;
        for (size_t i = 0; i < m_feeModelByGun.size(); ++i) {
            m_feeModelByGun[i] = feeModel;
            if (i < m_ylcFeeModelNoByGun.size()) {
                m_ylcFeeModelNoByGun[i] = ylcFeeModelNo.empty() ? "0000" : ylcFeeModelNo;
            }
        }

        // BY LZW: 先把平台模型落库，后续交易记录可通过 update_record.feeModelId 回查历史模型。
        m_logSender.saveFeeModel(feeModel);

        for (size_t i = 0; i < m_feeModelByGun.size(); ++i) {
            publishOk = publishFeeModelSetConfig(static_cast<uint8_t>(i)) && publishOk;
        }
        if (!publishOk) {
            m_logSender.warn("platform_fee_model_ack_setconfig_publish_fail", feeModel.feeModelId);
        }

        // BY LZW: 充电中掉线重登时，新计费模型独立生效，不对当前订单造成冲突；此时暂不等待平台0x56对时，直接恢复在线态。
        if (anyCharging) {
            (void)markPlatformOnlineAfterFeeModel("relogin_fee_model_updated_charging");
            m_logSender.info("platform_login_step", "fee_model_ack_ok_charging_skip_time_sync");
            return;
        }

        // BY LZW: YLC 对时由平台0x56主动下发，0x0A计费模型确认后先进入在线态。
        (void)markPlatformOnlineAfterFeeModel("login_ready");
        m_logSender.info("platform_login_step", "fee_model_ack_ok");
        return;
    }

    if (cmd == kCmdFeeModelSet) {
        uint8_t ackResult = 0x00;
        uint8_t gunNoBcd = 0x00;
        FeeModel feeModel;
        std::string ylcFeeModelNo;
        bool publishOk = false;
        if (parseFeeModelAck00A(body, decBodyLen, feeModel, &ylcFeeModelNo)) {
            std::vector<size_t> affectedGuns;
            for (size_t i = 0; i < m_feeModelByGun.size(); ++i) {
                affectedGuns.push_back(i);
            }

            publishOk = !affectedGuns.empty();
            for (size_t idx = 0; idx < affectedGuns.size(); ++idx) {
                const size_t gunIndex = affectedGuns[idx];
                m_feeModelByGun[gunIndex] = feeModel;
                if (gunIndex < m_ylcFeeModelNoByGun.size()) {
                    m_ylcFeeModelNoByGun[gunIndex] = ylcFeeModelNo.empty() ? "0000" : ylcFeeModelNo;
                }
            }

            // BY LZW: 先持久化平台下发模型，保证后续交易记录可按 feeModelId 回查历史模型。
            m_logSender.saveFeeModel(feeModel);

            for (size_t idx = 0; idx < affectedGuns.size(); ++idx) {
                const size_t gunIndex = affectedGuns[idx];
                publishOk = publishFeeModelSetConfig(static_cast<uint8_t>(gunIndex)) && publishOk;
            }
            if (!publishOk) {
                // BY LZW: saveFeeModel 会同步 DB/共享内存；不回滚运行缓存，避免内部模型来源不一致。
                m_logSender.warn("platform_fee_model_set_setconfig_publish_fail", feeModel.feeModelId);
            }
            ackResult = publishOk ? 0x01 : 0x00;
            m_logSender.info("platform_fee_model_set", feeModel.feeModelId);
        } else {
            m_logSender.warn("platform_fee_model_set", "parse_fail");
        }

        const std::vector<uint8_t> ackBody = buildFeeModelSetAckBody(gunNoBcd, ackResult);
        if (!ackBody.empty()) {
            (void)sendPlatformFrame(kCmdFeeModelSetAck, ackBody, rxSeq);
        }
        return;
    }

    if (cmd == kCmdLoginAck) {
        if (m_loginState == LOGIN_REQ_AUTH) {
            if (!body || decBodyLen != 8U) {
                m_loginState = LOGIN_IDLE;
                m_nextLoginAllowedTime = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                m_logSender.warn("platform_login_step", "login_ack_invalid_len");
                return;
            }

            // BY LZW: body[0..6]=桩编码BCD7，body[7]=登录结果。
            std::string recvPileCode;
            if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
                m_loginState = LOGIN_IDLE;
                m_nextLoginAllowedTime = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                m_logSender.warn("platform_login_step", "login_ack_pile_bcd_invalid");
                return;
            }
            if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
                m_loginState = LOGIN_IDLE;
                m_nextLoginAllowedTime = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                m_logSender.warn("platform_login_step", "login_ack_pile_mismatch");
                return;
            }
            const uint8_t feedbackResult = body[7];
            if (feedbackResult != 0x00) {
                m_loginState = LOGIN_IDLE;
                m_nextLoginAllowedTime = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                m_logSender.warn("platform_login_step", "login_ack_rejected");
                return;
            }
            // BY LZW: 登录成功后先进入0x05计费模型校验；离线存储数据补传按协议5.1流程图放到0x06之后。
            m_sm4SessionKeyReady = false;
            resetOfflineReplayState();
            m_loginState = LOGIN_REQ_FEE_MODEL_CHECK;
            m_lastLoginAction = std::chrono::steady_clock::now() - std::chrono::seconds(m_config.loginRetrySec);
            m_logSender.info("platform_login_step", "login_ack_ok_fee_model_check");
        }
        return;
    }

    if (cmd == kCmdTimeSyncSet) {
        // BY LZW: 0x56 对时设置：桩编号BCD7 + CP56Time2a(7)；桩同步系统时间后回复0x55。
        if (!body || decBodyLen != 14U) {
            m_logSender.warn("platform_time_sync", "time_sync_set_invalid_len");
            return;
        }
        std::string recvPileCode;
        if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
            m_logSender.warn("platform_time_sync", "time_sync_set_pile_bcd_invalid");
            return;
        }
        if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
            m_logSender.warn("platform_time_sync", "time_sync_set_pile_mismatch");
            return;
        }
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
        const std::vector<uint8_t> ackBody = buildTimeSyncRequestBody();
        if (!ackBody.empty()) {
            (void)sendPlatformFrame(kCmdTimeSyncAck, ackBody, rxSeq);
        }
        m_logSender.info("platform_time_sync", "time_sync_set_ok");
        return;
    }

    if (cmd == kCmdHeartbeatAck) {
        if (!body || decBodyLen != 9U) {
            m_logSender.warn("platform_heartbeat", "ack_invalid_len");
            return;
        }
        std::string recvPileCode;
        if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
            m_logSender.warn("platform_heartbeat", "ack_pile_bcd_invalid");
            return;
        }
        if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
            m_logSender.warn("platform_heartbeat", "ack_pile_mismatch");
            return;
        }
        const uint8_t gunNoBcd = body[7];
        if (gunNoBcd != 0x00) {
            const int gunNo = bcdByteToInt(gunNoBcd);
            if (gunNo <= 0 || gunNo > static_cast<int>(m_config.gunCount)) {
                m_logSender.warn("platform_heartbeat", "ack_gun_invalid");
                return;
            }
        }
        if (body[8] != 0x00) {
            m_logSender.warn("platform_heartbeat", "ack_result_invalid");
            return;
        }
        m_lastHeartbeatRecv = std::chrono::steady_clock::now();
        m_heartbeatCounter = 0;
        return;
    }

    if (cmd == kCmdQrCodeSet) {
        uint8_t gun = 0;
        uint8_t gunNoBcd = 0x00;
        cJSON* setConfigData = nullptr;
        uint8_t ackResult = 0x01;
        if (parseQrCodeSet05A(body, decBodyLen, gun, gunNoBcd, &setConfigData)) {
            std::string qrPrefix;
            int qrType = 0;
            cJSON* qrPrefixItem = cJSON_GetObjectItem(setConfigData, "qrPrefix");
            cJSON* qrTypeItem = cJSON_GetObjectItem(setConfigData, "qrType");
            if (qrPrefixItem && cJSON_IsString(qrPrefixItem) && qrPrefixItem->valuestring) {
                qrPrefix = qrPrefixItem->valuestring;
            }
            if (qrTypeItem && cJSON_IsNumber(qrTypeItem)) {
                qrType = qrTypeItem->valueint;
            }
            bool allOk = true;
            for (uint8_t i = 0; i < m_config.gunCount; ++i) {
                char gunNoText[4] = {0};
                // BY LZW: qrType=1时枪号不补零，二维码格式为 平台前缀+桩号+"/"+1位枪号，例如55031412782305/2。
                std::snprintf(gunNoText, sizeof(gunNoText), "%u", static_cast<unsigned int>(i + 1));
                const std::string qrCode = (qrType == 1)
                        ? (qrPrefix + m_config.cdzNo + "/" + gunNoText)
                        : (qrPrefix + m_config.cdzNo);
                cJSON* oneGunData = cJSON_Duplicate(setConfigData, 1);
                if (!oneGunData) {
                    allOk = false;
                    continue;
                }
                cJSON_DeleteItemFromObject(oneGunData, "qrCode");
                cJSON_AddStringToObject(oneGunData, "qrCode", qrCode.c_str());
                const bool iniOk = persistGunQrCodeToIni(i, qrCode);
                const bool mqttOk = publishSetConfig(i, oneGunData);
                cJSON_Delete(oneGunData);
                if (!iniOk) {
                    m_logSender.warn("platform_qr_code_set", "persist_qrcode_fail");
                }
                if (!mqttOk) {
                    m_logSender.warn("platform_qr_code_set", "publish_setconfig_fail");
                }
                allOk = allOk && iniOk && mqttOk;
            }
            if (allOk) {
                ackResult = 0x00;
            }
        } else {
            m_logSender.warn("platform_qr_code_set", "parse_or_validate_fail");
        }
        const std::vector<uint8_t> ackBody = buildQrCodeSetAckBody(ackResult);
        if (!ackBody.empty()) {
            (void)sendPlatformFrame(kCmdQrCodeSetAck, ackBody, rxSeq);
        }
        if (setConfigData) {
            cJSON_Delete(setConfigData);
        }
        return;
    }

    if (cmd == kCmdStartApplyAck) {
        m_logSender.info("platform_vin_start_apply_ack_rx", "phase2_disabled");
        return;
    }

    if (cmd == kCmdMergeChargeApplyAck) {
        if (!kYlcEnablePhase2MergeCharge) {
            m_logSender.info("platform_merge_vin_start_apply_ack_rx", "phase2_disabled");
            return;
        }
        uint8_t gun = 0;
        cJSON* startData = nullptr;
        FeeModel parsedFeeModel;
        if (parseMergeChargeApplyAck0A2(body, decBodyLen, gun, &startData, parsedFeeModel)) {
            m_logSender.info("platform_merge_vin_start_apply_ack_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
            if (!parsedFeeModel.feeModelId.empty() && gun < m_feeModelByGun.size()) {
                m_feeModelByGun[gun] = parsedFeeModel;
            }
            int successFlag = 1;
            (void)jsonGetInt(startData, "successFlag", successFlag);
            // BY LZW: 0xA2 鉴权成功标志：0x01 成功，0x00 失败。
            if (successFlag == 1) {
                cJSON_DeleteItemFromObject(startData, "successFlag");
                cJSON_DeleteItemFromObject(startData, "failReason");
                publishPlatCommand(gun, "start_charge", startData);
            } else {
                publishPlatCommand(gun, "plug_and_charge_auth_result", startData);
            }
        } else {
            m_logSender.warn("platform_merge_vin_start_apply_ack", "parse_or_validate_fail");
        }
        if (startData) {
            cJSON_Delete(startData);
        }
        return;
    }

    if (cmd == kCmdRemoteStartCmd) {
        uint8_t gun = 0;
        cJSON* startData = nullptr;
        FeeModel parsedFeeModel;
        if (parseYlcRemoteStart034(body, decBodyLen, gun, &startData, parsedFeeModel)) {
            m_logSender.info("platform_remote_start_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
            if (!parsedFeeModel.feeModelId.empty() && gun < m_feeModelByGun.size()) {
                m_feeModelByGun[gun] = parsedFeeModel;
            }
            if (!parsedFeeModel.feeModelId.empty()) {
                m_logSender.saveFeeModel(parsedFeeModel);
            }
            if (gun < m_gunRuntimeData.size()) {
                // BY LZW: 0x34 is platform charge start; clear stale discharge session flag.
                m_gunRuntimeData[gun].lastBusinessIsDischarge = false;
            }
            cacheRemoteStartReconnectContext(gun, startData, std::chrono::steady_clock::now());
            const bool knownUnplugged =
                    (gun < m_gunRuntimeData.size()) &&
                    m_gunRuntimeData[gun].yxVehicleConnectStatusValid &&
                    m_gunRuntimeData[gun].yxVehicleConnectStatus == 0;
            if (knownUnplugged) {
                (void)beginRemoteStartReconnectWait(gun, rxSeq);
            } else if (!publishPlatCommand(gun, "start_charge", startData)) {
                m_logSender.warn("platform_remote_start", "publish_start_charge_fail");
                clearRemoteStartReconnectWait(gun);
            }
        } else {
            // BY LZW: 0x34解析失败时可回失败0x33；成功场景等待实际start_complete后只上报一次0x33。
            std::vector<uint8_t> nackBody;
            std::string failTradeNo;
            bool tradeNoOk = false;
            if (body && decBodyLen >= 16U) {
                tradeNoOk = bcdToDigitStringStrict(body, 16, failTradeNo);
            }
            std::string recvPileCode;
            const bool pileCodeOk = (body && decBodyLen >= 23U) ? bcdToDigitStringStrict(body + 16, 7, recvPileCode) : false;
            const bool pileCodeMatched = pileCodeOk && normalizePileCode14(recvPileCode) == normalizePileCode14(m_config.cdzNo);
            int failGunNo = 1;
            const bool gunNoOk = (body && decBodyLen >= 24U) ? bcdByteToInt(body[23], failGunNo) : false;
            const bool gunNoValid = gunNoOk && failGunNo > 0 && failGunNo <= static_cast<int>(m_config.gunCount);
            if (!gunNoValid) {
                failGunNo = 1;
            }
            const uint8_t failGunNoBcd = intToBcdByte(failGunNo);
            const uint8_t failReason = pileCodeOk && !pileCodeMatched ? 0x01 : 0x03;
            appendBcdFixed(nackBody, failTradeNo, 16);
            appendPileCodeBcd7(nackBody, m_config.cdzNo);
            nackBody.push_back(failGunNoBcd);
            nackBody.push_back(0x00);
            nackBody.push_back(failReason);
            (void)sendPlatformFrame(kCmdRemoteStartAck, nackBody, rxSeq);
            std::string logicCardNo;
            const bool logicCardOk = (body && decBodyLen >= 32U) ? bcdToDigitStringStrict(body + 24, 8, logicCardNo) : false;
            // BY ZF: 云快充V1.6 0x34固定44字节。
            const bool remoteStartLenOk = body && decBodyLen == 44U;
            const bool shouldRequeryFeeModel = remoteStartLenOk && tradeNoOk && pileCodeMatched && gunNoValid && logicCardOk;
            if (shouldRequeryFeeModel) {
                (void)sendPlatformFrame(kCmdFeeModelReq, buildFeeModelRequestBody(0x00));
                m_logSender.warn("platform_start_reject", "fee_model_not_ready_requery");
            } else {
                m_logSender.warn("platform_start_reject", "frame_invalid");
            }
        }
        if (startData) {
            cJSON_Delete(startData);
        }
        return;
    }

    if (cmd == kCmdRemoteMergeStart) {
        if (!kYlcEnablePhase2MergeCharge) {
            m_logSender.info("platform_remote_merge_start", "phase2_disabled");
            return;
        }
        uint8_t gun = 0;
        cJSON* startData = nullptr;
        FeeModel parsedFeeModel;
        std::string mergeSeq;
        if (parseRemoteMergeStart0A4(body, decBodyLen, gun, &startData, parsedFeeModel, mergeSeq)) {
            const uint8_t leftGun = static_cast<uint8_t>(gun & static_cast<uint8_t>(~0x01));
            const uint8_t rightGun = static_cast<uint8_t>(leftGun + 1);
            if (!parsedFeeModel.feeModelId.empty()) {
                if (gun < m_feeModelByGun.size()) {
                    m_feeModelByGun[gun] = parsedFeeModel;
                }
                m_logSender.saveFeeModel(parsedFeeModel);
            }

            if (leftGun < m_gunRuntimeData.size()) {
                m_gunRuntimeData[leftGun].pendingVinAuthMergeSeq = mergeSeq;
            }
            if (rightGun < m_gunRuntimeData.size()) {
                m_gunRuntimeData[rightGun].pendingVinAuthMergeSeq = mergeSeq;
            }

            if (startData) {
                cJSON* gunData = cJSON_Duplicate(startData, 1);
                if (gunData) {
                    const uint8_t masterGunFlag = (gun == leftGun) ? 0x00 : 0x01;
                    cJSON_AddNumberToObject(gunData, "masterGunFlag", masterGunFlag);
                    const std::string orderNo = jsonGetString(gunData, "orderNo");
                    const std::string preTradeNo = jsonGetString(gunData, "preTradeNo");
                    const std::string tradeNo = jsonGetString(gunData, "tradeNo");
                    if (m_config.debugTcp) {
                        std::cout << "[Comm][MERGE_A4][PUBLISH] srcGun=" << static_cast<int>(gun)
                                  << " targetGun=" << static_cast<int>(gun)
                                  << " orderNo=" << orderNo
                                  << " preTradeNo=" << preTradeNo
                                  << " tradeNo=" << tradeNo
                                  << " mergeSeq=" << mergeSeq
                                  << " masterGunFlag=" << static_cast<int>(masterGunFlag)
                                  << std::endl;
                    }
                    {
                        std::ostringstream oss;
                        oss << "srcGun=" << static_cast<int>(gun)
                            << ",targetGun=" << static_cast<int>(gun)
                            << ",orderNo=" << orderNo
                            << ",preTradeNo=" << preTradeNo
                            << ",tradeNo=" << tradeNo
                            << ",mergeSeq=" << mergeSeq
                            << ",masterGunFlag=" << static_cast<int>(masterGunFlag);
                        m_logSender.info("platform_remote_merge_start_publish", oss.str());
                    }
                    publishPlatCommand(gun, "start_charge", gunData);
                    cJSON_Delete(gunData);
                }
            }

            m_logSender.info("platform_remote_merge_start_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                             ",leftGun=" + std::to_string(static_cast<int>(leftGun)) +
                             ",rightGun=" + std::to_string(static_cast<int>(rightGun)));
            const std::string ackOrderNo = startData ? jsonGetString(startData, "orderNo") : std::string();
            const std::vector<uint8_t> ackBody = buildRemoteMergeStartAckBody(gun, ackOrderNo, 0x01, 0x00, mergeSeq);
            if (!ackBody.empty()) {
                (void)sendPlatformFrame(kCmdMergeStartReply, ackBody, rxSeq);
            }
        } else {
            const uint8_t gunNoBcd = (decBodyLen >= 24U) ? body[23] : 0x01;
            const int gunNo = bcdByteToInt(gunNoBcd);
            const uint8_t gunIndex = (gunNo > 0) ? static_cast<uint8_t>(gunNo - 1) : 0U;
            std::string orderNo;
            if (decBodyLen >= 16U) {
                (void)bcdToDigitStringStrict(body, 16, orderNo);
            }
            std::string mergeSeq("000000000000");
            if (decBodyLen >= 50U) {
                std::string parsedMergeSeq;
                if (bcdToDigitStringStrict(body + 44, 6, parsedMergeSeq)) {
                    mergeSeq = parsedMergeSeq;
                }
            }
            const std::vector<uint8_t> nackBody = buildRemoteMergeStartAckBody(gunIndex, orderNo, 0x00, 0x01, mergeSeq);
            if (!nackBody.empty()) {
                (void)sendPlatformFrame(kCmdMergeStartReply, nackBody, rxSeq);
            }
            m_logSender.warn("platform_remote_merge_start", "parse_or_validate_fail");
        }
        if (startData) {
            cJSON_Delete(startData);
        }
        return;
    }

    if (cmd == kCmdRemoteStopCmd) {
        uint8_t gun = 0;
        int stopGunNo = 1;
        if (body && decBodyLen >= 8U) {
            (void)bcdByteToInt(body[7], stopGunNo);
            if (stopGunNo <= 0 || stopGunNo > static_cast<int>(m_config.gunCount)) {
                stopGunNo = 1;
            }
        }
        const uint8_t gunNoBcd = intToBcdByte(stopGunNo);
        cJSON* stopData = nullptr;
        if (parseYlcRemoteStop036(body, decBodyLen, gun, &stopData)) {
            m_logSender.info("platform_remote_stop_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
            if (!isGunChargingForRemoteStop(gun)) {
                // BY LZW: YLC 0x35失败原因0x02=枪未处于充电状态；明确非充电时不再向内部发布stop_charge。
                (void)sendRemoteStopAck(gunNoBcd, 0x00, 0x02, rxSeq);
                m_logSender.warn("platform_remote_stop",
                                 std::string("gun_not_charging,gun=") +
                                 std::to_string(static_cast<int>(gun)) +
                                 ",gunStatus=" + std::to_string(static_cast<int>(m_gunRuntimeData[gun].gunStatus)) +
                                 ",isDischarge=" + (m_gunRuntimeData[gun].lastBusinessIsDischarge ? "1" : "0"));
            } else if (publishPlatCommand(gun, "stop_charge", stopData)) {
                // BY LZW: 0x35 停机应答成功：停止结果0x01，失败原因0x00。
                (void)sendRemoteStopAck(gunNoBcd, 0x01, 0x00, rxSeq);
            } else {
                (void)sendRemoteStopAck(gunNoBcd, 0x00, 0x03, rxSeq);
                m_logSender.warn("platform_remote_stop", "publish_stop_charge_fail");
            }
        } else {
            // BY LZW: 0x36远程停机解析失败时，立即回复0x35失败应答，失败原因固定01。
            (void)sendRemoteStopAck(gunNoBcd, 0x00, 0x01, rxSeq);
            m_logSender.warn("platform_remote_stop", "parse_or_validate_fail");
        }
        if (stopData) {
            cJSON_Delete(stopData);
        }
        return;
    }

    if (cmd == kCmdRecordConfirm) {
        uint8_t gun = 0;
        cJSON* cfmData = nullptr;
        if (parseYlcRecordConfirm040(cmd, body, decBodyLen, gun, &cfmData)) {
            m_logSender.info("platform_record_confirm_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
            publishPlatCommand(gun, "record_cfm", cfmData);
            int confirmFlag = 0;
            (void)jsonGetInt(cfmData, "confirmFlag", confirmFlag);
            if (confirmFlag != 0) {
                markOfflineReplayTradeConfirmed(jsonGetString(cfmData, "tradeNo"));
            }
        }
        if (cfmData) {
            cJSON_Delete(cfmData);
        }
        return;
    }

    if (cmd == kCmdReadChargeInfo) {
        uint8_t gun = 0;
        if (parseYlcReadRealtimeRequest(body, decBodyLen, gun)) {
            const std::vector<uint8_t> infoBody = buildChargeInfoBody(gun);
            if (!infoBody.empty()) {
                (void)sendPlatformFrame(kCmdChargeInfo, infoBody, rxSeq);
            } else {
                m_logSender.warn("platform_read_charge_info", "build_body_empty");
            }
        } else {
            m_logSender.warn("platform_read_charge_info", "parse_or_validate_fail");
        }
        return;
    }

    if (cmd == kCmdDischargeStartCmd) {
        uint8_t gun = 0;
        std::string tradeNo;
        cJSON* startData = nullptr;
        if (parseYlcDischargeStart0E2(body, decBodyLen, gun, tradeNo, &startData)) {
            const uint32_t ackResult = precheckYlcDischargeStartAckResult(gun, startData);
            const uint8_t gunNoBcd = intToBcdByte(static_cast<int>(gun) + 1);
            if (ackResult == 0U) {
                if (publishPlatCommand(gun, "start_charge", startData)) {
                    if (gun < m_gunRuntimeData.size()) {
                        // BY LZW: 0xE2 starts a platform discharge session; cache it for later MQTT events without v2g.
                        m_gunRuntimeData[gun].lastBusinessIsDischarge = true;
                    }
                    (void)sendDischargeStartAck(tradeNo, gunNoBcd, 0x00000000U, rxSeq);
                    m_logSender.info("platform_discharge_start_rx",
                                     std::string("gun=") + std::to_string(static_cast<int>(gun)));
                } else {
                    (void)sendDischargeStartAck(tradeNo, gunNoBcd, 0x000000FFU, rxSeq);
                    m_logSender.warn("platform_discharge_start", "publish_start_charge_fail");
                }
            } else {
                (void)sendDischargeStartAck(tradeNo, gunNoBcd, ackResult, rxSeq);
                m_logSender.warn("platform_discharge_start_precheck",
                                 std::string("gun=") + std::to_string(static_cast<int>(gun)) +
                                 ",ackResult=" + std::to_string(static_cast<unsigned int>(ackResult)));
            }
        } else {
            const uint8_t gunNoBcd = (decBodyLen >= 24U) ? body[23] : 0x01;
            std::string failTradeNo;
            if (decBodyLen >= 16U) {
                (void)bcdToDigitStringStrict(body, 16, failTradeNo);
            }
            (void)sendDischargeStartAck(failTradeNo, gunNoBcd, 0x00000001U, rxSeq);
            m_logSender.warn("platform_discharge_start", "parse_or_validate_fail");
        }
        if (startData) {
            cJSON_Delete(startData);
        }
        return;
    }

    if (cmd == kCmdDischargeStopCmd) {
        uint8_t gun = 0;
        const uint8_t gunNoBcd = (decBodyLen >= 8U) ? body[7] : 0x01;
        cJSON* stopData = nullptr;
        if (parseYlcDischargeStop0E4(body, decBodyLen, gun, &stopData)) {
            if (gun < m_gunRuntimeData.size()) {
                // BY LZW: 0xE4 is platform discharge stop; keep discharge flag until stop_complete/update_record finish.
                m_gunRuntimeData[gun].lastBusinessIsDischarge = true;
            }
            publishPlatCommand(gun, "stop_charge", stopData);
            (void)sendDischargeStopAck(gunNoBcd, 0x00, rxSeq);
            m_logSender.info("platform_discharge_stop_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
        } else {
            (void)sendDischargeStopAck(gunNoBcd, 0x01, rxSeq);
            m_logSender.warn("platform_discharge_stop", "parse_or_validate_fail");
        }
        if (stopData) {
            cJSON_Delete(stopData);
        }
        return;
    }

    if (cmd == kCmdDischargeRecordConfirm) {
        uint8_t gun = 0;
        cJSON* cfmData = nullptr;
        if (parseYlcRecordConfirm040(cmd, body, decBodyLen, gun, &cfmData)) {
            publishPlatCommand(gun, "record_cfm", cfmData);
            int confirmFlag = 0;
            (void)jsonGetInt(cfmData, "confirmFlag", confirmFlag);
            if (confirmFlag != 0) {
                markOfflineReplayTradeConfirmed(jsonGetString(cfmData, "tradeNo"));
            }
            m_logSender.info("platform_discharge_record_confirm_rx",
                             std::string("gun=") + std::to_string(static_cast<int>(gun)));
        } else {
            m_logSender.warn("platform_discharge_record_confirm", "parse_or_validate_fail");
        }
        if (cfmData) {
            cJSON_Delete(cfmData);
        }
        return;
    }

    if (cmd == kCmdReadDischargeInfo) {
        uint8_t gun = 0;
        if (parseYlcReadRealtimeRequest(body, decBodyLen, gun)) {
            const std::vector<uint8_t> infoBody = buildDischargeInfoBody(gun);
            if (!infoBody.empty()) {
                (void)sendPlatformFrame(kCmdDischargeInfo, infoBody, rxSeq);
            } else {
                m_logSender.warn("platform_read_discharge_info", "build_body_empty");
            }
        } else {
            m_logSender.warn("platform_read_discharge_info", "parse_or_validate_fail");
        }
        return;
    }

    // BY LZW: 未识别平台帧仅保留本地调试日志，不发布MQTT。
    if (m_config.debugTcp) {
        std::cout << "[Comm][TCP][RX_FRAME] ignored cmd=0x"
                  << std::hex << static_cast<int>(cmd) << std::dec << std::endl;
    }
}

bool CommProcess::parseFeeModelCheckAck006(const uint8_t* body, size_t bodyLen, uint8_t& result) const
{
    if (!body || bodyLen != 10U) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }
    std::string modelNo;
    if (!bcdToDigitStringStrict(body + 7, 2, modelNo)) {
        return false;
    }
    result = body[9];
    return true;
}

bool CommProcess::parseFeeModelAck00A(const uint8_t* body, size_t bodyLen, FeeModel& feeModel, std::string* ylcFeeModelNo)
{
    static const size_t kYlcFeeModelBodyLen = 90U;
    static const size_t kYlcFeeModelRateCount = 4U;
    if (!body || bodyLen != kYlcFeeModelBodyLen) {
        return false;
    }

    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }

    // BY LZW: YLC PDF 6.8/9.5：桩编号BCD7 + 模型号BCD2 + 尖峰平谷4组费率 + 计损比例 + 48段费率号。
    const size_t modelPos = 7U;
    std::string modelNo;
    if (!bcdToDigitStringStrict(body + modelPos, 2, modelNo)) {
        return false;
    }
    if (ylcFeeModelNo) {
        *ylcFeeModelNo = modelNo.empty() ? "0000" : modelNo;
    }

    std::vector<uint32_t> chargeRateRaw;
    std::vector<uint32_t> serviceRateRaw;
    chargeRateRaw.reserve(kYlcFeeModelRateCount);
    serviceRateRaw.reserve(kYlcFeeModelRateCount);

    size_t pos = modelPos + 2U;
    for (size_t i = 0; i < kYlcFeeModelRateCount; ++i) {
        // BY LZW: 每组按小端：电费BIN4 + 服务费BIN4；精度10^-5元。
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

    // BY LZW: 计损比例保留，当前不处理。
    pos += 1;

    std::vector<uint8_t> slotMap(48, 0);
    for (size_t i = 0; i < 48; ++i) {
        const uint8_t feeIdx = body[pos + i];
        slotMap[i] = (feeIdx <= 0x03U) ? feeIdx : 0x00U;
    }

    // BY LZW: 平台 modelNo 固定但内容可能变化，接收时按“yymmddhhmmss_modelNo”生成本地唯一计费模型编号。
    const std::time_t nowSec = std::time(nullptr);
    std::tm* tmv = std::localtime(&nowSec);
    char tsBuf[32] = {0};
    std::snprintf(tsBuf, sizeof(tsBuf), "%02d%02d%02d%02d%02d%02d",
                  tmv ? ((tmv->tm_year + 1900) % 100) : 0,
                  tmv ? (tmv->tm_mon + 1) : 1,
                  tmv ? tmv->tm_mday : 1,
                  tmv ? tmv->tm_hour : 0,
                  tmv ? tmv->tm_min : 0,
                  tmv ? tmv->tm_sec : 0);
    feeModel.feeModelId = std::string(tsBuf) + "_" + (modelNo.empty() ? "0000" : modelNo);
    feeModel.timeSeg.clear();
    feeModel.segFlag.clear();
    feeModel.chargeFee.clear();
    feeModel.serviceFee.clear();

    // BY LZW: 按 48 个半小时槽位完整展开保存，供后续计费与交易记录逐槽位一一对应。
    // BY LZW: 费率号骨架沿用 zero-based 编码，后续按 YLC 48 个半小时费率号确认。
    for (int slot = 0; slot < 48; ++slot) {
        int feeIdx = static_cast<int>(slotMap[static_cast<size_t>(slot)]); // 0x00 => 第1组
        if (feeIdx < 0 || feeIdx >= static_cast<int>(kYlcFeeModelRateCount)) {
            feeIdx = 0; // BY LZW: 异常值兜底到第1组
        }

        const int minute = slot * 30;
        const int hh = minute / 60;
        const int mm = minute % 60;
        char ts[8] = {0};
        std::snprintf(ts, sizeof(ts), "%02d%02d", hh, mm);
        feeModel.timeSeg.push_back(ts);
        feeModel.segFlag.push_back(static_cast<unsigned int>(feeIdx + 1));

        // BY LZW: 内部统一按 10^-5 元保存单价，直接保留平台原始精度。
        feeModel.chargeFee.push_back(chargeRateRaw[static_cast<size_t>(feeIdx)]);
        feeModel.serviceFee.push_back(serviceRateRaw[static_cast<size_t>(feeIdx)]);
    }

    if (feeModel.timeSeg.size() != 48U ||
        feeModel.segFlag.size() != 48U ||
        feeModel.chargeFee.size() != 48U ||
        feeModel.serviceFee.size() != 48U) {
        return false;
    }
    feeModel.timeNum = 48;
    return true;
}

bool CommProcess::parseYlcRemoteStart034(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData, FeeModel& feeModel)
{
    if (!body || !outData) {
        return false;
    }
    // BY ZF: 云快充V1.6 0x34固定44字节，无易联桩新版继电器断开字段。
    // BY ZF: 字段：交易流水号16 + 桩编号7 + 枪号1 + 逻辑卡号8 + 物理卡号8 + 账户余额4。
    if (bodyLen != 44U) {
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

    const int gunNo = bcdByteToInt(gunBcd);
    if (gunNo <= 0) {
        return false;
    }
    const int gunIndex = gunNo - 1;
    if (gunIndex < 0 || gunIndex >= static_cast<int>(m_gunRuntimeData.size())) {
        return false;
    }
    gun = static_cast<uint8_t>(gunIndex);

    std::string orderNo;
    std::string chargeUserNo;
    std::string pileNo;
    if (!bcdToDigitStringStrict(tradeBcd, 16, orderNo) ||
        !bcdToDigitStringStrict(logicCardBcd, 8, chargeUserNo) ||
        !bcdToDigitStringStrict(pileBcd, 7, pileNo)) {
        return false;
    }
    if (pileNo != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }

    // BY LZW: 0x34不携带时段费率，使用该枪已同步缓存的计费模型。
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
    // BY LZW: 无内存计费模型时，从本地DB懒加载最新模型兜底，避免0x34早于0x58/0x0A导致误拒。
    if (!isYlcFeeModelReady(feeModel)) {
        FeeModel latestFeeModel;
        if (loadLatestFeeModelFromDbFile(latestFeeModel)) {
            feeModel = latestFeeModel;
            if (gun < m_feeModelByGun.size()) {
                m_feeModelByGun[gun] = latestFeeModel;
            }
            if (gun < m_ylcFeeModelNoByGun.size()) {
                m_ylcFeeModelNoByGun[gun] = ylcFeeModelNoFromLocalId(latestFeeModel.feeModelId);
            }
            std::ostringstream oss;
            oss << "gun=" << static_cast<int>(gun)
                << ",feeModelId=" << latestFeeModel.feeModelId
                << ",timeNum=" << static_cast<int>(latestFeeModel.timeNum);
            m_logSender.info("remote_start_fee_model_db_fallback_ok", oss.str());
        } else {
            std::ostringstream oss;
            oss << "gun=" << static_cast<int>(gun)
                << ",orderNo=" << orderNo;
            m_logSender.warn("remote_start_fee_model_db_fallback_miss", oss.str());
        }
    }
    // BY LZW: 无可用计费模型时拒绝启动（由上层回复0x33失败并重发0x09）。
    if (!isYlcFeeModelReady(feeModel)) {
        // BY LZW: 仅缓存订单号，供0x33失败应答使用。
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
    cJSON_AddNumberToObject(data, "feeModelNo", 0);
    cJSON_AddStringToObject(data, "feeModelId", feeModel.feeModelId.c_str());
    cJSON_AddNumberToObject(data, "billingFlag", 0);
    cJSON_AddNumberToObject(data, "userStatus", 0);
    // BY LZW: 补充启动充电控制参数，供 tcu_logic/pile_controller 透传使用。
    cJSON_AddNumberToObject(data, "loadControlSwitch", 0x02);
    cJSON_AddNumberToObject(data, "plugAndChargeFlag", 0x01);
    cJSON_AddNumberToObject(data, "auxPowerVoltage", 0x0C);
    cJSON_AddNumberToObject(data, "mergeChargeFlag", 0x00);


    // BY LZW: 费率相关字段（沿用已缓存计费模型）。
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

    // BY LZW: 保存启动命令解析结果到每枪运行态缓存，供后续平台上送复用。
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

bool CommProcess::parseRemoteMergeStart0A4(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData, FeeModel& feeModel, std::string& mergeSeq)
{
    if (!body || !outData) {
        return false;
    }
    // BY LZW: 0xA4 最小长度：交易流水号16 + 桩编号7 + 枪号1 + 逻辑卡号8 + 物理卡号8 + 账户余额4 + 并充序号6
    if (bodyLen < 50U) {
        return false;
    }

    const uint8_t* tradeBcd = body;
    const uint8_t* pileBcd = body + 16;
    const uint8_t gunBcd = body[23];
    const uint8_t* logicCardBcd = body + 24;
    const uint8_t* phyCardBin = body + 32;
    const uint32_t balanceRaw = static_cast<uint32_t>(body[40]) |
                                (static_cast<uint32_t>(body[41]) << 8) |
                                (static_cast<uint32_t>(body[42]) << 16) |
                                (static_cast<uint32_t>(body[43]) << 24);
    const uint8_t* mergeSeqBcd = body + 44;

    std::string recvPileCode;
    if (!bcdToDigitStringStrict(pileBcd, 7, recvPileCode)) {
        return false;
    }
    const std::string localPileCode = normalizePileCode14(m_config.cdzNo);
    if (recvPileCode != localPileCode) {
        return false;
    }

    const int gunNo = bcdByteToInt(gunBcd);
    if (gunNo <= 0) {
        return false;
    }
    const int gunIndex = gunNo - 1;
    if (gunIndex < 0 || gunIndex >= static_cast<int>(m_gunRuntimeData.size())) {
        return false;
    }
    gun = static_cast<uint8_t>(gunIndex);

    if (!bcdToDigitStringStrict(mergeSeqBcd, 6, mergeSeq)) {
        return false;
    }

    std::string orderNo;
    std::string chargeUserNo;
    if (!bcdToDigitStringStrict(tradeBcd, 16, orderNo) ||
        !bcdToDigitStringStrict(logicCardBcd, 8, chargeUserNo)) {
        return false;
    }
    (void)phyCardBin;

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
    const bool feeModelReady =
            (!feeModel.feeModelId.empty()) &&
            (feeModel.timeNum > 0) &&
            (feeModel.timeSeg.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.chargeFee.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.serviceFee.size() >= static_cast<size_t>(feeModel.timeNum));
    if (!feeModelReady) {
        if (gun < m_gunRuntimeData.size()) {
            m_gunRuntimeData[gun].orderNo = orderNo;
            m_gunRuntimeData[gun].pendingVinAuthMergeSeq = mergeSeq;
        }
        return false;
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "startTime", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddStringToObject(data, "chargeUserNo", chargeUserNo.c_str());
    cJSON_AddStringToObject(data, "orderNo", orderNo.c_str());
    cJSON_AddStringToObject(data, "logicCardNo", chargeUserNo.c_str());
    cJSON_AddStringToObject(data, "preTradeNo", orderNo.c_str());
    cJSON_AddStringToObject(data, "tradeNo", orderNo.c_str());
    cJSON_AddNumberToObject(data, "chargeMode", 0x60);
    cJSON_AddNumberToObject(data, "prechargeAmount", static_cast<double>(balanceRaw) / 100.0);
    cJSON_AddNumberToObject(data, "feeModelNo", 0);
    cJSON_AddStringToObject(data, "feeModelId", feeModel.feeModelId.c_str());
    cJSON_AddNumberToObject(data, "billingFlag", 0);
    cJSON_AddNumberToObject(data, "userStatus", 0);
    cJSON_AddNumberToObject(data, "loadControlSwitch", 0x02);
    cJSON_AddNumberToObject(data, "plugAndChargeFlag", 0x01);
    cJSON_AddNumberToObject(data, "auxPowerVoltage", 0x0C);
    cJSON_AddNumberToObject(data, "mergeChargeFlag", 0x01);
    cJSON_AddStringToObject(data, "mergeChargeSeq", mergeSeq.c_str());
    cJSON_AddStringToObject(data, "mergeSeq", mergeSeq.c_str());

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

    if (gun < m_gunRuntimeData.size()) {
        GunRuntimeData& rd = m_gunRuntimeData[gun];
        rd.chargeUserNo = chargeUserNo;
        rd.orderNo = orderNo;
        rd.chargeMode = 0x60;
        rd.prechargeAmount = static_cast<double>(balanceRaw) / 100.0;
        rd.userStatus = 0;
        rd.billingFlag = 0;
        rd.feeModelId = feeModel.feeModelId;
        rd.feeTimeNum = static_cast<int>(feeModel.timeNum);
        rd.pendingVinAuthMergeChargeFlag = 0x01;
        rd.pendingVinAuthMergeSeq = mergeSeq;
        rd.feeSegments.clear();
        const size_t periodCount = feeModel.timeSeg.size();
        rd.feeSegments.reserve(periodCount);
        for (size_t i = 0; i < periodCount; ++i) {
            FeeSegmentData seg;
            seg.startTs = feeModel.timeSeg[i];
            seg.endTs = (i + 1 < periodCount) ? feeModel.timeSeg[i + 1] : "2400";
            rd.feeSegments.push_back(seg);
        }
    }

    *outData = data;
    return true;
}

bool CommProcess::parseMergeChargeApplyAck0A2(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData, FeeModel& feeModel)
{
    if (!body || !outData) {
        return false;
    }
    // BY LZW: 0xA2 平台并充启动应答：
    // 交易流水号16 + 桩编号7 + 枪号1 + 逻辑卡号8 + 账户余额4 + 鉴权成功标志1 + 失败原因1 + 并充序号6。
    if (bodyLen < 44U) {
        return false;
    }

    const uint8_t* tradeBcd = body;
    const uint8_t* pileBcd = body + 16;
    const uint8_t gunBcd = body[23];
    const uint8_t* logicCardBcd = body + 24;
    const uint32_t balanceRaw = static_cast<uint32_t>(body[32]) |
                                (static_cast<uint32_t>(body[33]) << 8) |
                                (static_cast<uint32_t>(body[34]) << 16) |
                                (static_cast<uint32_t>(body[35]) << 24);
    const uint8_t successFlag = body[36];
    const uint8_t failReason = body[37];
    const uint8_t* mergeSeqBcd = body + 38;

    std::string recvPileCode;
    if (!bcdToDigitStringStrict(pileBcd, 7, recvPileCode)) {
        return false;
    }
    const std::string localPileCode = normalizePileCode14(m_config.cdzNo);
    if (recvPileCode != localPileCode) {
        return false;
    }

    const int gunNo = bcdByteToInt(gunBcd);
    if (gunNo <= 0) {
        return false;
    }
    const int gunIndex = gunNo - 1;
    if (gunIndex < 0 || gunIndex >= static_cast<int>(m_gunRuntimeData.size())) {
        return false;
    }
    gun = static_cast<uint8_t>(gunIndex);

    std::string orderNo;
    std::string chargeUserNo;
    std::string mergeSeq;
    if (!bcdToDigitStringStrict(tradeBcd, 16, orderNo) ||
        !bcdToDigitStringStrict(logicCardBcd, 8, chargeUserNo) ||
        !bcdToDigitStringStrict(mergeSeqBcd, 6, mergeSeq)) {
        return false;
    }
    const GunRuntimeData& rd = m_gunRuntimeData[gun];

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

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "startTime", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddStringToObject(data, "vin", rd.pendingVinAuthVin.c_str());
    cJSON_AddStringToObject(data, "vinCode", rd.pendingVinAuthVin.c_str());
    cJSON_AddStringToObject(data, "chargeUserNo", chargeUserNo.c_str());
    cJSON_AddStringToObject(data, "logicCardNo", chargeUserNo.c_str());
    cJSON_AddStringToObject(data, "orderNo", orderNo.c_str());
    cJSON_AddStringToObject(data, "preTradeNo", orderNo.c_str());
    cJSON_AddStringToObject(data, "tradeNo", orderNo.c_str());
    cJSON_AddNumberToObject(data, "chargeMode", 0x60);
    cJSON_AddNumberToObject(data, "prechargeAmount", static_cast<double>(balanceRaw) / 100.0);
    cJSON_AddNumberToObject(data, "plugAndChargeFlag", rd.pendingVinAuthPlugAndChargeFlag);
    cJSON_AddNumberToObject(data, "mergeChargeFlag", 0x01);
    cJSON_AddStringToObject(data, "mergeChargeSeq", mergeSeq.c_str());
    cJSON_AddStringToObject(data, "mergeSeq", mergeSeq.c_str());
    cJSON_AddNumberToObject(data, "masterGunFlag", rd.pendingVinAuthMasterGunFlag);
    cJSON_AddNumberToObject(data, "successFlag", successFlag);
    cJSON_AddNumberToObject(data, "failReason", failReason);
    cJSON_AddNumberToObject(data, "feeModelNo", 0);
    cJSON_AddStringToObject(data, "feeModelId", feeModel.feeModelId.c_str());

    const bool feeModelReady =
            (!feeModel.feeModelId.empty()) &&
            (feeModel.timeNum > 0) &&
            (feeModel.timeSeg.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.chargeFee.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.serviceFee.size() >= static_cast<size_t>(feeModel.timeNum));
    if (feeModelReady) {
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
    }

    GunRuntimeData& wr = m_gunRuntimeData[gun];
    wr.chargeUserNo = chargeUserNo;
    wr.orderNo = orderNo;
    wr.chargeMode = 0x60;
    wr.prechargeAmount = static_cast<double>(balanceRaw) / 100.0;
    wr.feeModelId = feeModel.feeModelId;
    wr.feeTimeNum = static_cast<int>(feeModel.timeNum);
    wr.pendingVinAuthMergeChargeFlag = 0x01;
    wr.pendingVinAuthMergeSeq = mergeSeq;
    wr.feeSegments.clear();
    const size_t periodCount = feeModel.timeSeg.size();
    wr.feeSegments.reserve(periodCount);
    for (size_t i = 0; i < periodCount; ++i) {
        FeeSegmentData seg;
        seg.startTs = feeModel.timeSeg[i];
        seg.endTs = (i + 1 < periodCount) ? feeModel.timeSeg[i + 1] : "2400";
        wr.feeSegments.push_back(seg);
    }

    *outData = data;
    return true;
}

bool CommProcess::parseYlcRemoteStop036(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData)
{
    if (!body || !outData) {
        return false;
    }
    // BY LZW: 0x36 固定长度：桩编号BCD7 + 枪号BCD1。
    if (bodyLen != 8U) {
        return false;
    }

    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    const std::string localPileCode = normalizePileCode14(m_config.cdzNo);
    if (recvPileCode != localPileCode) {
        return false;
    }

    const uint8_t gunNoBcd = body[7];
    const int gunNo = bcdByteToInt(gunNoBcd);
    if (gunNo <= 0) {
        return false;
    }
    const int gunIndex = gunNo - 1;
    if (gunIndex >= static_cast<int>(m_config.gunCount)) {
        return false;
    }
    gun = static_cast<uint8_t>(gunIndex);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "stopReason", 0x01);  // BY LZW: 按要求固定停机原因 0x01
    cJSON_AddNumberToObject(data, "tcuStopCode", 0);
    *outData = data;
    return true;
}

bool CommProcess::parseQrCodeSet05A(const uint8_t* body, size_t bodyLen, uint8_t& gun, uint8_t& gunNoBcd, cJSON** outData) const
{
    if (!body || !outData) {
        return false;
    }
    // BY LZW: 0xF0 二维码前缀下发：桩编号BCD7 + 二维码格式BCD1 + 前缀长度1 + 前缀ASCII。
    if (bodyLen < 9U || bodyLen > kYlcMaxBodyLen) {
        return false;
    }

    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    const std::string localPileCode = normalizePileCode14(m_config.cdzNo);
    if (recvPileCode != localPileCode) {
        return false;
    }

    gun = 0;
    gunNoBcd = 0x00;

    const int qrType = bcdByteToInt(body[7]);
    if (qrType != 0 && qrType != 1) {
        return false;
    }

    const size_t remain = bodyLen - 9U;
    // BY LZW: 0xF0前缀长度字段固定按BIN解析；平台可能在前缀后补0，不要求长度等于剩余报文长度。
    const size_t qrLen = static_cast<size_t>(body[8]);
    if (qrLen > remain ||
        qrLen > kYlcQrPrefixProtocolMaxLen ||
        qrLen > kYlcQrPrefixFrameMaxLen) {
        return false;
    }

    std::string qrPrefix;
    if (qrLen > 0U) {
        qrPrefix.assign(reinterpret_cast<const char*>(body + 9U), qrLen);
    }

    cJSON* data = cJSON_CreateObject();
    if (!data) {
        return false;
    }
    cJSON_AddStringToObject(data, "qrPrefix", qrPrefix.c_str());
    cJSON_AddNumberToObject(data, "qrType", qrType);
    cJSON_AddNumberToObject(data, "qrLen", static_cast<double>(qrLen));
    cJSON_AddNumberToObject(data, "qrPrefixProtocolMaxLen", static_cast<double>(kYlcQrPrefixProtocolMaxLen));
    cJSON_AddNumberToObject(data, "qrPrefixFrameMaxLen", static_cast<double>(kYlcQrPrefixFrameMaxLen));
    *outData = data;
    return true;
}

bool CommProcess::parseYlcRecordConfirm040(uint8_t confirmCmd, const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData)
{
    if (!body || !outData) {
        return false;
    }
    // BY LZW: 充电记录确认固定长度：交易流水号BCD16 + 传送原因1。
    if (bodyLen != 17U) {
        return false;
    }

    std::string protocolTradeNo;
    if (!bcdToDigitStringStrict(body, 16, protocolTradeNo)) {
        return false;
    }
    const uint8_t feedbackResult = body[16];
    // BY LZW: YLC 0x40/0xE8 按协议口径仅 0x00 表示确认成功，其他值保留为平台未确认/异常。
    const bool confirmed = (feedbackResult == 0x00);
    const uint8_t uploadCmd = (confirmCmd == kCmdDischargeRecordConfirm)
        ? kCmdDischargeTradeRecord
        : kCmdUploadTradeRecord;
    const std::string protocolKey = tradeRecordUploadKey(uploadCmd, protocolTradeNo);
    std::string originalTradeNo = protocolTradeNo;
    std::map<std::string, std::string>::const_iterator mapIt = m_tradeRecordOriginalByProtocolKey.find(protocolKey);
    if (mapIt != m_tradeRecordOriginalByProtocolKey.end()) {
        originalTradeNo = mapIt->second;
        m_logSender.info("ykc_trade_record_confirm_map_hit",
                         std::string("cmd=0x") + (uploadCmd == kCmdUploadTradeRecord ? "3B" : "E9") +
                         ",protocolTradeNo=" + protocolTradeNo +
                         ",originalTradeNo=" + originalTradeNo);
    } else {
        m_logSender.warn("ykc_trade_record_confirm_map_miss",
                         std::string("cmd=0x") + (uploadCmd == kCmdUploadTradeRecord ? "3B" : "E9") +
                         ",protocolTradeNo=" + protocolTradeNo);
    }

    // BY ZF: 协议无枪号，按原始/协议 tradeNo 在待确认缓存中反查所属枪。
    int gunIndex = 0;
    bool foundGun = false;
    for (size_t i = 0; i < m_gunRuntimeData.size(); ++i) {
        if (m_gunRuntimeData[i].pendingRecordTradeNo == originalTradeNo ||
            m_gunRuntimeData[i].pendingRecordTradeNo == protocolTradeNo) {
            gunIndex = static_cast<int>(i);
            foundGun = true;
            break;
        }
    }
    // BY LZW: 若未命中缓存，兜底归到0号枪，避免丢失确认消息。
    (void)foundGun;
    gun = static_cast<uint8_t>(gunIndex);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "tradeNo", originalTradeNo.c_str());
    cJSON_AddStringToObject(data, "protocolTradeNo", protocolTradeNo.c_str());
    cJSON_AddNumberToObject(data, "confirmFlag", confirmed ? 1 : 0);
    cJSON_AddNumberToObject(data, "result", feedbackResult);
    *outData = data;

    if (confirmed && !originalTradeNo.empty()) {
        m_logSender.confirmTradeRecord(originalTradeNo, 1);
    }

    // BY LZW: result=0x00才删除本地账单；非法账单只停止本轮重发并保留本地记录便于排查。
    clearPendingTradeRecordUpload(originalTradeNo, confirmed);
    return true;
}

bool CommProcess::parseYlcDischargeStart0E2(const uint8_t* body, size_t bodyLen, uint8_t& gun, std::string& tradeNo, cJSON** outData)
{
    if (!body || !outData) {
        return false;
    }
    // BY LZW: 0xE2 固定65字节：交易流水号BCD16 + 桩编号BCD7 + 枪号BCD1 + 策略1 + 参数4 + 停止密码4 + 用户卡号32。
    if (bodyLen != 65U) {
        return false;
    }
    if (!bcdToDigitStringStrict(body, 16, tradeNo)) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body + 16, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }
    const int gunNo = bcdByteToInt(body[23]);
    if (gunNo <= 0 || gunNo > static_cast<int>(m_config.gunCount)) {
        return false;
    }
    gun = static_cast<uint8_t>(gunNo - 1);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "startTime", static_cast<double>(std::time(nullptr)) * 1000.0);
    cJSON_AddStringToObject(data, "orderNo", tradeNo.c_str());
    cJSON_AddStringToObject(data, "preTradeNo", tradeNo.c_str());
    cJSON_AddStringToObject(data, "tradeNo", tradeNo.c_str());
    const uint8_t dischargeStrategy = body[24];
    const uint32_t strategyParam = static_cast<uint32_t>(body[25]) |
                                  (static_cast<uint32_t>(body[26]) << 8) |
                                  (static_cast<uint32_t>(body[27]) << 16) |
                                  (static_cast<uint32_t>(body[28]) << 24);
    const uint32_t stopPassword = static_cast<uint32_t>(body[29]) |
                                  (static_cast<uint32_t>(body[30]) << 8) |
                                  (static_cast<uint32_t>(body[31]) << 16) |
                                  (static_cast<uint32_t>(body[32]) << 24);
    const std::string userCardNo = asciiFixedToString(body + 33, 32);
    const std::string userNo = userCardNo.empty() ? tradeNo : userCardNo;
    cJSON_AddStringToObject(data, "chargeUserNo", userNo.c_str());
    cJSON_AddStringToObject(data, "cardNumber", userNo.c_str());
    cJSON_AddNumberToObject(data, "chargeMode", 0x60);
    cJSON_AddNumberToObject(data, "v2g", 1);
    cJSON_AddBoolToObject(data, "isDischarge", 1);
    cJSON_AddNumberToObject(data, "dischargeStrategy", dischargeStrategy);
    cJSON_AddNumberToObject(data, "dischargeStrategyParam", strategyParam);
    cJSON_AddNumberToObject(data, "dischargeStopPassword", stopPassword);
    cJSON_AddStringToObject(data, "dischargeUserCardNo", userCardNo.c_str());

    FeeModel feeModel;
    if (gun < m_feeModelByGun.size()) {
        feeModel = m_feeModelByGun[gun];
    }
    cJSON_AddNumberToObject(data, "feeModelNo", 0);
    cJSON_AddStringToObject(data, "feeModelId", feeModel.feeModelId.c_str());
    const bool feeModelReady =
            (!feeModel.feeModelId.empty()) &&
            (feeModel.timeNum > 0) &&
            (feeModel.timeSeg.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.chargeFee.size() >= static_cast<size_t>(feeModel.timeNum)) &&
            (feeModel.serviceFee.size() >= static_cast<size_t>(feeModel.timeNum));
    if (feeModelReady) {
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
    }

    if (gun < m_gunRuntimeData.size()) {
        m_gunRuntimeData[gun].orderNo = tradeNo;
        m_gunRuntimeData[gun].chargeUserNo = userNo;
    }

    *outData = data;
    return true;
}

bool CommProcess::parseYlcDischargeStop0E4(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData)
{
    if (!parseYlcRemoteStop036(body, bodyLen, gun, outData)) {
        return false;
    }
    cJSON_AddNumberToObject(*outData, "v2g", 1);
    cJSON_AddBoolToObject(*outData, "isDischarge", 1);
    return true;
}

bool CommProcess::parseYlcPileGunBody(const uint8_t* body, size_t bodyLen, uint8_t& gun) const
{
    if (!body || bodyLen != 8U) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }
    const int gunNo = bcdByteToInt(body[7]);
    if (gunNo <= 0 || gunNo > static_cast<int>(m_config.gunCount)) {
        return false;
    }
    gun = static_cast<uint8_t>(gunNo - 1);
    return true;
}

bool CommProcess::parseYlcReadRealtimeRequest(const uint8_t* body, size_t bodyLen, uint8_t& gun) const
{
    return parseYlcPileGunBody(body, bodyLen, gun);
}

bool CommProcess::parseYlcBalanceUpdate042(const uint8_t* body, size_t bodyLen, uint8_t& gun, std::vector<uint8_t>& physicalCard, cJSON** outData) const
{
    if (!body || !outData || bodyLen < 20U) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }
    const int gunNo = bcdByteToInt(body[7]);
    if (gunNo <= 0 || gunNo > static_cast<int>(m_config.gunCount)) {
        return false;
    }
    gun = static_cast<uint8_t>(gunNo - 1);
    physicalCard.assign(body + 8, body + 16);
    const uint32_t balanceRaw = static_cast<uint32_t>(body[16]) |
                                (static_cast<uint32_t>(body[17]) << 8) |
                                (static_cast<uint32_t>(body[18]) << 16) |
                                (static_cast<uint32_t>(body[19]) << 24);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "pileNo", recvPileCode.c_str());
    cJSON_AddNumberToObject(data, "gun", gun);
    cJSON_AddStringToObject(data, "physicalCardHex", toHex(physicalCard.data(), physicalCard.size()).c_str());
    cJSON_AddNumberToObject(data, "balanceRaw", static_cast<double>(balanceRaw));
    cJSON_AddNumberToObject(data, "balance", static_cast<double>(balanceRaw) / 100.0);
    cJSON_AddNumberToObject(data, "rawFrameType", 0x42);
    cJSON_AddStringToObject(data, "ylcRawHex", toHex(body, bodyLen).c_str());
    *outData = data;
    return true;
}

bool CommProcess::parseYlcOfflineCardSync044(const uint8_t* body, size_t bodyLen, cJSON** outData) const
{
    if (!body || !outData || bodyLen < 8U) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }
    const uint8_t count = body[7];
    if (count == 0U || count > 15U || bodyLen < 8U + static_cast<size_t>(count) * 16U) {
        return false;
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "pileNo", recvPileCode.c_str());
    cJSON_AddNumberToObject(data, "gun", 0);
    cJSON_AddNumberToObject(data, "cardCount", count);
    cJSON_AddNumberToObject(data, "rawFrameType", 0x44);
    cJSON_AddStringToObject(data, "ylcRawHex", toHex(body, bodyLen).c_str());
    cJSON* cards = cJSON_CreateArray();
    size_t pos = 8U;
    for (uint8_t i = 0; i < count; ++i) {
        cJSON* card = cJSON_CreateObject();
        std::string logicCardNo;
        if (!bcdToDigitStringStrict(body + pos, 8, logicCardNo)) {
            cJSON_Delete(card);
            cJSON_Delete(cards);
            cJSON_Delete(data);
            return false;
        }
        cJSON_AddStringToObject(card, "logicCardNo", logicCardNo.c_str());
        pos += 8U;
        cJSON_AddStringToObject(card, "physicalCardHex", toHex(body + pos, 8).c_str());
        pos += 8U;
        cJSON_AddItemToArray(cards, card);
    }
    cJSON_AddItemToObject(data, "cards", cards);
    cJSON_AddItemToObject(data, "cardList", cJSON_Duplicate(cards, 1));
    *outData = data;
    return true;
}

bool CommProcess::parseYlcOfflineCardCards(const uint8_t* body,
                                           size_t bodyLen,
                                           size_t maxCount,
                                           const char* countName,
                                           std::vector<std::vector<uint8_t> >& physicalCards,
                                           cJSON** outData) const
{
    if (!body || !outData || bodyLen < 8U) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }
    const uint8_t count = body[7];
    if (count == 0U || count > maxCount || bodyLen < 8U + static_cast<size_t>(count) * 8U) {
        return false;
    }

    physicalCards.clear();
    physicalCards.reserve(count);
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "pileNo", recvPileCode.c_str());
    cJSON_AddNumberToObject(data, "gun", 0);
    cJSON_AddNumberToObject(data, countName ? countName : "cardCount", count);
    cJSON_AddStringToObject(data, "ylcRawHex", toHex(body, bodyLen).c_str());
    cJSON* cards = cJSON_CreateArray();
    size_t pos = 8U;
    for (uint8_t i = 0; i < count; ++i) {
        std::vector<uint8_t> card(body + pos, body + pos + 8U);
        physicalCards.push_back(card);
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "physicalCardHex", toHex(card.data(), card.size()).c_str());
        cJSON_AddItemToArray(cards, item);
        pos += 8U;
    }
    cJSON_AddItemToObject(data, "cards", cards);
    cJSON_AddItemToObject(data, "cardList", cJSON_Duplicate(cards, 1));
    *outData = data;
    return true;
}

bool CommProcess::parseYlcWorkParamSet052(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData) const
{
    if (!body || !outData || bodyLen != 9U) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }

    gun = 0;
    const uint8_t allowWork = body[7];
    const uint8_t maxPowerPercent = body[8];

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "pileNo", recvPileCode.c_str());
    cJSON_AddNumberToObject(data, "gun", gun);
    cJSON_AddNumberToObject(data, "allowWork", allowWork);
    cJSON_AddNumberToObject(data, "maxPowerPercent", maxPowerPercent);
    cJSON_AddNumberToObject(data, "rawFrameType", 0x52);
    cJSON_AddStringToObject(data, "ykcRawHex", toHex(body, bodyLen).c_str());
    *outData = data;
    return true;
}

bool CommProcess::parseYlcParkingLockCtrl062(const uint8_t* body, size_t bodyLen, uint8_t& gun, cJSON** outData) const
{
    if (!body || !outData || bodyLen < 13U) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }
    const uint8_t gunNo = body[7];
    if (gunNo == 0U || gunNo > m_config.gunCount) {
        return false;
    }
    gun = static_cast<uint8_t>(gunNo - 1U);
    const uint8_t action = body[8];
    const char* actionText = "unknown";
    if (action == 0x55U) {
        actionText = "raise";
    } else if (action == 0xFFU) {
        actionText = "lower";
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "pileNo", recvPileCode.c_str());
    cJSON_AddNumberToObject(data, "gun", gun);
    cJSON_AddStringToObject(data, "lockAction", actionText);
    cJSON_AddNumberToObject(data, "lockActionRaw", action);
    cJSON_AddStringToObject(data, "reserveHex", toHex(body + 9, 4).c_str());
    cJSON_AddNumberToObject(data, "rawFrameType", 0x62);
    cJSON_AddStringToObject(data, "ylcRawHex", toHex(body, bodyLen).c_str());
    *outData = data;
    return true;
}

bool CommProcess::parseYlcRemoteReboot092(const uint8_t* body, size_t bodyLen, cJSON** outData) const
{
    if (!body || !outData || bodyLen < 8U) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "pileNo", recvPileCode.c_str());
    cJSON_AddNumberToObject(data, "gun", 0);
    cJSON_AddNumberToObject(data, "executeControl", body[7]);
    cJSON_AddNumberToObject(data, "rawFrameType", 0x92);
    cJSON_AddStringToObject(data, "ylcRawHex", toHex(body, bodyLen).c_str());
    *outData = data;
    return true;
}

bool CommProcess::parseYlcRemoteUpdate094(const uint8_t* body, size_t bodyLen, cJSON** outData) const
{
    if (!body || !outData || bodyLen < 94U) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }
    size_t pos = 7U;
    const uint8_t chargerType = body[pos++];
    const uint16_t chargerPowerRaw = static_cast<uint16_t>(body[pos]) |
                                     (static_cast<uint16_t>(body[pos + 1U]) << 8);
    pos += 2U;
    const std::string serverHost = asciiFixedToString(body + pos, 16);
    pos += 16U;
    const uint16_t serverPort = static_cast<uint16_t>(body[pos]) |
                                (static_cast<uint16_t>(body[pos + 1U]) << 8);
    pos += 2U;
    const std::string username = asciiFixedToString(body + pos, 16);
    pos += 16U;
    const std::string password = asciiFixedToString(body + pos, 16);
    pos += 16U;
    const std::string filePath = asciiFixedToString(body + pos, 32);
    pos += 32U;
    const uint8_t executeControl = body[pos++];
    const uint8_t timeoutMinutes = body[pos++];

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "pileNo", recvPileCode.c_str());
    cJSON_AddNumberToObject(data, "gun", 0);
    cJSON_AddNumberToObject(data, "chargerType", chargerType);
    cJSON_AddNumberToObject(data, "chargerPowerRaw", chargerPowerRaw);
    cJSON_AddStringToObject(data, "serverHost", serverHost.c_str());
    cJSON_AddNumberToObject(data, "serverPort", serverPort);
    cJSON_AddStringToObject(data, "username", username.c_str());
    cJSON_AddStringToObject(data, "password", password.c_str());
    cJSON_AddStringToObject(data, "filePath", filePath.c_str());
    cJSON_AddNumberToObject(data, "executeControl", executeControl);
    cJSON_AddNumberToObject(data, "timeoutMinutes", timeoutMinutes);
    cJSON_AddNumberToObject(data, "rawFrameType", 0x94);
    cJSON_AddStringToObject(data, "ylcRawHex", toHex(body, bodyLen).c_str());
    *outData = data;
    return true;
}

bool CommProcess::parseYlcRemoteLogRequest096(const uint8_t* body, size_t bodyLen, cJSON** outData) const
{
    if (!body || !outData || bodyLen < 92U) {
        return false;
    }
    std::string recvPileCode;
    if (!bcdToDigitStringStrict(body, 7, recvPileCode)) {
        return false;
    }
    if (recvPileCode != normalizePileCode14(m_config.cdzNo)) {
        return false;
    }
    size_t pos = 7U;
    const uint8_t chargerType = body[pos++];
    const std::string serverHost = asciiFixedToString(body + pos, 16);
    pos += 16U;
    const uint16_t serverPort = static_cast<uint16_t>(body[pos]) |
                                (static_cast<uint16_t>(body[pos + 1U]) << 8);
    pos += 2U;
    const std::string username = asciiFixedToString(body + pos, 16);
    pos += 16U;
    const std::string password = asciiFixedToString(body + pos, 16);
    pos += 16U;
    const std::string filePath = asciiFixedToString(body + pos, 32);
    pos += 32U;
    const uint8_t executeControl = body[pos++];
    const uint8_t timeoutMinutes = body[pos++];

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "pileNo", recvPileCode.c_str());
    cJSON_AddNumberToObject(data, "gun", 0);
    cJSON_AddNumberToObject(data, "chargerType", chargerType);
    cJSON_AddStringToObject(data, "serverHost", serverHost.c_str());
    cJSON_AddNumberToObject(data, "serverPort", serverPort);
    cJSON_AddStringToObject(data, "username", username.c_str());
    cJSON_AddStringToObject(data, "password", password.c_str());
    cJSON_AddStringToObject(data, "filePath", filePath.c_str());
    cJSON_AddNumberToObject(data, "executeControl", executeControl);
    cJSON_AddNumberToObject(data, "timeoutMinutes", timeoutMinutes);
    cJSON_AddNumberToObject(data, "rawFrameType", 0x96);
    cJSON_AddStringToObject(data, "ylcRawHex", toHex(body, bodyLen).c_str());
    *outData = data;
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

std::string CommProcess::buildGunQrCode(uint8_t gun) const
{
    // BY LZW: YLC 二维码优先使用平台下发/ini配置值，为空时才用 cdzNo+两位枪号兜底。
    if (gun < m_config.gunQrCodeList.size() && !m_config.gunQrCodeList[gun].empty()) {
        return m_config.gunQrCodeList[gun];
    }
    char gunNoText[3] = {0};
    std::snprintf(gunNoText, sizeof(gunNoText), "%02u", static_cast<unsigned int>(gun + 1));
    return m_config.cdzNo + gunNoText;
}

cJSON* CommProcess::buildSetConfigData(uint8_t gun) const
{
    cJSON* data = cJSON_CreateObject();
    if (!data) {
        return nullptr;
    }

    cJSON_AddNumberToObject(data, "gunId",
                            (gun < m_config.gunIdList.size()) ? static_cast<double>(m_config.gunIdList[gun]) : 0.0);
    cJSON_AddNumberToObject(data, "gunNo", static_cast<int>(gun) + 1);
    cJSON_AddStringToObject(data, "cdzNo", m_config.cdzNo.c_str());
    cJSON_AddStringToObject(data, "factoryCreditCode", m_config.factoryCreditCode.c_str());
    cJSON_AddStringToObject(data, "macAddr", m_config.macAddr.c_str());
    cJSON_AddStringToObject(data, "qrCode", buildGunQrCode(gun).c_str());

    if (gun < m_feeModelByGun.size()) {
        const FeeModel& fm = m_feeModelByGun[gun];
        cJSON_AddStringToObject(data, "feeModelId", fm.feeModelId.c_str());
        const std::string ylcNo = (gun < m_ylcFeeModelNoByGun.size() && !m_ylcFeeModelNoByGun[gun].empty())
            ? m_ylcFeeModelNoByGun[gun] : "0000";
        cJSON_AddStringToObject(data, "ylcFeeModelNo", ylcNo.c_str());
        cJSON_AddNumberToObject(data, "timeNum", static_cast<int>(fm.timeNum));

        cJSON* timeSeg = cJSON_CreateArray();
        cJSON* segFlag = cJSON_CreateArray();
        cJSON* chargeFee = cJSON_CreateArray();
        cJSON* serviceFee = cJSON_CreateArray();
        if (timeSeg && segFlag && chargeFee && serviceFee) {
            for (size_t i = 0; i < fm.timeSeg.size(); ++i) {
                cJSON_AddItemToArray(timeSeg, cJSON_CreateString(fm.timeSeg[i].c_str()));
            }
            for (size_t i = 0; i < fm.segFlag.size(); ++i) {
                cJSON_AddItemToArray(segFlag, cJSON_CreateNumber(static_cast<double>(fm.segFlag[i])));
            }
            for (size_t i = 0; i < fm.chargeFee.size(); ++i) {
                cJSON_AddItemToArray(chargeFee, cJSON_CreateNumber(static_cast<double>(fm.chargeFee[i]) / 100000.0));
            }
            for (size_t i = 0; i < fm.serviceFee.size(); ++i) {
                cJSON_AddItemToArray(serviceFee, cJSON_CreateNumber(static_cast<double>(fm.serviceFee[i]) / 100000.0));
            }
            cJSON_AddItemToObject(data, "timeSeg", timeSeg);
            cJSON_AddItemToObject(data, "segFlag", segFlag);
            cJSON_AddItemToObject(data, "chargeFee", chargeFee);
            cJSON_AddItemToObject(data, "serviceFee", serviceFee);
        } else {
            if (timeSeg) cJSON_Delete(timeSeg);
            if (segFlag) cJSON_Delete(segFlag);
            if (chargeFee) cJSON_Delete(chargeFee);
            if (serviceFee) cJSON_Delete(serviceFee);
        }
    }

    return data;
}

bool CommProcess::publishFeeModelSetConfig(uint8_t gun)
{
    cJSON* data = buildSetConfigData(gun);
    if (!data) {
        return false;
    }
    const bool ok = publishSetConfig(gun, data);
    cJSON_Delete(data);
    return ok;
}

bool CommProcess::markPlatformOnlineAfterFeeModel(const char* reason)
{
    m_loginState = LOGIN_ONLINE;
    resetOfflineReplayState();
    m_lastHeartbeat = std::chrono::steady_clock::now();
    m_lastHeartbeatRecv = std::chrono::steady_clock::now();
    m_heartbeatCounter = 0;
    if (!m_platformOnlineEventActive) {
        m_platformOnlineEventActive = true;
        publishPlatformLinkEvent(true, reason ? reason : "login_ready");
    }
    m_forcePluggedChargeInfoByGun.assign(static_cast<size_t>(m_config.gunCount), 1);
    m_runtimeChangedByGun.assign(static_cast<size_t>(m_config.gunCount), 1);
    return true;
}

void CommProcess::resetOfflineReplayState()
{
    m_afterOfflineReplayState = LOGIN_REQ_FEE_MODEL_CHECK;
    m_offlineReplayRequested = false;
    m_offlineReplaySawRecord = false;
    m_offlineReplayPendingTrades.clear();
    m_offlineReplayStartTime = std::chrono::steady_clock::time_point();
    m_offlineReplayLastActivity = std::chrono::steady_clock::time_point();
}

void CommProcess::enterOfflineReplayState(const std::chrono::steady_clock::time_point& now, PlatformLoginState nextState, const char* reason)
{
    resetOfflineReplayState();
    m_afterOfflineReplayState = nextState;
    m_loginState = LOGIN_UPLOAD_OFFLINE_DATA;
    m_lastLoginAction = now;
    m_logSender.info("platform_login_step", reason ? reason : "enter_offline_replay_after_fee_check");
}

void CommProcess::enterAfterOfflineReplayState(const std::chrono::steady_clock::time_point& now, const char* reason)
{
    const PlatformLoginState nextState = m_afterOfflineReplayState;
    if (reason && *reason) {
        m_logSender.info("platform_login_step", reason);
    }

    if (nextState == LOGIN_ONLINE) {
        (void)markPlatformOnlineAfterFeeModel("offline_replay_done_login_ready");
        return;
    }

    resetOfflineReplayState();
    if (nextState == LOGIN_REQ_FEE_MODEL) {
        m_loginState = LOGIN_REQ_FEE_MODEL;
        m_lastLoginAction = now - std::chrono::seconds(m_config.loginRetrySec);
        m_logSender.info("platform_login_step", "offline_replay_done_fee_model_req");
        return;
    }

    m_loginState = LOGIN_REQ_FEE_MODEL_CHECK;
    m_lastLoginAction = now - std::chrono::seconds(m_config.loginRetrySec);
    m_logSender.info("platform_login_step", "offline_replay_done_fee_model_check");
}

void CommProcess::driveOfflineReplayState(const std::chrono::steady_clock::time_point& now)
{
    if (!m_offlineReplayRequested) {
        m_offlineReplayRequested = true;
        m_offlineReplayStartTime = now;
        m_offlineReplayLastActivity = now;
        m_logSender.requestUnconfirmedTradeRecords(kOfflineReplayQueryLimit);
        m_logSender.info("platform_login_step", "offline_replay_query_sent");
        return;
    }

    const std::chrono::steady_clock::time_point base =
        (m_offlineReplayLastActivity.time_since_epoch().count() != 0)
            ? m_offlineReplayLastActivity
            : m_offlineReplayStartTime;

    if (!m_offlineReplaySawRecord) {
        if (now - m_offlineReplayStartTime >= std::chrono::seconds(kOfflineReplayEmptyWaitSec)) {
            enterAfterOfflineReplayState(now, "offline_replay_empty_done");
        }
        return;
    }

    if (m_offlineReplayPendingTrades.empty()) {
        enterAfterOfflineReplayState(now, "offline_replay_confirmed_done");
        return;
    }

    if (now - base >= std::chrono::seconds(kOfflineReplayConfirmWaitSec)) {
        std::ostringstream oss;
        oss << "pending=" << m_offlineReplayPendingTrades.size();
        m_logSender.warn("platform_login_step", std::string("offline_replay_confirm_timeout ") + oss.str());
        enterAfterOfflineReplayState(now, "offline_replay_timeout_done");
    }
}

void CommProcess::trackOfflineReplayTradeIfNeeded(const std::string& tradeNo)
{
    if (m_loginState != LOGIN_UPLOAD_OFFLINE_DATA || tradeNo.empty()) {
        return;
    }
    m_offlineReplaySawRecord = true;
    m_offlineReplayLastActivity = std::chrono::steady_clock::now();
    m_offlineReplayPendingTrades.insert(tradeNo);
    m_logSender.info("platform_login_step", std::string("offline_replay_trade_sent tradeNo=") + tradeNo);
}

void CommProcess::markOfflineReplayTradeConfirmed(const std::string& tradeNo)
{
    if (tradeNo.empty() || m_offlineReplayPendingTrades.empty()) {
        return;
    }
    const size_t removed = m_offlineReplayPendingTrades.erase(tradeNo);
    if (removed != 0U) {
        m_offlineReplayLastActivity = std::chrono::steady_clock::now();
        m_logSender.info("platform_login_step", std::string("offline_replay_trade_confirmed tradeNo=") + tradeNo);
    }
}

bool CommProcess::publishSetConfig(uint8_t gun, cJSON* dataObj)
{
    if (!dataObj) {
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
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
    cJSON* qrItem = cJSON_GetObjectItem(dataCopy, "qrCode");
    const bool hasQr = qrItem && cJSON_IsString(qrItem) && qrItem->valuestring && qrItem->valuestring[0] != '\0';
    if (!hasQr) {
        cJSON_DeleteItemFromObject(dataCopy, "qrCode");
        cJSON_AddStringToObject(dataCopy, "qrCode", buildGunQrCode(gun).c_str());
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
    // BY LZW: 离线运行模式下，统一对外上报平台通信正常，便于脱网联调。
    if (m_config.offlineRunMode) {
        online = true;
        reason = "offline_mode";
        m_platformOnlineEventActive = true;
    }
    if (online) {
        m_platformOfflineTimeoutReported = false;
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

bool CommProcess::persistGunQrCodeToIni(uint8_t gun, const std::string& qrCode)
{
    if (gun >= m_config.gunCount) {
        return false;
    }
    if (gun >= m_config.gunQrCodeList.size()) {
        m_config.gunQrCodeList.resize(static_cast<size_t>(m_config.gunCount));
    }
    m_config.gunQrCodeList[gun] = qrCode;

    ConfigManagerLite& cfg = getConfig();
    const std::string section = "Comm";
    std::ostringstream key;
    key << "gun" << static_cast<int>(gun + 1) << "_qrcode";
    cfg.setString(section, key.str(), qrCode);
    // BY LZW: 二维码下发后固化到目标机配置文件。
    return cfg.saveConfig("/usr/app/config/tcu_comm.ini");
}

bool CommProcess::persistSm2PubKeyToIni(const std::string& pubKey)
{
    if (pubKey.empty()) {
        return false;
    }

    ConfigManagerLite& cfg = getConfig();
    const std::string section = "Comm";
    cfg.setString(section, "sm2_public_key", pubKey);
    // BY LZW: 平台在0x02下发的新SM2公钥需要固化，避免重启后继续使用旧公钥。
    return cfg.saveConfig("/usr/app/config/tcu_comm.ini");
}

void CommProcess::publishInitialSetConfig()
{
    const uint8_t gunCount = static_cast<uint8_t>(m_config.gunIdList.size());
    for (uint8_t gun = 0; gun < gunCount; ++gun) {
        cJSON* data = buildSetConfigData(gun);
        if (!data || !publishSetConfig(gun, data)) {
            m_logSender.warn("platform_setconfig_bootstrap", "publish_fail");
        }
        if (data) {
            cJSON_Delete(data);
        }
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
