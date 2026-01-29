#ifndef NETWORKSELECTOR_H
#define NETWORKSELECTOR_H

#include "LoggingCategory4EMH.h"
#include "LoggingCategory4EMHConfigurator.h"
#include "ipcommnetworkselector.h"
#include "appsettings.h"
#include "AisAndonClient/mqttclient.h"

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QEventLoop>
#include <QMap>

struct StatusNetwork
{
    QList<QString> nwName;
    QList<bool> nwConnected;
};

class AbstractTask;

class NetworkSelector : public QObject
{
    Q_OBJECT

    LoggingCategory4EMH*            m_logger          = nullptr;
    MqttClient*                     m_mqttClient      = nullptr;
    IpcommNetworkSelector*          m_networkSelector = nullptr;
    StatusNetwork                   m_statusNetwork;
    AppSettings                     m_settings;

    QTimer                          m_automaticTimer;
    bool                            m_automaticMode     = true;
    int                             m_networkSwichtStep = 0;

    void onSwitchID(int id);
    bool switchToNetwork(IpcommNetworkSelector::NetworkRelais relais,
                         QString &errorMsg, int timeoutMs);

public:
    explicit NetworkSelector();
    virtual ~NetworkSelector();

    void start() { m_automaticTimer.start(); }
    void stop()  { m_automaticTimer.stop();  }

    // Funktionen zum umschalten des Umschalters
    bool switchToSecure(int timeoutMs = 10000);
    bool switchToUnsecure(int timeoutMs = 10000);

signals:
    void newMessage(QString);
    void errMessage(QString);

    void updateStatusNetwork(StatusNetwork);

    void setAutomaticMode(bool);

    void startTransfer(int);
    void nextTransfer();
    void endTransfer(int);

    void startWork();
    void endWork();

    void eventDeviceStatusValid();

public slots:
    void onNetworkSwitch(bool);
    void onSwitch1() { onSwitchID(1); }
    void onSwitch2() { onSwitchID(2); }
    void onSwitch3() { onSwitchID(3); }
    void onSwitch4() { onSwitchID(4); }
    void onToggleMode();

private slots:
    void onDeviceStateChanged();
    void onMqttConnected();
    void onMqttError(const QString &error);

    void onSwitchNetworkInterval();
    void onActionTask(const QString &msg);

};

#endif // NETWORKSELECTOR_H
