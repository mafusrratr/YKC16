/**
 * MQTT 客户端封装（基于 libmosquitto）
 * BY ZF
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <string>
#include <functional>

struct mosquitto;
struct mosquitto_message;

class MqttClient {
public:
    typedef std::function<void(const std::string& topic, const std::string& payload)> MessageHandler;
    typedef std::function<void(int rc)> ConnectHandler;

    MqttClient();
    ~MqttClient();

    bool init(const std::string& clientId, bool cleanSession = true);
    bool connect(const std::string& host, int port = 1883, int keepalive = 60);
    void disconnect();

    bool subscribe(const std::string& topic, int qos = 0);
    bool publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false);

    bool loopStart();
    void loopStop(bool force = false);

    void setMessageHandler(const MessageHandler& handler);
    void setConnectHandler(const ConnectHandler& handler);
    bool setWill(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false);
    bool setUsernamePassword(const std::string& username, const std::string& password);

private:
    static void onMessage(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message);
    static void onConnect(struct mosquitto* mosq, void* userdata, int rc);

private:
    struct mosquitto* m_mosq;
    MessageHandler m_onMessage;
    ConnectHandler m_onConnect;
};

#endif // MQTT_CLIENT_H
