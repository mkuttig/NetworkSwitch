#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QTimer>
#include <QMap>
#include <memory>
#include <functional>

/**
 * @brief MQTT-Client Implementierung für Qt mit Topic-Handler-System
 *
 * Diese Klasse implementiert einen MQTT 3.1.1 Client mit integriertem Handler-System.
 * Topics können mit eigenen Callback-Funktionen abonniert werden.
 *
 * Verwendung:
 * @code
 * MqttClient client;
 * client.connectToHost("localhost", 1883, "MeinClient");
 * connect(&client, &MqttClient::connected, [&client]() {
 *     // Topic mit Handler abonnieren
 *     client.subscribe("sensor/temperature", [](const QByteArray &data) {
 *         qDebug() << "Temperatur:" << data;
 *     });
 *
 *     // Ohne Handler - nutzt messageReceived Signal
 *     client.subscribe("other/topic");
 * });
 * @endcode
 */
class MqttClient : public QObject
{
    Q_OBJECT

public:
    /// Typ-Alias für Handler-Funktionen
    using TopicHandler = std::function<void(const QByteArray&)>;

    /**
     * @brief Konstruktor
     * @param parent Eltern-QObject für automatische Speicherverwaltung
     */
    explicit MqttClient(QObject *parent = nullptr);

    /**
     * @brief Destruktor
     *
     * Trennt automatisch die Verbindung und gibt Ressourcen frei.
     * Smart Pointer sorgen für automatisches Aufräumen.
     */
    ~MqttClient() override;

    /**
     * @brief Verbindet zum MQTT-Broker
     * @param host Hostname oder IP-Adresse des Brokers
     * @param port Port des Brokers (Standard: 1883)
     * @param clientId Eindeutige Client-ID für diese Verbindung
     *
     * Startet die TCP-Verbindung und sendet nach erfolgreicher Verbindung
     * automatisch das MQTT CONNECT-Paket.
     */
    void connectToHost(const QString &host, quint16 port, const QString &clientId);

    /**
     * @brief Publiziert eine Nachricht zu einem Topic
     * @param topic MQTT-Topic (z.B. "sensor/temperature")
     * @param message Nachrichteninhalt als Byte-Array
     * @param qos Quality of Service Level (0, 1 oder 2) - Standard: 0
     * @param retain Soll die Nachricht vom Broker gespeichert werden? Standard: false
     *
     * Sendet eine PUBLISH-Nachricht an den Broker. Bei QoS 0 erfolgt keine Bestätigung.
     *
     * @note Die Verbindung muss bestehen (isConnected() == true)
     */
    void publish(const QString &topic, const QByteArray &message, quint8 qos = 0, bool retain = false);

    /**
     * @brief Abonniert ein Topic ohne Handler (nutzt messageReceived Signal)
     * @param topic MQTT-Topic das abonniert werden soll (z.B. "sensor/#")
     * @param qos Quality of Service Level (0, 1 oder 2) - Standard: 0
     *
     * Sendet ein SUBSCRIBE-Paket. Empfangene Nachrichten werden über
     * das messageReceived() Signal weitergeleitet.
     * Wildcards werden unterstützt: # (mehrstufig), + (einstufig)
     */
    void subscribe(const QString &topic, quint8 qos = 0);

    /**
     * @brief Abonniert ein Topic mit Handler-Funktion
     * @param topic MQTT-Topic das abonniert werden soll
     * @param handler Callback-Funktion die bei empfangenen Nachrichten aufgerufen wird
     * @param qos Quality of Service Level (0, 1 oder 2) - Standard: 0
     *
     * Registriert eine Handler-Funktion für ein spezifisches Topic.
     * Bei empfangenen Nachrichten wird der Handler direkt aufgerufen.
     *
     * Beispiel:
     * @code
     * client.subscribe("sensor/temp", [](const QByteArray &data) {
     *     qDebug() << "Temperatur:" << data;
     * }, 0);
     * @endcode
     *
     * @note Der Handler hat Vorrang vor dem messageReceived Signal
     */
    void subscribe(const QString &topic, TopicHandler handler, quint8 qos = 0);

    /**
     * @brief Meldet ein Topic ab
     * @param topic Das abzumeldende Topic
     *
     * Sendet ein UNSUBSCRIBE-Paket und entfernt den registrierten Handler.
     */
    void unsubscribe(const QString &topic);

    /**
     * @brief Registriert einen Handler für ein bereits abonniertes Topic
     * @param topic Das Topic für das der Handler registriert werden soll
     * @param handler Die Callback-Funktion
     *
     * Kann verwendet werden um nachträglich Handler zu registrieren.
     */
    void registerHandler(const QString &topic, TopicHandler handler);

    /**
     * @brief Entfernt einen Handler für ein Topic
     * @param topic Das Topic dessen Handler entfernt werden soll
     *
     * Das Topic bleibt abonniert, Nachrichten werden über messageReceived Signal gesendet.
     */
    void unregisterHandler(const QString &topic);

    /**
     * @brief Prüft ob ein Handler für ein Topic registriert ist
     * @param topic Das zu prüfende Topic
     * @return true wenn ein Handler registriert ist
     */
    bool hasHandler(const QString &topic) const;

    /**
     * @brief Trennt die Verbindung zum Broker
     *
     * Sendet ein DISCONNECT-Paket und schließt die TCP-Verbindung sauber.
     * Stoppt auch den Keep-Alive Timer und löscht alle Handler.
     */
    void disconnect();

    /**
     * @brief Prüft ob die MQTT-Verbindung besteht
     * @return true wenn CONNACK empfangen wurde und Verbindung aktiv ist
     */
    bool isConnected() const { return m_connected; }

signals:
    /**
     * @brief Signal wird ausgelöst wenn CONNACK empfangen wurde
     *
     * Nach diesem Signal können publish() und subscribe() aufgerufen werden.
     */
    void connected();

    /**
     * @brief Signal wird ausgelöst wenn die Verbindung getrennt wurde
     *
     * Wird sowohl bei gewolltem disconnect() als auch bei Verbindungsabbruch ausgelöst.
     */
    void disconnected();

    /**
     * @brief Signal wird ausgelöst wenn eine PUBLISH-Nachricht empfangen wurde
     * @param topic Das Topic der empfangenen Nachricht
     * @param message Der Nachrichteninhalt als Byte-Array
     *
     * @note Wird NUR ausgelöst wenn KEIN Handler für das Topic registriert ist.
     *       Bei registrierten Handlers wird stattdessen der Handler aufgerufen.
     */
    void messageReceived(const QString &topic, const QByteArray &message);

    /**
     * @brief Signal wird ausgelöst wenn eine Nachricht erfolgreich publiziert wurde
     * @param topic Das Topic zu dem publiziert wurde
     */
    void published(const QString &topic);

    /**
     * @brief Signal wird ausgelöst wenn ein Topic erfolgreich abonniert wurde
     * @param topic Das abonnierte Topic
     */
    void subscribed(const QString &topic);

    /**
     * @brief Signal wird ausgelöst wenn ein Topic erfolgreich abgemeldet wurde
     * @param topic Das abgemeldete Topic
     */
    void unsubscribed(const QString &topic);

    /**
     * @brief Signal wird bei Fehlern ausgelöst
     * @param errorString Beschreibung des Fehlers
     */
    void error(const QString &errorString);

private slots:
    /**
     * @brief Slot wird aufgerufen wenn TCP-Verbindung hergestellt wurde
     *
     * Sendet automatisch das MQTT CONNECT-Paket.
     */
    void onConnected();

    /**
     * @brief Slot wird aufgerufen wenn TCP-Verbindung getrennt wurde
     *
     * Setzt m_connected auf false, stoppt Timer und löscht Handler.
     */
    void onDisconnected();

    /**
     * @brief Slot wird aufgerufen wenn Daten vom Socket empfangen wurden
     *
     * Parsed MQTT-Pakete (CONNACK, PUBLISH, SUBACK, UNSUBACK, PINGRESP) und
     * ruft entsprechende Handler auf oder löst Signals aus.
     */
    void onReadyRead();

    /**
     * @brief Slot wird bei Socket-Fehlern aufgerufen
     * @param socketError Qt Socket Error Code
     *
     * Setzt m_connected auf false und löst error() Signal aus.
     */
    void onSocketError(QAbstractSocket::SocketError socketError);

    /**
     * @brief Slot wird vom Keep-Alive Timer aufgerufen
     *
     * Sendet ein PINGREQ-Paket um die Verbindung aufrecht zu erhalten.
     * Wird alle 20 Sekunden (2/3 des Keep-Alive Intervalls) aufgerufen.
     */
    void sendPingRequest();

private:
    /**
     * @brief Erstellt ein MQTT CONNECT-Paket
     * @param clientId Die Client-ID für diese Verbindung
     * @return Fertiges CONNECT-Paket als Byte-Array
     *
     * Erstellt MQTT 3.1.1 CONNECT mit Clean Session Flag und Keep-Alive.
     */
    QByteArray createConnectPacket(const QString &clientId);

    /**
     * @brief Erstellt ein MQTT PUBLISH-Paket
     * @param topic Das Ziel-Topic
     * @param payload Die zu sendenden Daten
     * @param qos Quality of Service (0, 1 oder 2)
     * @param retain Retain-Flag
     * @return Fertiges PUBLISH-Paket als Byte-Array
     */
    QByteArray createPublishPacket(const QString &topic, const QByteArray &payload, quint8 qos, bool retain);

    /**
     * @brief Erstellt ein MQTT SUBSCRIBE-Paket
     * @param topic Das zu abonnierende Topic
     * @param qos Gewünschter QoS-Level
     * @return Fertiges SUBSCRIBE-Paket als Byte-Array
     *
     * Inkrementiert automatisch die Packet-ID.
     */
    QByteArray createSubscribePacket(const QString &topic, quint8 qos);

    /**
     * @brief Erstellt ein MQTT UNSUBSCRIBE-Paket
     * @param topic Das abzumeldende Topic
     * @return Fertiges UNSUBSCRIBE-Paket als Byte-Array
     *
     * Inkrementiert automatisch die Packet-ID.
     */
    QByteArray createUnsubscribePacket(const QString &topic);

    /**
     * @brief Erstellt ein MQTT DISCONNECT-Paket
     * @return Fertiges DISCONNECT-Paket (2 Bytes)
     */
    QByteArray createDisconnectPacket();

    /**
     * @brief Erstellt ein MQTT PINGREQ-Paket
     * @return Fertiges PINGREQ-Paket (2 Bytes)
     */
    QByteArray createPingRequestPacket();

    /**
     * @brief Kodiert die "Remaining Length" nach MQTT-Spezifikation
     * @param buffer Ziel-Buffer (wird erweitert)
     * @param length Die zu kodierende Länge
     * @return Anzahl der verwendeten Bytes (1-4)
     *
     * MQTT verwendet eine variable Längenkodierung mit 7 Bit pro Byte.
     */
    quint16 encodeRemainingLength(QByteArray &buffer, quint32 length);

    /**
     * @brief Dekodiert die "Remaining Length" nach MQTT-Spezifikation
     * @param data Quell-Daten
     * @param offset Start-Offset (wird erhöht)
     * @return Die dekodierte Länge
     *
     * Liest 1-4 Bytes und berechnet die tatsächliche Paketlänge.
     */
    quint32 decodeRemainingLength(const QByteArray &data, int &offset);

    /**
     * @brief Verarbeitet empfangene PUBLISH-Nachricht
     * @param topic Das empfangene Topic
     * @param message Die empfangene Nachricht
     *
     * Prüft ob ein Handler registriert ist und ruft diesen auf,
     * andernfalls wird das messageReceived Signal ausgelöst.
     */
    void handlePublishMessage(const QString &topic, const QByteArray &message);

    // Mitgliedsvariablen
    std::unique_ptr<QTcpSocket> m_socket;                    ///< TCP-Socket für MQTT-Kommunikation (Smart Pointer)
    std::unique_ptr<QTimer> m_keepAliveTimer;                ///< Timer für Keep-Alive (PINGREQ) (Smart Pointer)
    QMap<QString, TopicHandler> m_topicHandlers;             ///< Map: Topic -> Handler-Funktion
    QString m_clientId;                                      ///< MQTT Client-ID
    bool m_connected;                                        ///< true wenn CONNACK empfangen wurde
    quint16 m_packetId;                                      ///< Laufende Packet-ID für SUBSCRIBE/PUBLISH QoS>0
    QByteArray m_buffer;                                     ///< Empfangspuffer für unvollständige Pakete
    quint16 m_keepAliveInterval;                             ///< Keep-Alive Intervall in Sekunden (Standard: 30)
};

#endif // MQTTCLIENT_H
