/**
 * 故障点表与原因映射公共模块
 * BY ZF
 */

#include "fault_reason_mapper.h"
#include <map>
#include <cstdio>
#include <cstdlib>

namespace {
    static const unsigned int REASON_STAGE_START    = 0x10000;
    static const unsigned int REASON_STAGE_CHARGING = 0x20000;
    static const unsigned int REASON_STAGE_STANDBY  = 0x30000;

    FaultPointRule MakeRule(bool treatAsFault, int faultType, unsigned int reason, const char* message)
    {
        return FaultPointRule(treatAsFault, faultType, reason, message ? message : "");
    }

    unsigned int makeReason(int faultType, unsigned int code)
    {
        switch (faultType) {
        case FaultTypeDef::START_FAIL: return REASON_STAGE_START | (code & 0xFFFFU);
        case FaultTypeDef::CHARGING_FAIL: return REASON_STAGE_CHARGING | (code & 0xFFFFU);
        case FaultTypeDef::STANDBY: return REASON_STAGE_STANDBY | (code & 0xFFFFU);
        default: return 0;
        }
    }

    std::string makePointKey(const char* prefix, unsigned int code)
    {
        char buf[64] = {0};
        std::snprintf(buf, sizeof(buf), "%s_%04X", prefix, code & 0xFFFFU);
        return std::string(buf);
    }

    bool parsePointKey(const std::string& pointKey, std::string& prefix, unsigned int& code)
    {
        const std::string::size_type pos = pointKey.find_last_of('_');
        if (pos == std::string::npos || pos + 1 >= pointKey.size()) {
            return false;
        }
        prefix = pointKey.substr(0, pos);
        const std::string codeStr = pointKey.substr(pos + 1);
        char* endp = NULL;
        unsigned long v = std::strtoul(codeStr.c_str(), &endp, 16);
        if (endp == NULL || *endp != 0) {
            return false;
        }
        code = static_cast<unsigned int>(v & 0xFFFFUL);
        return true;
    }

    std::map<unsigned int, FaultPointRule> BuildStandbyRuleMap()
    {
        std::map<unsigned int, FaultPointRule> m;
        m[0x0001] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0001), "交流接触器故障");
        m[0x0002] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0002), "车辆接触器粘连");
        m[0x0003] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0003), "直流接触器1粘连");
        m[0x0004] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0004), "直流接触器2粘连");
        m[0x0005] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0005), "模块启动失败");
        m[0x0006] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0006), "绝缘检测告警");
        m[0x0007] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0007), "绝缘检测故障");
        m[0x0008] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0008), "模块关闭失败");
        m[0x0009] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0009), "泄放回路故障---粘连");
        m[0x000A] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x000A), "辅助电源故障");
        m[0x000B] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x000B), "握手阶段通讯超时");
        m[0x000C] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x000C), "配置阶段通讯超时");
        m[0x000D] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x000D), "充电阶段通讯超时");
        m[0x000E] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x000E), "软启动升压失败");
        m[0x000F] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x000F), "软启动电流异常");
        m[0x0010] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0010), "输出电流故障");
        m[0x0011] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0011), "结束阶段通讯超时");
        m[0x0012] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0012), "车辆通讯多包异常");
        m[0x0013] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0013), "充电模块通讯超时");
        m[0x0014] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0014), "充电模块故障");
        m[0x0015] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0015), "熔丝故障");
        m[0x0016] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0016), "过温故障");
        m[0x0017] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0017), "烟感故障");
        m[0x0018] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0018), "断路器故障");
        m[0x0019] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0019), "CC1电压异常");
        m[0x001A] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x001A), "PE断开故障");
        m[0x001B] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x001B), "功率模块不足");
        m[0x001C] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x001C), "车辆暂停充电");
        m[0x001D] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x001D), "车辆BSM异常");
        m[0x001E] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x001E), "违规拔枪");
        m[0x001F] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x001F), "电池电压过低1(距离第一次测得电池电压为0后2min内电池电压仍为0)");
        m[0x0020] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0020), "电池电压反接");
        m[0x0021] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0021), "车辆就绪超时");
        m[0x0022] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0022), "急停故障");
        m[0x0023] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0023), "防反二极管故障");
        m[0x0024] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0024), "充电桩控制器通讯故障（整流柜侧）");
        m[0x0025] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0025), "BMS超时重新握手3次后停止");
        m[0x0026] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0026), "BRO_AA之后又发BRO_00");
        m[0x0027] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0027), "电池电压过低2//");
        m[0x0028] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0028), "模块软启动阶段报文超时//");
        m[0x0029] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0029), "充电暂停阶段报文超时");
        m[0x002A] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x002A), "充电桩要求停止充电(ZLG)");
        m[0x002B] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x002B), "充电桩与计费模块通讯超时（CDZ）");
        m[0x002C] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x002C), "充电桩与整流柜通讯超时（CDZ）");
        m[0x002D] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x002D), "输出电压超过BCP中最高允许电压（CDZ）");
        m[0x002E] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x002E), "输出电流超过BCP中最高允许电流（CDZ）");
        m[0x002F] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x002F), "BCS中最高单体电池电压超过BCP中最高允许单体电池电压（CDZ）");
        m[0x0030] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0030), "BSM中最高动力蓄电池温度超过BCP中单体动力蓄电池最高允许温度（CDZ）");
        m[0x0031] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0031), "BCL电压需求超过BCP中最大允许电压");
        m[0x0032] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0032), "BCL电流需求超过BCP中最大允许电流");
        m[0x0033] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0033), "BCP最大允许电压比充电机最小输出电压低");
        m[0x0034] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0034), "尝试3次仍然收不到BRO = 0XAA");
        m[0x0035] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0035), "充电桩侧收到APP要求停止充电(CDZ)");
        m[0x0036] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0036), "充电桩侧收到BCU要求停止充电(CDZ)");
        m[0x0037] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0037), "防雷器故障（ZLG）");
        m[0x0038] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0038), "主从板识别故障");
        m[0x0039] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0039), "CDZ地址识别故障");
        m[0x003A] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x003A), "接收到BST");
        m[0x003B] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x003B), "模块组中有模块有故障，但是不是所有模块有故障");
        m[0x003C] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x003C), "电子锁锁止故障（LEVEL2）");
        m[0x003D] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x003D), "zlg与can盒通讯故障");
        m[0x003E] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x003E), "cdz从暂停状态恢复到充电状态");
        m[0x003F] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x003F), "充电过程中CC1电压波动超过指定值");
        m[0x0040] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0040), "车辆BSM报SOC过低");
        m[0x0041] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0041), "泄放回路告警---拒动误动");
        m[0x0042] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0042), "BHM中最大电压输出小于充电机最低输出电压");
        m[0x0043] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0043), "切换开关遥信故障");
        m[0x0044] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0044), "车辆要求暂停充电超过10min");
        m[0x0045] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0045), "车辆SOC 100%维持30s");
        m[0x0046] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0046), "计费控制单元遥控电子锁 合");
        m[0x0047] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0047), "计费控制单元遥控电子锁 分");
        m[0x0048] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0048), "检测到电池电压与BCP中当前电池电压相差超过充电机最高输出电压的5%");
        m[0x0050] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0050), "检测到门禁");
        m[0x0051] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0051), "枪到位检测");
        m[0x0052] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0052), "电池电压过高，超过充电机额定电压10V以上");
        m[0x0053] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0053), "电池电压过高，超过电池最大允许充电电压10V以上");
        m[0x0054] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0054), "充电桩与接口板通讯超时（CDZ）");
        m[0x0055] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0055), "充电桩与IO板通讯超时（CDZ）");
        m[0x0056] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0056), "充电桩控制器地址冲突");
        m[0x0057] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0057), "水浸故障");
        m[0x0058] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0058), "超过充电机最大输出电压+20V");
        m[0x0059] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0059), "超过充电机最大输出电流+20V");
        m[0x005A] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x005A), "低于充电机最小输出电压（欠压）");
        m[0x005B] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x005B), "tcu充电启动完成帧应答超时");
        m[0x005C] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x005C), "BRM 报文数据项异常（报文数据长度错误，BMS 通信协议版本号错误，其他数据错误）");
        m[0x005D] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x005D), "BCP 报文数据项异常（报文数据长度错误，电池当前电压越限，其他数据项错误）");
        m[0x005E] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x005E), "BRM中的BMS版本非1.10");
        m[0x005F] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x005F), "BCP中电压超过范围");
        m[0x0060] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0060), "直流母线正接触器粘连");
        m[0x0061] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0061), "直流母线负接触器粘连");
        m[0x0062] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0062), "直流母线正接触器拒动/误动");
        m[0x0063] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0063), "直流母线负接触器拒动/误动");
        m[0x0064] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0064), "输出电压欠压故障");
        m[0x0065] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0065), "非即插即充设备");
        m[0x0066] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0066), "辅助电源电压切换回路故障（仅具备辅助电源电压切换功能的直流桩使用）//");
        m[0x0067] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0067), "整流柜io2板通信故障");
        m[0x0069] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0069), "功率控制器遥信状态中有故障//");
        m[0x0070] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0070), "功率控制器遥信状态中有故障");
        m[0x0071] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0071), "接触器DC+遥信故障/闭合故障");
        m[0x0072] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0072), "接触器DC-遥信故障/闭合故障");
        m[0x0073] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0073), "TCU下发VIN码鉴权失败");
        m[0x0074] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0074), "TCU下发VIN码鉴权信息超时");
        m[0x0075] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0075), "鉴权VIN与车辆VIN不一致");
        m[0x0076] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0076), "充电过程中功率控制器工作状态不是开机状态");
        m[0x0077] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0077), "充电枪未到位告警");
        m[0x0078] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0078), "电池电压过高故障");
        m[0x0079] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0079), "充电枪头温度过高故障，超过120度");
        m[0x0080] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0080), "需求电压超过BCP中最大允许电压");
        m[0x0081] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0081), "需求电流超过BCP中最大允许电流");
        m[0x0082] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0082), "充电枪投温度在90度到120度之间超过10min");
        m[0x0083] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0083), "短路保护// SDK ADD start");
        m[0x0084] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0084), "功控禁止充电（0x22遥信中）");
        m[0x0085] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0085), "功控回复启动完成帧中确认失败");
        m[0x0086] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0086), "设备离线告警（sdk离线），需要能够充电，点故障灯");
        m[0x0087] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0087), "电表通讯故障");
        m[0x0088] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0088), "电表数据异常故障");
        m[0x0089] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0089), "程序更新中");
        m[0x008A] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x008A), "检修中");
        m[0x008B] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x008B), "功控要求暂停本次充电//SDK ADD end//1.5 TCU ADD start");
        m[0x008C] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x008C), "非法VIN");
        m[0x008D] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x008D), "电子锁解锁故障（LEVEL 1）//1.5 TCU ADD end");
        m[0x00A0] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00A0), "从板绝缘故障");
        m[0x00A1] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00A1), "从板绝缘超时");
        m[0x00A2] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00A2), "从板电池电压故障");
        m[0x00A3] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00A3), "从板电池电压检测超时");
        m[0x00A4] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00A4), "从板遥控超时//SDK ADD start");
        m[0x00B0] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00B0), "BMS超时重新握手3次后停止-BRM");
        m[0x00B1] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00B1), "BMS超时重新握手3次后停止-BCP");
        m[0x00B2] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00B2), "BMS超时重新握手3次后停止-BRO");
        m[0x00B3] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00B3), "BMS超时重新握手3次后停止-BCL");
        m[0x00B4] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00B4), "BMS超时重新握手3次后停止-BCS");
        m[0x00B5] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00B5), "BMS超时重新握手3次后停止-BST");
        m[0x00B6] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00B6), "BMS超时重新握手3次后停止-BSD");
        m[0x00C0] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00C0), "车辆BSM异常-电压过高");
        m[0x00C1] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00C1), "车辆BSM异常-电压过低");
        m[0x00C2] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00C2), "车辆BSM异常-SOC过高");
        m[0x00C3] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00C3), "车辆BSM异常-SOC过低");
        m[0x00C4] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00C4), "车辆BSM异常-电流过高");
        m[0x00C5] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00C5), "车辆BSM异常-温度过高");
        m[0x00C6] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00C6), "车辆BSM异常-绝缘异常");
        m[0x00C7] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00C7), "车辆BSM异常-连接器异常");
        m[0x00D0] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00D0), "接收到BST-正常终止");
        m[0x00D1] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00D1), "接收到BST-绝缘故障");
        m[0x00D2] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00D2), "接收到BST-BMS元件过温");
        m[0x00D3] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00D3), "接收到BST-电池电压异常");
        m[0x00D4] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00D4), "接收到BST-其他异常终止");
        m[0x00D5] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00D5), "接收到BST-正常终止-达到目标值（总电压、单体电压达到设定值）");
        m[0x00D6] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00D6), "接收到BST-正常终止-达到目标值（soc达到目标值）//SDK ADD end//1.5TCU  ADD start");
        m[0x00E0] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00E0), "接收到BST-正常终止-（soc达到目标值）");
        m[0x00E1] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00E1), "接收到BST-正常终止-达到目标值（总电压达到设定值）");
        m[0x00E2] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00E2), "接收到BST-正常终止-达到目标值（单体电压达到设定值)");
        m[0x00E3] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00E3), "接收到BST-绝缘故障");
        m[0x00E4] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00E4), "接收到BST-输出连接器过温故障");
        m[0x00E5] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00E5), "接收到BST-BMS元件、输出连接器过温");
        m[0x00E6] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00E6), "接收到BST-充电连接器故障");
        m[0x00E7] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00E7), "接收到BST-电池组温度过高故障");
        m[0x00E8] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00E8), "接收到BST-高压继电器故障");
        m[0x00E9] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00E9), "接收到BST-检测点2电压检测故障");
        m[0x00EA] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00EA), "接收到BST-其他故障");
        m[0x00EB] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00EB), "接收到BST-电流过大");
        m[0x00EC] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00EC), "接收到BST-电压异常");
        m[0x00ED] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x00ED), "接收到BST-BST停止未知故障//1.5 TCU ADD end//ZLG上送遥信故障 从0x0100开始定义 200114SY 直接在原定义上加0x0100//表 16 功率控制模块工作状态及故障信息数据帧 PF:0x22");
        m[0x0131] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0131), "Bit31：充电机柜交流进线塑壳断路器故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0130] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0130), "Bit30：充电机柜交流输入接触器故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0129] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0129), "Bit29：充电机柜烟雾故障（可选项）  0x00 --- 正常 0x01 --- 故障");
        m[0x0128] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0128), "Bit28：充电机柜散热风机故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0127] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0127), "Bit27：充电机柜加热部件故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0126] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0126), "Bit26：充电机柜开门故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0125] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0125), "Bit25：充电机柜水浸故障（可选）  0x00 --- 正常 0x01 --- 故障");
        m[0x0124] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0124), "Bit24：充电机柜输入电压过压故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0123] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0123), "Bit23：充电机柜输入电压欠压故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0122] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0122), "Bit22：充电机柜输入电压缺相故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0121] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0121), "Bit21：充电机模块故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0120] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0120), "Bit20：开关模块故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0119] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0119), "Bit19：充电机柜过温故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0118] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0118), "Bit18：功率控制器接收遥控、遥信报文超时故障 16  0x00 --- 正常 0x01 --- 故障");
        m[0x0117] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0117), "Bit17：充电机柜交流输入接触器粘连故障 0x00 --- 正常 0x01 --- 故障");
        m[0x0116] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0116), "Bit16：充电机柜避雷器故障  0x00 --- 正常 0x01 --- 故障");
        m[0x0115] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0115), "Bit15：环境湿度报警0x00 --- 正常 0x01 --- 故障");
        m[0x0114] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0114), "Bit14：无空闲模块可用（具备分配功率机型）  0x00 --- 正常 0x01 --- 故障");
        m[0x0113] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0113), "Bit13：功率控制模块与充电控制模块通讯超时0x00 --- 正常 0x01 --- 故障");
        m[0x0112] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0112), "Bit12：功率控制模块与开关模块通讯超时  0x00 --- 正常 0x01 --- 故障");
        m[0x0111] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0111), "Bit11：功率控制器间通讯超时（功率扩容） 16  0x00 --- 正常 0x01 --- 故障");
        m[0x0110] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0110), "Bit10：功率控制器与环境信息采集模块通讯超时（功率扩容 0x00 --- 正常 0x01 --- 故障");
        m[0x0109] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0109), "Bit9：功率控制器与充电模块通讯超时（0x00 --- 正常 0x01 --- 故障");
        m[0x0108] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0108), "Bit8:充电模块地址冲突（0x00 --- 正常 0x01 --- 故障");
        m[0x0101] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0101), "电表通信中断");
        m[0x0102] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0102), "平台通信中断");
        m[0x0103] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0103), "logic状态超时");
        m[0x0104] = MakeRule(true, FaultTypeDef::STANDBY, makeReason(FaultTypeDef::STANDBY, 0x0104), "主控通信故障");
        return m;
    }

    std::map<unsigned int, FaultPointRule> BuildStartRuleMap()
    {
        std::map<unsigned int, FaultPointRule> m;
        m[0x00] = MakeRule(false, FaultTypeDef::START_FAIL, 0, "成功");
        m[0x01] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x01), "充电中控制导引故障");
        m[0x02] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x02), "充电控制模块与计费控制单元通讯超时");
        m[0x03] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x03), "急停按钮动作故障");
        m[0x04] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x04), "门禁故障（充电桩）");
        m[0x05] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x05), "避雷器故障");
        m[0x06] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x06), "烟雾故障");
        m[0x07] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x07), "交流输入断路器故障");
        m[0x08] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x08), "交流输入接触器拒动/误动故障");
        m[0x09] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x09), "交流输入接触器粘连故障");
        m[0x0A] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x0A), "交流输入故障（过压，欠压，缺相，过流）");
        m[0x0B] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x0B), "充电桩过温故障");
        m[0x0C] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x0C), "充电接口过温故障");
        m[0x0D] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x0D), "充电接口电子锁故障");
        m[0x0E] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x0E), "绝缘监测故障");
        m[0x0F] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x0F), "电池极性反接故障");
        m[0x10] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x10), "直流输出接触器拒动/误动故障");
        m[0x11] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x11), "直流输出接触器粘连故障");
        m[0x12] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x12), "直流输出熔断器故障");
        m[0x13] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x13), "并联接触器拒动/误动故障");
        m[0x14] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x14), "并联接触器粘连故障");
        m[0x15] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x15), "泄放回路故障");
        m[0x17] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x17), "充电机模块故障");
        m[0x18] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x18), "直流输出电压过压故障");
        m[0x19] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x19), "直流输出电压欠压故障");
        m[0x1A] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x1A), "直流输出电流过流故障");
        m[0x1B] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x1B), "直流输出短路故障");
        m[0x1C] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x1C), "电池最高允许充电电压小于充电机最小输出电压");
        m[0x1D] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x1D), "绝缘监测前直流输出接触器外侧电压≥10V");
        m[0x1E] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x1E), "启动充电前直流输出接触器外侧电压与通信报文电池电压相差＞±5%");
        m[0x1F] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x1F), "启动充电前直流输出接触器外侧电压小于充电机最小输出电压");
        m[0x20] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x20), "启动充电前直流输出接触器外侧电压大于充电机最大输出电压");
        m[0x21] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x21), "电池端电压大于电池最高允许充电电压");
        m[0x22] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x22), "BRM 报文接收超时");
        m[0x23] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x23), "BRM 报文数据项异常（报文数据长度错误，BMS 通信协议版本号错误，其他数据错误）");
        m[0x24] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x24), "BCP 报文接收超时");
        m[0x25] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x25), "BCP 报文数据项异常（报文数据长度错误，电池当前电压越限，其他数据项错误）");
        m[0x26] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x26), "BRO 报文（0x00）接收超时");
        m[0x27] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x27), "BRO 报文（0xAA）接收超时");
        m[0x28] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x28), "预留");
        m[0x29] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x29), "BCL报文接收超时");
        m[0x2A] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x2A), "BCS报文接收超时");
        m[0x2B] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x2B), "门禁故障（充电机柜，仅直流桩使用）");
        m[0x2C] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x2C), "水浸故障（充电桩）");
        m[0x2D] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x2D), "水浸故障（充电机柜，仅直流桩使用）");
        m[0x2E] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x2E), "直流充电控制模块与功率控制模块通信超时（仅直流桩使用）");
        m[0x2F] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x2F), "功率控制模块与开关模块通信超时（仅直流桩使用）");
        m[0x30] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x30), "开关模块故障（仅直流桩使用）");
        m[0x31] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x31), "BMS通信协议版本不匹配");
        m[0x32] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x32), "VIN码非法");
        m[0x33] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x33), "即插即充鉴权失败");
        m[0x34] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x34), "平台鉴权超时");
        m[0x35] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x35), "车辆识别鉴权帧超时");
        m[0x36] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x36), "鉴权VIN与车辆VIN不一致");
        m[0x37] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x37), "低压辅助电源模块输出故障");
        m[0x38] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x38), "PE断线故障");
        m[0x39] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x39), "控制导引电压异常");
        m[0x3A] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x3A), "充电机柜过温故障（充电机柜，仅直流桩使用）");
        m[0x3B] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x3B), "充电桩执行功率分配策略失败故障");
        m[0x3C] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x3C), "无空闲模块可用（具备分配功率机型）");
        m[0x3D] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x3D), "预充阶段调压失败");
        m[0x3E] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x3E), "BRO准备就绪后取消");
        m[0x3F] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x3F), "BRO 报文接收超时");
        m[0x40] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x40), "功率控制模块与充电模块通信超时");
        m[0x41] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x41), "功率控制器间通信超时（并机扩容场景）");
        m[0x42] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x42), "BRO充电准备就绪报文超时");
        m[0x43] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x43), "BMS通信异常");
        m[0x0102] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x0102), "平台通信故障");
        m[0xFF] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0xFF), "充电机其他故障");
        m[0x0101] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x0101), "电表通信故障");
        m[0x0104] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0x0104), "主控通信故障");
        m[0xF001] = MakeRule(true, FaultTypeDef::START_FAIL, makeReason(FaultTypeDef::START_FAIL, 0xF001), "启动超时");
        return m;
    }

    std::map<unsigned int, FaultPointRule> BuildChargingRuleMap()
    {
        std::map<unsigned int, FaultPointRule> m;
        m[0x01] = MakeRule(false, FaultTypeDef::CHARGING_FAIL, 0, "计费控制单元正常停止");
        m[0x02] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x02), "计费控制单元自身故障终止");
        m[0x03] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x03), "计费控制单元判断充电控制模块故障停止");
        m[0x04] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x04), "启动完成状态帧确认超时故障终止");
        m[0x05] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x05), "充电中控制导引故障");
        m[0x06] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x06), "充电控制模块与计费控制单元通讯超时");
        m[0x07] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x07), "充电桩充电中暂停超时");
        m[0x08] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x08), "急停按钮动作故障");
        m[0x09] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x09), "门禁故障");
        m[0x0A] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x0A), "避雷器故障");
        m[0x0B] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x0B), "烟雾故障");
        m[0x0C] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x0C), "交流输入断路器故障");
        m[0x0D] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x0D), "交流输入接触器拒动/误动故障");
        m[0x0E] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x0E), "交流输入接触器粘连故障");
        m[0x0F] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x0F), "交流输入故障（过压，欠压，缺相，过流）");
        m[0x10] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x10), "充电桩过温故障");
        m[0x11] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x11), "充电接口过温故障");
        m[0x12] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x12), "充电接口电子锁故障");
        m[0x13] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x13), "直流输出接触器拒动/误动故障");
        m[0x14] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x14), "直流输出接触器粘连故障");
        m[0x15] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x15), "直流输出熔断器故障");
        m[0x16] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x16), "并联接触器拒动/误动故障");
        m[0x17] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x17), "并联接触器粘连故障");
        m[0x18] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x18), "泄放回路故障");
        m[0x19] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x19), "辅助电源故障");
        m[0x1A] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x1A), "充电机模块故障");
        m[0x1B] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x1B), "直流输出电压过压故障");
        m[0x1C] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x1C), "直流输出电压欠压故障");
        m[0x1D] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x1D), "直流输出电流过流故障");
        m[0x1E] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x1E), "直流输出短路故障");
        m[0x1F] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x1F), "电池充电需求报文（BCL）超时");
        m[0x20] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x20), "电池充电总状态报文（BCS）超时");
        m[0x21] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x21), "动力蓄电池状态信息（BSM）超时");
        m[0x22] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x22), "BSM 报文中单体动力蓄电池电压过压");
        m[0x23] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x23), "BSM 报文中单体动力蓄电池电压过低");
        m[0x24] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x24), "BSM 报文中SOC 过高");
        m[0x25] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x25), "BSM 报文中SOC 过低");
        m[0x26] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x26), "BSM 报文中充电过电流");
        m[0x27] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x27), "BSM 报文中动力蓄电池温度过高");
        m[0x28] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x28), "BSM 报文中动力蓄电池绝缘状态异常");
        m[0x29] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x29), "BSM 报文中连接器连接状态异常");
        m[0x2A] = MakeRule(false, FaultTypeDef::CHARGING_FAIL, 0, "BMS 正常终止充电");
        m[0x2B] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x2B), "BMS 异常终止充电");
        m[0x2C] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x2C), "充电桩判断BMS 其他异常终止");
        m[0x2D] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x2D), "预留");
        m[0x2E] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x2E), "低压辅助电源模块输出故障");
        m[0x2F] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x2F), "PE断线故障");
        m[0x30] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x30), "控制导引电压异常");
        m[0x31] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x31), "充电机柜过温故障（充电机柜，仅直流桩使用）");
        m[0x32] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x32), "充电桩执行功率分配策略失败故障");
        m[0x33] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x33), "无空闲模块可用（具备分配功率机型）");
        m[0x34] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x34), "预充阶段调压失败");
        m[0x35] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x35), "BST停止未知故障");
        m[0x36] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x36), "输出电压大于车辆最高允许充电电压");
        m[0x37] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x37), "需求电流超过车辆最高允许充电电流");
        m[0x38] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x38), "需求电压超过车辆最高允许充电电压");
        m[0x39] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x39), "充电控制模块与功率控制模块通信超时");
        m[0x3A] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x3A), "功率控制模块与开关模块通信超时");
        m[0x3B] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x3B), "功率控制模块与充电模块通信超时");
        m[0x3C] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x3C), "功率控制器间通信超时（并机扩容场景）");
        m[0x3D] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x3D), "BMS通讯异常");
        m[0x0102] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x0102), "平台通信故障");
        m[0xFF] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0xFF), "充电桩其他故障");
        m[0x0101] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x0101), "电表通信故障");
        m[0x0104] = MakeRule(true, FaultTypeDef::CHARGING_FAIL, makeReason(FaultTypeDef::CHARGING_FAIL, 0x0104), "主控通信故障");
        return m;
    }

    const std::map<unsigned int, FaultPointRule> kStandbyRuleMap = BuildStandbyRuleMap();
    const std::map<unsigned int, FaultPointRule> kStartRuleMap = BuildStartRuleMap();
    const std::map<unsigned int, FaultPointRule> kChargingRuleMap = BuildChargingRuleMap();

    FaultJudgeResult judgeByMap(const std::string& pointKey, const std::map<unsigned int, FaultPointRule>& table,
                               const std::string& prefix, int defaultFaultType)
    {
        FaultJudgeResult result;
        result.pointKey = pointKey;
        std::string actualPrefix;
        unsigned int code = 0;
        if (!parsePointKey(pointKey, actualPrefix, code) || actualPrefix != prefix) {
            return result;
        }
        std::map<unsigned int, FaultPointRule>::const_iterator it = table.find(code);
        if (it != table.end()) {
            result.valid = true;
            result.treatAsFault = it->second.treatAsFault;
            result.faultType = it->second.faultType;
            result.reason = it->second.reason;
            result.message = it->second.message;
            return result;
        }
        result.valid = true;
        result.treatAsFault = true;
        result.faultType = defaultFaultType;
        result.reason = makeReason(defaultFaultType, code);
        char buf[128] = {0};
        if (defaultFaultType == FaultTypeDef::START_FAIL) {
            std::snprintf(buf, sizeof(buf), "启动失败(0x%04X)", code);
        } else if (defaultFaultType == FaultTypeDef::CHARGING_FAIL) {
            std::snprintf(buf, sizeof(buf), "充电中故障(0x%04X)", code);
        } else {
            std::snprintf(buf, sizeof(buf), "常态故障(0x%04X)", code);
        }
        result.message = buf;
        return result;
    }
}

FaultPointRule::FaultPointRule()
    : treatAsFault(false), faultType(FaultTypeDef::NONE), reason(0)
{
}

FaultPointRule::FaultPointRule(bool treatAsFault_, int faultType_, unsigned int reason_, const std::string& message_)
    : treatAsFault(treatAsFault_), faultType(faultType_), reason(reason_), message(message_)
{
}

FaultJudgeResult::FaultJudgeResult()
    : valid(false), treatAsFault(false), faultType(FaultTypeDef::NONE), reason(0)
{
}

std::string MakeStandbyPointKey(unsigned int code) { return makePointKey("standby", code); }
std::string MakeStartPointKey(unsigned int code) { return makePointKey("start", code); }
std::string MakeChargingPointKey(unsigned int code) { return makePointKey("charging", code); }

FaultJudgeResult JudgeStandbyFaultPoint(const std::string& pointKey)
{
    return judgeByMap(pointKey, kStandbyRuleMap, "standby", FaultTypeDef::STANDBY);
}

FaultJudgeResult JudgeStartFailPoint(const std::string& pointKey)
{
    return judgeByMap(pointKey, kStartRuleMap, "start", FaultTypeDef::START_FAIL);
}

FaultJudgeResult JudgeChargingFailPoint(const std::string& pointKey)
{
    return judgeByMap(pointKey, kChargingRuleMap, "charging", FaultTypeDef::CHARGING_FAIL);
}

FaultJudgeResult JudgeFaultPoint(const std::string& pointKey)
{
    if (pointKey.find("standby_") == 0) return JudgeStandbyFaultPoint(pointKey);
    if (pointKey.find("start_") == 0) return JudgeStartFailPoint(pointKey);
    if (pointKey.find("charging_") == 0) return JudgeChargingFailPoint(pointKey);
    return FaultJudgeResult();
}

const char* QueryReasonText(unsigned int reason)
{
    static std::string text;
    if (reason == 0) {
        return "正常停止";
    }
    FaultJudgeResult result;
    if ((reason & 0xF0000U) == REASON_STAGE_START) {
        result = JudgeStartFailPoint(MakeStartPointKey(reason & 0xFFFFU));
    } else if ((reason & 0xF0000U) == REASON_STAGE_CHARGING) {
        result = JudgeChargingFailPoint(MakeChargingPointKey(reason & 0xFFFFU));
    } else if ((reason & 0xF0000U) == REASON_STAGE_STANDBY) {
        result = JudgeStandbyFaultPoint(MakeStandbyPointKey(reason & 0xFFFFU));
    }
    text = result.message;
    return text.c_str();
}

bool IsFaultReason(unsigned int reason)
{
    return reason != 0;
}
