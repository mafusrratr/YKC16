// BY ZF
#include "virtual_plug_backend.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace {

long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string trim(const std::string& value)
{
    const char* spaces = " \t\r\n";
    const size_t begin = value.find_first_not_of(spaces);
    if (begin == std::string::npos) {
        return "";
    }
    const size_t end = value.find_last_not_of(spaces);
    return value.substr(begin, end - begin + 1);
}

int toInt(const std::string& value, int fallback)
{
    if (value.empty()) {
        return fallback;
    }
    return std::atoi(value.c_str());
}

double clampDouble(double value, double minValue, double maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

double round3(double value)
{
    return std::floor(value * 1000.0 + 0.5) / 1000.0;
}

bool containsText(const std::string& payload, const std::string& needle)
{
    return payload.find(needle) != std::string::npos;
}

std::string doubleText(double value)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << value;
    return out.str();
}

} // namespace

VirtualPlugBackend::VirtualPlugBackend()
    : m_running(false),
      m_mqttConnected(false),
      m_seq(1),
      m_httpFd(-1)
{
    m_config.mqttHost = "192.168.10.126";
    m_config.mqttPort = 1883;
    m_config.mqttKeepalive = 60;
    m_config.mqttClientId = "virtual_plug";
    m_config.topicPrefix = "tcu";
    m_config.httpHost = "127.0.0.1";
    m_config.httpPort = 18080;
    m_config.biasNo = 0;
    m_config.gunCount = 2;
}

VirtualPlugBackend::~VirtualPlugBackend()
{
    stop();
}

bool VirtualPlugBackend::loadConfig(const std::string& path)
{
    std::ifstream in(path.c_str());
    if (!in.is_open()) {
        std::cerr << "config open failed: " << path << std::endl;
        return false;
    }

    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }
        const size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        kv[trim(line.substr(0, pos))] = trim(line.substr(pos + 1));
    }

    m_config.mqttHost = kv.count("mqtt_host") ? kv["mqtt_host"] : m_config.mqttHost;
    m_config.mqttPort = toInt(kv["mqtt_port"], m_config.mqttPort);
    m_config.mqttKeepalive = toInt(kv["mqtt_keepalive"], m_config.mqttKeepalive);
    m_config.mqttClientId = kv.count("mqtt_client_id") ? kv["mqtt_client_id"] : m_config.mqttClientId;
    m_config.topicPrefix = kv.count("mqtt_topic_prefix") ? kv["mqtt_topic_prefix"] : m_config.topicPrefix;
    m_config.httpHost = kv.count("http_host") ? kv["http_host"] : m_config.httpHost;
    m_config.httpPort = toInt(kv["http_port"], m_config.httpPort);
    m_config.biasNo = toInt(kv["bias_no"], m_config.biasNo);
    m_config.gunCount = toInt(kv["gun_count"], m_config.gunCount);

    m_guns.clear();
    for (int i = 0; i < m_config.gunCount; ++i) {
        GunState gun;
        gun.localGun = i;
        gun.connected = false;
        gun.running = false;
        gun.fault = false;
        gun.meterOnline = true;
        gun.logicState = "UNKNOWN";
        gun.pileState = "IDLE";
        gun.mode = "charge";
        gun.vin = "LJ21BABB8L1001955";
        gun.voltage = 750.0;
        gun.current = 80.0;
        gun.soc = 45.0;
        gun.targetSoc = 80.0;
        gun.dischargeTargetSoc = 20.0;
        gun.socRatePerMinute = 1.0;
        gun.totalEnergy = (i == 0) ? 12.345 : 8.5;
        gun.reverseEnergy = 0.0;
        gun.chargeMode = 1;
        m_guns.push_back(gun);
    }
    return true;
}

bool VirtualPlugBackend::start()
{
    if (m_running) {
        return true;
    }
    m_running = true;
    if (!startMqtt()) {
        m_running = false;
        return false;
    }
    if (!startHttp()) {
        m_running = false;
        return false;
    }
    m_meterThread = std::thread(&VirtualPlugBackend::meterLoop, this);
    return true;
}

void VirtualPlugBackend::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    if (m_httpFd >= 0) {
        close(m_httpFd);
        m_httpFd = -1;
    }
    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
    if (m_httpThread.joinable()) {
        m_httpThread.join();
    }
    if (m_meterThread.joinable()) {
        m_meterThread.join();
    }
}

bool VirtualPlugBackend::startMqtt()
{
    if (!m_mqtt.init(m_config.mqttClientId, true)) {
        std::cerr << "mqtt init failed" << std::endl;
        return false;
    }
    m_mqtt.setConnectHandler([this](int rc) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mqttConnected = (rc == 0);
        if (m_mqttConnected) {
            m_mqtt.subscribe(m_config.topicPrefix + "/pile/+/cmd", 0);
            m_mqtt.subscribe(m_config.topicPrefix + "/logic/+/event", 0);
        }
    });
    m_mqtt.setMessageHandler([this](const std::string& topic, const std::string& payload) {
        handleMqttMessage(topic, payload);
    });
    if (!m_mqtt.connect(m_config.mqttHost, m_config.mqttPort, m_config.mqttKeepalive)) {
        std::cerr << "mqtt connect failed: " << m_config.mqttHost << ":" << m_config.mqttPort << std::endl;
        return false;
    }
    return m_mqtt.loopStart();
}

bool VirtualPlugBackend::startHttp()
{
    m_httpFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_httpFd < 0) {
        std::cerr << "http socket failed" << std::endl;
        return false;
    }

    int yes = 1;
    setsockopt(m_httpFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(m_config.httpPort));
    addr.sin_addr.s_addr = inet_addr(m_config.httpHost.c_str());
    if (bind(m_httpFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "http bind failed: " << m_config.httpHost << ":" << m_config.httpPort << std::endl;
        close(m_httpFd);
        m_httpFd = -1;
        return false;
    }
    if (listen(m_httpFd, 16) != 0) {
        std::cerr << "http listen failed" << std::endl;
        close(m_httpFd);
        m_httpFd = -1;
        return false;
    }
    m_httpThread = std::thread(&VirtualPlugBackend::httpLoop, this);
    return true;
}

void VirtualPlugBackend::httpLoop()
{
    while (m_running) {
        const int client = accept(m_httpFd, nullptr, nullptr);
        if (client < 0) {
            continue;
        }

        char buffer[4096];
        const ssize_t len = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (len <= 0) {
            close(client);
            continue;
        }
        buffer[len] = '\0';
        std::string req(buffer);
        const size_t firstSpace = req.find(' ');
        const size_t secondSpace = req.find(' ', firstSpace + 1);
        std::string target = "/";
        if (firstSpace != std::string::npos && secondSpace != std::string::npos) {
            target = req.substr(firstSpace + 1, secondSpace - firstSpace - 1);
        }
        const size_t qpos = target.find('?');
        const std::string path = qpos == std::string::npos ? target : target.substr(0, qpos);
        const std::string query = qpos == std::string::npos ? "" : target.substr(qpos + 1);

        std::string body;
        int status = 200;
        if (path == "/api/status") {
            body = statusJson();
        } else if (path == "/api/action") {
            const int gun = toInt(queryValue(query, "gun"), 0);
            const std::string action = queryValue(query, "action");
            const bool ok = handleAction(gun, action);
            body = std::string("{\"ok\":") + (ok ? "true" : "false") + "}";
            status = ok ? 200 : 400;
        } else {
            body = "{\"ok\":false,\"error\":\"not found\"}";
            status = 404;
        }

        std::ostringstream out;
        out << "HTTP/1.1 " << status << (status == 200 ? " OK" : " Error") << "\r\n";
        out << "Access-Control-Allow-Origin: *\r\n";
        out << "Content-Type: application/json\r\n";
        out << "Content-Length: " << body.size() << "\r\n";
        out << "Connection: close\r\n\r\n";
        out << body;
        const std::string resp = out.str();
        send(client, resp.data(), resp.size(), 0);
        close(client);
    }
}

void VirtualPlugBackend::meterLoop()
{
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < m_guns.size(); ++i) {
            tickMeter(m_guns[i], 1);
        }
    }
}

bool VirtualPlugBackend::handleAction(int localGun, const std::string& action)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    GunState* gun = findGunByLocal(localGun);
    if (!gun) {
        return false;
    }

    if (action == "plug") {
        gun->connected = true;
        gun->pileState = "PLUGGED";
        publishPileData(*gun, "yx", yxJson(*gun));
        return true;
    }
    if (action == "unplug") {
        gun->connected = false;
        gun->running = false;
        gun->pileState = "IDLE";
        publishPileData(*gun, "yx", yxJson(*gun));
        return true;
    }
    if (action == "startResponse") {
        publishPileEvent(*gun, "start_response", "{\"confirmFlag\":0,\"startFailReason\":0,\"loadControlSwitch\":1,\"plugAndChargeFlag\":1,\"auxPowerVoltage\":12}");
        return true;
    }
    if (action == "startComplete") {
        gun->running = true;
        gun->pileState = "OUTPUT";
        publishPileEvent(*gun, "start_complete", startCompleteJson(*gun, true));
        publishPileData(*gun, "yx", yxJson(*gun));
        publishPileData(*gun, "yc", ycJson(*gun));
        publishMeterData(*gun);
        return true;
    }
    if (action == "startFail") {
        gun->running = false;
        gun->pileState = "START_FAIL";
        publishPileEvent(*gun, "start_complete", startCompleteJson(*gun, false));
        return true;
    }
    if (action == "vin") {
        publishPileEvent(*gun, "vehicle_id", vehicleIdJson(*gun));
        return true;
    }
    if (action == "authAck") {
        publishPileEvent(*gun, "vehicle_auth_ack", "{\"successFlag\":0,\"failReason\":0}");
        return true;
    }
    if (action == "stopComplete") {
        gun->running = false;
        gun->pileState = "STOPPED";
        publishPileEvent(*gun, "stop_complete", stopCompleteJson(*gun));
        publishPileData(*gun, "yx", yxJson(*gun));
        publishMeterData(*gun);
        return true;
    }
    if (action == "fault") {
        gun->fault = true;
        gun->pileState = "FAULT";
        publishPileData(*gun, "yx", yxJson(*gun));
        publishPileEvent(*gun, "deviceErr_on", "{\"totalFault\":1,\"otherFault\":1,\"faults\":[\"emergencyStopFault\"]}");
        return true;
    }
    if (action == "recover") {
        gun->fault = false;
        gun->pileState = gun->connected ? "PLUGGED" : "IDLE";
        publishPileData(*gun, "yx", yxJson(*gun));
        publishPileEvent(*gun, "deviceErr_off", "{\"totalFault\":0,\"otherFault\":0}");
        return true;
    }
    if (action == "tick") {
        tickMeter(*gun, 1);
        return true;
    }
    if (action == "meterOnline") {
        gun->meterOnline = true;
        publishMeterEvent(*gun, "meter_online", "manual_recover");
        return true;
    }
    if (action == "meterOffline") {
        gun->meterOnline = false;
        publishMeterEvent(*gun, "meter_offline", "manual_inject");
        return true;
    }
    return false;
}

std::string VirtualPlugBackend::statusJson()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream out;
    out << "{\"ok\":true,\"mqttConnected\":" << (m_mqttConnected ? "true" : "false")
        << ",\"mqttHost\":\"" << jsonEscape(m_config.mqttHost) << "\""
        << ",\"mqttPort\":" << m_config.mqttPort
        << ",\"topicPrefix\":\"" << jsonEscape(m_config.topicPrefix) << "\""
        << ",\"seq\":" << m_seq
        << ",\"guns\":[";
    for (size_t i = 0; i < m_guns.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        const GunState& gun = m_guns[i];
        out << "{\"localGun\":" << gun.localGun
            << ",\"globalGun\":" << globalGunNo(gun)
            << ",\"connected\":" << (gun.connected ? "true" : "false")
            << ",\"running\":" << (gun.running ? "true" : "false")
            << ",\"fault\":" << (gun.fault ? "true" : "false")
            << ",\"pileState\":\"" << jsonEscape(gun.pileState) << "\""
            << ",\"logicState\":\"" << jsonEscape(gun.logicState) << "\""
            << ",\"soc\":" << doubleText(gun.soc)
            << "}";
    }
    out << "]}";
    return out.str();
}

void VirtualPlugBackend::handleMqttMessage(const std::string& topic, const std::string& payload)
{
    const std::string pilePrefix = m_config.topicPrefix + "/pile/";
    const std::string logicPrefix = m_config.topicPrefix + "/logic/";
    std::lock_guard<std::mutex> lock(m_mutex);

    if (topic.find(pilePrefix) == 0 && topic.rfind("/cmd") == topic.size() - 4) {
        const std::string mid = topic.substr(pilePrefix.size(), topic.size() - pilePrefix.size() - 4);
        updateFromCmd(std::atoi(mid.c_str()), payload);
        return;
    }
    if (topic.find(logicPrefix) == 0 && topic.rfind("/event") == topic.size() - 6) {
        const std::string mid = topic.substr(logicPrefix.size(), topic.size() - logicPrefix.size() - 6);
        updateFromLogicEvent(std::atoi(mid.c_str()), payload);
    }
}

void VirtualPlugBackend::updateFromCmd(int globalGun, const std::string& payload)
{
    GunState* gun = findGunByGlobal(globalGun);
    if (!gun) {
        return;
    }
    if (containsText(payload, "\"start_charge\"")) {
        gun->pileState = "CMD_START";
        gun->mode = containsText(payload, "\"v2g\":1") ? "discharge" : "charge";
    } else if (containsText(payload, "\"stop_charge\"")) {
        gun->pileState = "CMD_STOP";
    } else if (containsText(payload, "\"clear_fault\"")) {
        gun->fault = false;
        gun->pileState = gun->connected ? "PLUGGED" : "IDLE";
    }
}

void VirtualPlugBackend::updateFromLogicEvent(int globalGun, const std::string& payload)
{
    GunState* gun = findGunByGlobal(globalGun);
    if (!gun) {
        return;
    }
    if (containsText(payload, "CHARGING")) {
        gun->logicState = "CHARGING";
    } else if (containsText(payload, "STOPPING")) {
        gun->logicState = "STOPPING";
    } else if (containsText(payload, "STOPPED")) {
        gun->logicState = "STOPPED";
    } else if (containsText(payload, "STARTING")) {
        gun->logicState = "STARTING";
    } else if (containsText(payload, "PREPARE")) {
        gun->logicState = "PREPARE";
    } else if (containsText(payload, "ERROR")) {
        gun->logicState = "ERROR";
    } else if (containsText(payload, "IDLE")) {
        gun->logicState = "IDLE";
    }
}

int VirtualPlugBackend::globalGunNo(const GunState& gun) const
{
    return gun.localGun + m_config.biasNo;
}

GunState* VirtualPlugBackend::findGunByLocal(int localGun)
{
    for (size_t i = 0; i < m_guns.size(); ++i) {
        if (m_guns[i].localGun == localGun) {
            return &m_guns[i];
        }
    }
    return nullptr;
}

GunState* VirtualPlugBackend::findGunByGlobal(int globalGun)
{
    for (size_t i = 0; i < m_guns.size(); ++i) {
        if (globalGunNo(m_guns[i]) == globalGun) {
            return &m_guns[i];
        }
    }
    return nullptr;
}

void VirtualPlugBackend::publishPileData(const GunState& gun, const std::string& type, const std::string& dataJson)
{
    const std::string topic = m_config.topicPrefix + "/pile/" + std::to_string(globalGunNo(gun)) + "/data";
    m_mqtt.publish(topic, rootPayload(gun, "pile_controller", "\"type\":\"" + type + "\",\"data\":" + dataJson));
}

void VirtualPlugBackend::publishPileEvent(const GunState& gun, const std::string& type, const std::string& dataJson)
{
    const std::string topic = m_config.topicPrefix + "/pile/" + std::to_string(globalGunNo(gun)) + "/event";
    m_mqtt.publish(topic, rootPayload(gun, "pile_controller", "\"type\":\"" + type + "\",\"data\":" + dataJson));
}

void VirtualPlugBackend::publishMeterData(const GunState& gun)
{
    const std::string topic = m_config.topicPrefix + "/meter/" + std::to_string(globalGunNo(gun)) + "/data";
    std::ostringstream data;
    data << "{\"totalEnergy\":" << doubleText(round3(gun.totalEnergy))
         << ",\"ReverseEnergy\":" << doubleText(round3(gun.reverseEnergy))
         << ",\"voltage\":" << doubleText(gun.voltage)
         << ",\"current\":" << doubleText(gun.current) << "}";
    m_mqtt.publish(topic, rootPayload(gun, "tcu_meter", "\"data\":" + data.str()));
}

void VirtualPlugBackend::publishMeterEvent(const GunState& gun, const std::string& event, const std::string& reason)
{
    const std::string topic = m_config.topicPrefix + "/meter/" + std::to_string(globalGunNo(gun)) + "/event";
    const std::string extra = "\"event\":\"" + event + "\",\"data\":{\"reason\":\"" + jsonEscape(reason) + "\"}";
    m_mqtt.publish(topic, rootPayload(gun, "tcu_meter", extra));
}

void VirtualPlugBackend::tickMeter(GunState& gun, int intervalSec)
{
    if (!gun.running || !gun.meterOnline) {
        return;
    }
    const double powerKwValue = gun.voltage * gun.current / 1000.0;
    const double deltaKwh = powerKwValue * intervalSec / 3600.0;
    const double deltaSoc = gun.socRatePerMinute * intervalSec / 60.0;
    if (gun.mode == "discharge") {
        gun.reverseEnergy = round3(gun.reverseEnergy + deltaKwh);
        gun.soc = clampDouble(gun.soc - deltaSoc, 0.0, 100.0);
    } else {
        gun.totalEnergy = round3(gun.totalEnergy + deltaKwh);
        gun.soc = clampDouble(gun.soc + deltaSoc, 0.0, 100.0);
    }
    publishPileData(gun, "yc", ycJson(gun));
    publishMeterData(gun);
}

std::string VirtualPlugBackend::yxJson(const GunState& gun) const
{
    std::ostringstream out;
    out << "{\"workStatus\":" << (gun.running ? 1 : 0)
        << ",\"totalFault\":" << (gun.fault ? 1 : 0)
        << ",\"totalAlarm\":0"
        << ",\"emergencyStopFault\":" << (gun.fault ? 1 : 0)
        << ",\"vehicleConnectStatus\":" << (gun.connected ? 1 : 0)
        << ",\"vinReq\":0"
        << ",\"gunSeatStatus\":" << (gun.connected ? 1 : 0)
        << ",\"electronicLockStatus\":" << (gun.connected ? 1 : 0)
        << ",\"dcContactorStatus\":" << (gun.running ? 1 : 0)
        << ",\"otherFault\":" << (gun.fault ? 1 : 0) << "}";
    return out.str();
}

std::string VirtualPlugBackend::ycJson(const GunState& gun) const
{
    const double remainRaw = (gun.mode == "discharge")
        ? (gun.soc - gun.dischargeTargetSoc) / gun.socRatePerMinute
        : (gun.targetSoc - gun.soc) / gun.socRatePerMinute;
    const int remain = static_cast<int>(std::max(0.0, std::floor(remainRaw + 0.5)));
    std::ostringstream out;
    out << "{\"outputVoltage\":" << doubleText(gun.voltage)
        << ",\"outputCurrent\":" << doubleText(gun.current)
        << ",\"soc\":" << static_cast<int>(std::floor(gun.soc + 0.5))
        << ",\"batteryMinTemp\":25,\"batteryMaxTemp\":32"
        << ",\"cellMaxVoltage\":3.65,\"cellMinVoltage\":3.55"
        << ",\"pileEnvTemp\":28,\"guideVoltage\":12"
        << ",\"bmsReqVoltage\":" << doubleText(gun.voltage)
        << ",\"bmsReqCurrent\":" << doubleText(gun.current)
        << ",\"chargeMode\":" << gun.chargeMode
        << ",\"bmsMeasuredVoltage\":" << doubleText(gun.voltage - 1.5)
        << ",\"bmsMeasuredCurrent\":" << doubleText(std::max(0.0, gun.current - 0.5))
        << ",\"estimatedRemainTime\":" << remain
        << ",\"interfaceTemp1\":28,\"interfaceTemp2\":28,\"interfaceTemp3\":28,\"interfaceTemp4\":28"
        << ",\"maxVoltageCellNo\":1,\"maxTempPointNo\":1,\"minTempPointNo\":1"
        << ",\"inletTemp\":25,\"outletTemp\":28,\"envHumidity\":50}";
    return out.str();
}

std::string VirtualPlugBackend::startCompleteJson(const GunState& gun, bool ok) const
{
    std::ostringstream out;
    out << "{\"successFlag\":" << (ok ? 0 : 1)
        << ",\"chargeFailReason\":" << (ok ? 0 : 14)
        << ",\"pileBmsVersion\":[1,0,0],\"bmsPileVersion\":[1,0,0]"
        << ",\"handshakeResult\":" << (ok ? 0 : 1)
        << ",\"batteryType\":1,\"maxAllowTemp\":60"
        << ",\"bmsMaxChargeVoltage\":" << doubleText(gun.voltage)
        << ",\"cellMaxChargeVoltage\":4200,\"maxAllowChargeCurrent\":250"
        << ",\"ratedTotalVoltage\":750,\"currentTotalVoltage\":" << doubleText(gun.voltage - 10.0)
        << ",\"ratedCapacity\":200,\"nominalEnergy\":150"
        << ",\"soc\":" << static_cast<int>(std::floor(gun.soc + 0.5))
        << ",\"pileMaxOutputVoltage\":1000,\"pileMinOutputVoltage\":200"
        << ",\"pileMaxOutputCurrent\":250,\"pileMinOutputCurrent\":0"
        << ",\"vin\":\"" << jsonEscape(gun.vin) << "\""
        << ",\"batteryManufacturer\":\"TEST\",\"batterySerial\":[0,0,0,1]"
        << ",\"batteryProdYear\":2026,\"batteryProdMonth\":5,\"batteryProdDay\":15"
        << ",\"batteryChargeCount\":[0,1,2],\"batteryPropertyFlag\":0"
        << ",\"bmsSoftwareVersion\":[1,0,0,0,0,0,0,0]}";
    return out.str();
}

std::string VirtualPlugBackend::stopCompleteJson(const GunState& gun) const
{
    std::ostringstream out;
    out << "{\"stopReason\":1,\"stopSuccessFlag\":0,\"bmsStopReason\":0"
        << ",\"bmsChargeFaultReason\":0,\"bmsStopErrorReason\":0"
        << ",\"stopSoc\":" << static_cast<int>(std::floor(gun.soc + 0.5))
        << ",\"cellMinVoltage\":3.55,\"cellMaxVoltage\":3.65"
        << ",\"batteryMinTemp\":25,\"batteryMaxTemp\":32,\"faults\":[]}";
    return out.str();
}

std::string VirtualPlugBackend::vehicleIdJson(const GunState& gun) const
{
    std::ostringstream out;
    out << "{\"vin\":\"" << jsonEscape(gun.vin) << "\""
        << ",\"batteryChargeCount\":[0,1,2]"
        << ",\"soc\":" << static_cast<int>(std::floor(gun.soc + 0.5))
        << ",\"currentBatteryVoltage\":" << doubleText(gun.voltage - 10.0) << "}";
    return out.str();
}

std::string VirtualPlugBackend::rootPayload(const GunState& gun, const std::string& source, const std::string& extraJson)
{
    std::ostringstream out;
    out << "{\"ts\":" << nowMs()
        << ",\"seq\":" << m_seq++
        << ",\"source\":\"" << source << "\""
        << ",\"gun\":" << globalGunNo(gun)
        << "," << extraJson << "}";
    return out.str();
}

std::string VirtualPlugBackend::jsonEscape(const std::string& value)
{
    std::ostringstream out;
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '"' || c == '\\') {
            out << '\\' << c;
        } else if (c == '\n') {
            out << "\\n";
        } else if (c == '\r') {
            out << "\\r";
        } else if (c == '\t') {
            out << "\\t";
        } else {
            out << c;
        }
    }
    return out.str();
}

std::string VirtualPlugBackend::queryValue(const std::string& query, const std::string& key)
{
    size_t start = 0;
    while (start <= query.size()) {
        const size_t end = query.find('&', start);
        const std::string part = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == key) {
            return urlDecode(part.substr(eq + 1));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return "";
}

std::string VirtualPlugBackend::urlDecode(const std::string& value)
{
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
            i += 2;
        } else if (value[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}
