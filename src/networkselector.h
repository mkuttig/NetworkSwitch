#ifndef NETWORKSELECTOR_H
#define NETWORKSELECTOR_H

#include "mqttclient.h"

#include <QObject>


class NetworkSelector : public QObject
{
    Q_OBJECT
    MqttClient* m_mqttClient = nullptr;

    void onMqttConnected();
    void onMqttError(const QString &error);

public:
    explicit NetworkSelector();
    virtual ~NetworkSelector();

    // Funktionen zum umschalten des Umschalters
    bool switchToSecure(int timeoutMs = 10000);
    bool switchToUnsecure(int timeoutMs = 10000);

};

#endif // NETWORKSELECTOR_H
