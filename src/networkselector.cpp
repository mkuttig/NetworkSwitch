#include "networkselector.h"

/*
 *
 */
NetworkSelector::NetworkSelector()
{

    // Mqtt
    QString host     = "localhost";
    quint16 port     = 1883;
    QString clientId = "NetworkSwitch";

    m_mqttClient = new MqttClient(this);
    connect(m_mqttClient, &MqttClient::connected, this, &NetworkSelector::onMqttConnected);
    connect(m_mqttClient, &MqttClient::error,     this, &NetworkSelector::onMqttError);
    m_mqttClient->connectToHost(host, port, clientId);

}

NetworkSelector::~NetworkSelector()
{
    delete m_mqttClient;
}

void NetworkSelector::onMqttConnected()
{
    qDebug() << "ERFOLGREICH VERBUNDEN!";
    if (m_mqttClient->isConnected())
    {
        m_mqttClient->subscribe("message/new", [this](const QByteArray &msg) {
            std::cout << QString(msg) << std::endl;
        });
        m_mqttClient->subscribe("message/err", [this](const QByteArray &msg) {
            std::cout << QString(msg) << std::endl;
        });
    }
}

void NetworkSelector::onMqttError(const QString &error)
{
    qDebug() << "MQTT Fehler: " << error;
}

/*
 *
 */
bool NetworkSelector::switchToUnsecure(int timeoutMs)
{
    // Externe Implementierung
    return // gibt zurück ob das Umschalten erfolgreich war!
}

/*
 *
 */
bool NetworkSelector::switchToSecure(int timeoutMs)
{
    // Externe Implementierung
    return // gibt zurück ob das Umschalten erfolgreich war!
}