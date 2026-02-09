/**
 * MQTT 客户端封装实现（libmosquitto）
 * BY ZF
 */

#include "mqtt_client.h"
#include "include/mosquitto/libmosquitto.h"
#include <mutex>

namespace {
    std::mutex g_mosqMutex;
    int g_mosqRefCount = 0;
}

MqttClient::MqttClient()
    : m_mosq(nullptr)
{
}

MqttClient::~MqttClient()
{
    disconnect();
    if (m_mosq) {
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_mosqMutex);
    if (g_mosqRefCount > 0) {
        g_mosqRefCount--;
        if (g_mosqRefCount == 0) {
            mosquitto_lib_cleanup();
        }
    }
}

bool MqttClient::init(const std::string& clientId, bool cleanSession)
{
    std::lock_guard<std::mutex> lock(g_mosqMutex);
    if (g_mosqRefCount == 0) {
        mosquitto_lib_init();
    }
    g_mosqRefCount++;

    m_mosq = mosquitto_new(clientId.c_str(), cleanSession, this);
    if (!m_mosq) {
        return false;
    }
    mosquitto_message_callback_set(m_mosq, &MqttClient::onMessage);
    return true;
}

bool MqttClient::connect(const std::string& host, int port, int keepalive)
{
    if (!m_mosq) {
        return false;
    }
    return mosquitto_connect(m_mosq, host.c_str(), port, keepalive) == MOSQ_ERR_SUCCESS;
}

void MqttClient::disconnect()
{
    if (m_mosq) {
        mosquitto_disconnect(m_mosq);
    }
}

bool MqttClient::subscribe(const std::string& topic, int qos)
{
    if (!m_mosq) {
        return false;
    }
    return mosquitto_subscribe(m_mosq, nullptr, topic.c_str(), qos) == MOSQ_ERR_SUCCESS;
}

bool MqttClient::publish(const std::string& topic, const std::string& payload, int qos, bool retain)
{
    if (!m_mosq) {
        return false;
    }
    return mosquitto_publish(m_mosq, nullptr, topic.c_str(),
                             static_cast<int>(payload.size()),
                             payload.empty() ? nullptr : payload.data(),
                             qos, retain) == MOSQ_ERR_SUCCESS;
}

bool MqttClient::loopStart()
{
    if (!m_mosq) {
        return false;
    }
    return mosquitto_loop_start(m_mosq) == MOSQ_ERR_SUCCESS;
}

void MqttClient::loopStop(bool force)
{
    if (m_mosq) {
        mosquitto_loop_stop(m_mosq, force);
    }
}

void MqttClient::setMessageHandler(const MessageHandler& handler)
{
    m_onMessage = handler;
}

bool MqttClient::setWill(const std::string& topic, const std::string& payload, int qos, bool retain)
{
    if (!m_mosq) {
        return false;
    }
    return mosquitto_will_set(m_mosq, topic.c_str(),
                              static_cast<int>(payload.size()),
                              payload.empty() ? nullptr : payload.data(),
                              qos, retain) == MOSQ_ERR_SUCCESS;
}

bool MqttClient::setUsernamePassword(const std::string& username, const std::string& password)
{
    if (!m_mosq) {
        return false;
    }
    return mosquitto_username_pw_set(m_mosq, username.c_str(), password.c_str()) == MOSQ_ERR_SUCCESS;
}

void MqttClient::onMessage(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message)
{
    if (!userdata || !message || !message->topic) {
        return;
    }
    MqttClient* self = static_cast<MqttClient*>(userdata);
    if (!self->m_onMessage) {
        return;
    }
    std::string payload;
    if (message->payload && message->payloadlen > 0) {
        payload.assign(static_cast<const char*>(message->payload),
                       static_cast<size_t>(message->payloadlen));
    }
    self->m_onMessage(message->topic, payload);
}
