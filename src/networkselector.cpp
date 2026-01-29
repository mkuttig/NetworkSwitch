#include "networkselector.h"

/*
 *
 */
NetworkSelector::NetworkSelector()
{
    // Logging
    m_logger = &LoggingCategory4EMHConfigurator::instance()->getLoggingCategory("NetzwerkTrennung.logic");

    // Mqtt
    QString host     = "localhost";
    quint16 port     = 1883;
    QString clientId = "NetworkSwitch";

    m_mqttClient = new MqttClient(this);
    connect(m_mqttClient, &MqttClient::connected, this, &NetworkSelector::onMqttConnected);
    connect(m_mqttClient, &MqttClient::error,     this, &NetworkSelector::onMqttError);
    m_mqttClient->connectToHost(host, port, clientId);

    // Netzwerkbrücke Initialsieren für REST Kommunikation
    if (!m_settings.DoNotUseNetworkSwitch)
    {
        m_networkSelector = new IpcommNetworkSelector();
        m_networkSelector->setUserAndPW(m_settings.IpcommUser, m_settings.ipcommPassword());
        connect(m_networkSelector, &IpcommNetworkSelector::StateChanged,
                this, &NetworkSelector::onDeviceStateChanged);

        m_statusNetwork.nwName.push_back(m_settings.E1NetworkName);
        m_statusNetwork.nwConnected.push_back(false);
        m_statusNetwork.nwName.push_back(m_settings.E2NetworkName);
        m_statusNetwork.nwConnected.push_back(false);
        m_statusNetwork.nwName.push_back(m_settings.E3NetworkName);
        m_statusNetwork.nwConnected.push_back(false);
        m_statusNetwork.nwName.push_back(m_settings.E4NetworkName);
        m_statusNetwork.nwConnected.push_back(false);

        emit updateStatusNetwork(m_statusNetwork);

    } //else m_unsecureNetworkTimer.stop(); // Automatic switch to secure network deactivate

    m_automaticTimer.setInterval(m_settings.AndonAisRefreshPeriodMs); // 15 Minuten Intervall in settings ini
    connect(&m_automaticTimer, &QTimer::timeout, this, &NetworkSelector::onSwitchNetworkInterval);

    // Alle Trennen Timer neu Starten mit 15 Minuten
    // -- Timer(15 Minuten) --
    // TimerEvent 15 Minuten - Sicheres Netzwerk
    // Alle senden ExitAction Unsicheres Netzwerk
    // Alle senden ExitAction Sicheres Netzwerk
    // Alle senden ExitAction Timer neu Starten (15 Minuten)

    //m_networkSelector->disconnectAllNetworks();
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
            newMessage(QString(msg));
        });
        m_mqttClient->subscribe("message/err", [this](const QByteArray &msg) {
            errMessage(QString(msg));
        });
        m_mqttClient->subscribe("action/task", [this](const QByteArray &msg) {
            onActionTask(QString(msg));
        });
    }
}

void NetworkSelector::onMqttError(const QString &error)
{
    qDebug() << "MQTT Fehler: " << error;
}



void NetworkSelector::onSwitchNetworkInterval()
{
    newMessage("Timer gestopt ...");
    m_automaticTimer.stop();
    m_networkSwichtStep = 0;
    m_mqttClient->publish("network/secure","TRUE");
}

void NetworkSelector::onActionTask(const QString &msg)
{
    QString errorMsg;
    int nCount = 0;
    if (msg.contains("exitAction"))
    {
        m_networkSwichtStep++;
        switch (m_networkSwichtStep)
        {
        case 1:
            while(nCount++ != 5) if (switchToUnsecure(20000)) break;
            break;
        case 2:
            while(nCount++ != 5) if (switchToSecure(20000)) break;
            break;
        case 3:
            m_automaticTimer.setInterval(m_settings.AndonAisRefreshPeriodMs);
            m_automaticTimer.start();
            newMessage("Timer neu gestarted!");
            break;
        }
    }
}

/*
 *
 */
void NetworkSelector::onNetworkSwitch(bool active)
{
    if ((!active) || (m_networkSelector == nullptr)) return;

    QMap<IpcommNetworkSelector::NetworkRelais, IpcommNetworkSelector::RelaisState> states;
    states = m_networkSelector->newestRelaisStates();
    if ( states[IpcommNetworkSelector::NetworkRelais::E1] == IpcommNetworkSelector::RelaisState::On )
        m_statusNetwork.nwConnected[0] = true;
    else
        m_statusNetwork.nwConnected[0] = false;

    if ( states[IpcommNetworkSelector::NetworkRelais::E2] == IpcommNetworkSelector::RelaisState::On )
        m_statusNetwork.nwConnected[1] = true;
    else
        m_statusNetwork.nwConnected[1] = false;

    if ( states[IpcommNetworkSelector::NetworkRelais::E3] == IpcommNetworkSelector::RelaisState::On )
        m_statusNetwork.nwConnected[2] = true;
    else
        m_statusNetwork.nwConnected[2] = false;

    if ( states[IpcommNetworkSelector::NetworkRelais::E4] == IpcommNetworkSelector::RelaisState::On )
        m_statusNetwork.nwConnected[3] = true;
    else
        m_statusNetwork.nwConnected[3] = false;

    QString msg = "Netzwerk: ";
    for (int i = 0; i < m_statusNetwork.nwName.size();++i)
        msg += m_statusNetwork.nwName[i] + " " + (m_statusNetwork.nwConnected[i] ? "[ON] " : "[OFF] ");
    emit newMessage(msg);
    emit updateStatusNetwork(m_statusNetwork);
}


/*
 *
 */
void NetworkSelector::onToggleMode()
{
    m_automaticMode = !m_automaticMode;
    if (m_automaticMode) m_automaticTimer.start(); else m_automaticTimer.stop();
    emit setAutomaticMode(m_automaticMode);
}

/*
 *
 */
//TODO: Wenn der status nicht erreicht werden kann retry, bis netzwerk umschlater zustand hat.
//      (IpComm GeräteStatus ... Aktueller Zustand des Relais stimmt nicht mit dem Angeforderten überein)
//      VALID/FATAL
void NetworkSelector::onDeviceStateChanged()
{

    if (m_networkSelector == nullptr) return;
    onNetworkSwitch(true);

    if ( m_networkSelector->deviceStatus() != IpcommNetworkSelector::DeviceStatus::Valid )
    {
        emit errMessage("IpcommNetworkSelector::DeviceStatus nicht Valid");
        emit errMessage(m_networkSelector->lastErrorMsg());

    }
    else
    {
        emit eventDeviceStatusValid();        
        return;
    }

    if ( m_networkSelector->deviceStatus() == IpcommNetworkSelector::DeviceStatus::Fatal )
    {
        emit errMessage("IpcommNetworkSelector::DeviceStatus::Fatal");
        emit errMessage(m_networkSelector->lastErrorMsg());
    }
    else if (m_networkSelector->deviceStatus() == IpcommNetworkSelector::DeviceStatus::Critical
             || m_networkSelector->deviceStatus() == IpcommNetworkSelector::DeviceStatus::Invalid)
    {
        emit errMessage("IpcommNetworkSelector::DeviceStatus::Critical");
        emit errMessage("IpcommNetworkSelector::DeviceStatus::Invalid");
        emit errMessage(m_networkSelector->lastErrorMsg());
        return;
    }
}

/*
 *
 */
bool NetworkSelector::switchToNetwork(IpcommNetworkSelector::NetworkRelais relais,
                                      QString &errorMsg, int timeoutMs)
{    
    // Umschalten wenn notwendig
    QEventLoop loop;
    QTimer     timeoutTimer;
    bool       eventReceived = false;

    // Event-Signal
    connect(this, &NetworkSelector::eventDeviceStatusValid, &loop, [&]() {
        eventReceived = true;
        loop.quit();
    });

    // Timeout-Timer
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(timeoutMs);

    // Umschalten in Sicheres Netzwerk
    if (m_networkSelector != nullptr)
        m_networkSelector->switchToNetwork(relais, errorMsg);

    // Warten
    loop.exec();

    if (eventReceived)
    {        
        if (relais == m_settings.SecureNetworkId)
            m_mqttClient->publish("network/secure","TRUE");
        if (relais == m_settings.UnSecureNetworkId)
            m_mqttClient->publish("network/unsecure","TRUE");
        return true;
    }
    else
    {
        QString error;
        emit errMessage("Timeout Netzwerkumschalter...");
        return false;
    }
}


/*
 *
 */
void NetworkSelector::onSwitchID(int id)
{
    if ((m_networkSelector == nullptr)||(m_automaticMode)) return;

    QString error;
    emit startWork();

    IpcommNetworkSelector::NetworkRelais relais;
    switch (id) {
    case 1:
        relais = IpcommNetworkSelector::NetworkRelais::E1;
        break;
    case 2:
        relais = IpcommNetworkSelector::NetworkRelais::E2;
        break;
    case 3:
        relais = IpcommNetworkSelector::NetworkRelais::E3;
        break;
    case 4:
        relais = IpcommNetworkSelector::NetworkRelais::E4;
        break;
    default:        
        emit errMessage(tr("Network switch [%1]ID not found!\n").arg(id));
        emit endWork();
        return;
    }

    switchToNetwork(relais, error, 10000);
    emit endWork();
}

/*
 *
 */
bool NetworkSelector::switchToSecure(int timeoutMs)
{
    // TRUE zurükgeben wenn kein Netzwerkumschalter verwendet wird
    if (m_networkSelector == nullptr) return true;

    // Prüfen wie der Netzwerkstatus ist
    if (( IpcommNetworkSelector::NetworkRelais::E1 == m_settings.SecureNetworkId)&&
        (m_statusNetwork.nwConnected[0])) return true;
    if (( IpcommNetworkSelector::NetworkRelais::E2 == m_settings.SecureNetworkId)&&
        (m_statusNetwork.nwConnected[1])) return true;
    if (( IpcommNetworkSelector::NetworkRelais::E3 == m_settings.SecureNetworkId)&&
        (m_statusNetwork.nwConnected[2])) return true;
    if (( IpcommNetworkSelector::NetworkRelais::E4 == m_settings.SecureNetworkId)&&
        (m_statusNetwork.nwConnected[3])) return true;

    QString errorMsg;
    return switchToNetwork(m_settings.SecureNetworkId, errorMsg, timeoutMs);
}

/*
 *
 */
bool NetworkSelector::switchToUnsecure(int timeoutMs)
{
    // TRUE zurükgeben wenn kein Netzwerkumschalter verwendet wird
    if (m_networkSelector == nullptr) return true;

    // Prüfen wie der Netzwerkstatus ist
    if (( IpcommNetworkSelector::NetworkRelais::E1 == m_settings.UnSecureNetworkId)&&
        (m_statusNetwork.nwConnected[0])) return true;
    if (( IpcommNetworkSelector::NetworkRelais::E2 == m_settings.UnSecureNetworkId)&&
        (m_statusNetwork.nwConnected[1])) return true;
    if (( IpcommNetworkSelector::NetworkRelais::E3 == m_settings.UnSecureNetworkId)&&
        (m_statusNetwork.nwConnected[2])) return true;
    if (( IpcommNetworkSelector::NetworkRelais::E4 == m_settings.UnSecureNetworkId)&&
        (m_statusNetwork.nwConnected[3])) return true;

    QString errorMsg;
    return switchToNetwork(m_settings.UnSecureNetworkId, errorMsg, timeoutMs);
}
