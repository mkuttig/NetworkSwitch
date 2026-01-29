#include "mqttclient.h"
#include <QDebug>

/**
 * @brief Konstruktor - Initialisiert den MQTT-Client
 *
 * Erstellt Socket und Timer mit Smart Pointern und verbindet alle Signals.
 * Setzt Socket-Optionen für stabile Verbindung (Keep-Alive, Low Delay).
 */
MqttClient::MqttClient(QObject *parent)
    : QObject(parent)
    , m_socket(std::make_unique<QTcpSocket>(this))          // Smart Pointer mit Parent für Qt-Integration
    , m_keepAliveTimer(std::make_unique<QTimer>(this))      // Smart Pointer mit Parent
    , m_connected(false)
    , m_packetId(1)
    , m_keepAliveInterval(30)  // 30 Sekunden Keep-Alive
{
    // Socket-Signals verbinden
    connect(m_socket.get(), &QTcpSocket::connected, this, &MqttClient::onConnected);
    connect(m_socket.get(), &QTcpSocket::disconnected, this, &MqttClient::onDisconnected);
    connect(m_socket.get(), &QTcpSocket::readyRead, this, &MqttClient::onReadyRead);
    connect(m_socket.get(), QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &MqttClient::onSocketError);

    // Keep-Alive Timer Signal verbinden
    connect(m_keepAliveTimer.get(), &QTimer::timeout, this, &MqttClient::sendPingRequest);

    // Socket-Optionen für stabilere Verbindung
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);  // TCP Keep-Alive aktivieren
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);   // Nagle-Algorithmus deaktivieren
}

/**
 * @brief Destruktor - Trennt Verbindung und räumt auf
 *
 * Smart Pointer geben automatisch QTcpSocket und QTimer frei.
 */
MqttClient::~MqttClient()
{
    if (m_connected) {
        disconnect();  // Sauberes Trennen wenn noch verbunden
    }
    // Smart Pointer räumen automatisch auf - kein manuelles delete nötig!
}

/**
 * @brief Verbindet zum MQTT-Broker
 *
 * Startet asynchrone TCP-Verbindung. Nach erfolgreicher Verbindung
 * wird automatisch onConnected() aufgerufen, welches das CONNECT-Paket sendet.
 */
void MqttClient::connectToHost(const QString &host, quint16 port, const QString &clientId)
{
    m_clientId = clientId;
    qDebug() << "Verbinde mit" << host << ":" << port;
    m_socket->connectToHost(host, port);
}

/**
 * @brief Erstellt MQTT CONNECT-Paket nach Spezifikation 3.1.1
 *
 * Struktur:
 * - Fixed Header: 0x10 (CONNECT)
 * - Remaining Length: Variable
 * - Variable Header: Protocol Name, Level, Flags, Keep-Alive
 * - Payload: Client-ID
 */
QByteArray MqttClient::createConnectPacket(const QString &clientId)
{
    QByteArray packet;
    packet.append((char)0x10);  // CONNECT Packet Type

    // Variable Header
    QByteArray variableHeader;
    variableHeader.append((char)0x00);
    variableHeader.append((char)0x04);
    variableHeader.append("MQTT");                    // Protocol Name
    variableHeader.append((char)0x04);                // Protocol Level (MQTT 3.1.1)
    variableHeader.append((char)0x02);                // Connect Flags: Clean Session

    // Keep-Alive in Sekunden (2 Bytes, Big Endian)
    variableHeader.append((char)((m_keepAliveInterval >> 8) & 0xFF));  // High Byte
    variableHeader.append((char)(m_keepAliveInterval & 0xFF));          // Low Byte

    // Payload: Client-ID
    QByteArray payload;
    QByteArray clientIdUtf8 = clientId.toUtf8();
    payload.append((char)(clientIdUtf8.length() >> 8));  // Länge High Byte
    payload.append((char)(clientIdUtf8.length() & 0xFF)); // Länge Low Byte
    payload.append(clientIdUtf8);                         // Client-ID Daten

    // Remaining Length berechnen und kodieren
    quint32 remainingLength = variableHeader.length() + payload.length();
    encodeRemainingLength(packet, remainingLength);

    // Alles zusammenfügen
    packet.append(variableHeader);
    packet.append(payload);

    return packet;
}

/**
 * @brief Erstellt MQTT PUBLISH-Paket
 *
 * Struktur:
 * - Fixed Header: 0x30 | QoS | Retain
 * - Remaining Length: Variable
 * - Variable Header: Topic Name (+ Packet ID bei QoS>0)
 * - Payload: Nachrichteninhalt
 */
QByteArray MqttClient::createPublishPacket(const QString &topic, const QByteArray &payload, quint8 qos, bool retain)
{
    QByteArray packet;

    // Fixed Header: PUBLISH (0x30) + Flags
    quint8 fixedHeader = 0x30;  // PUBLISH
    if (retain) fixedHeader |= 0x01;  // Retain-Bit setzen
    fixedHeader |= (qos << 1);        // QoS-Bits setzen (Bit 1-2)

    packet.append((char)fixedHeader);

    // Variable Header: Topic Name
    QByteArray variableHeader;
    QByteArray topicUtf8 = topic.toUtf8();
    variableHeader.append((char)(topicUtf8.length() >> 8));   // Länge High Byte
    variableHeader.append((char)(topicUtf8.length() & 0xFF)); // Länge Low Byte
    variableHeader.append(topicUtf8);                         // Topic-Daten

    // Remaining Length berechnen und kodieren
    quint32 remainingLength = variableHeader.length() + payload.length();
    encodeRemainingLength(packet, remainingLength);

    // Zusammenfügen
    packet.append(variableHeader);
    packet.append(payload);

    return packet;
}

/**
 * @brief Erstellt MQTT SUBSCRIBE-Paket
 *
 * Struktur:
 * - Fixed Header: 0x82 (SUBSCRIBE mit QoS 1)
 * - Remaining Length: Variable
 * - Variable Header: Packet ID
 * - Payload: Topic Filter + QoS
 */
QByteArray MqttClient::createSubscribePacket(const QString &topic, quint8 qos)
{
    QByteArray packet;
    packet.append((char)0x82);  // SUBSCRIBE mit QoS 1 (erforderlich)

    // Variable Header: Packet ID
    QByteArray variableHeader;
    variableHeader.append((char)(m_packetId >> 8));   // Packet ID High Byte
    variableHeader.append((char)(m_packetId & 0xFF)); // Packet ID Low Byte
    m_packetId++;  // Für nächstes Paket inkrementieren

    // Payload: Topic Filter + QoS
    QByteArray payload;
    QByteArray topicUtf8 = topic.toUtf8();
    payload.append((char)(topicUtf8.length() >> 8));   // Länge High Byte
    payload.append((char)(topicUtf8.length() & 0xFF)); // Länge Low Byte
    payload.append(topicUtf8);                         // Topic-Daten
    payload.append((char)qos);                         // Gewünschter QoS-Level

    // Remaining Length berechnen und kodieren
    quint32 remainingLength = variableHeader.length() + payload.length();
    encodeRemainingLength(packet, remainingLength);

    // Zusammenfügen
    packet.append(variableHeader);
    packet.append(payload);

    return packet;
}

/**
 * @brief Erstellt MQTT UNSUBSCRIBE-Paket
 *
 * Struktur:
 * - Fixed Header: 0xA2 (UNSUBSCRIBE mit QoS 1)
 * - Remaining Length: Variable
 * - Variable Header: Packet ID
 * - Payload: Topic Filter
 */
QByteArray MqttClient::createUnsubscribePacket(const QString &topic)
{
    QByteArray packet;
    packet.append((char)0xA2);  // UNSUBSCRIBE mit QoS 1 (erforderlich)

    // Variable Header: Packet ID
    QByteArray variableHeader;
    variableHeader.append((char)(m_packetId >> 8));   // Packet ID High Byte
    variableHeader.append((char)(m_packetId & 0xFF)); // Packet ID Low Byte
    m_packetId++;  // Für nächstes Paket inkrementieren

    // Payload: Topic Filter
    QByteArray payload;
    QByteArray topicUtf8 = topic.toUtf8();
    payload.append((char)(topicUtf8.length() >> 8));   // Länge High Byte
    payload.append((char)(topicUtf8.length() & 0xFF)); // Länge Low Byte
    payload.append(topicUtf8);                         // Topic-Daten

    // Remaining Length berechnen und kodieren
    quint32 remainingLength = variableHeader.length() + payload.length();
    encodeRemainingLength(packet, remainingLength);

    // Zusammenfügen
    packet.append(variableHeader);
    packet.append(payload);

    return packet;
}

/**
 * @brief Erstellt MQTT DISCONNECT-Paket
 *
 * Einfaches 2-Byte Paket ohne Variable Header oder Payload.
 */
QByteArray MqttClient::createDisconnectPacket()
{
    QByteArray packet;
    packet.append((char)0xE0);  // DISCONNECT Packet Type
    packet.append((char)0x00);  // Remaining Length = 0
    return packet;
}

/**
 * @brief Erstellt MQTT PINGREQ-Paket für Keep-Alive
 *
 * Einfaches 2-Byte Paket ohne Variable Header oder Payload.
 */
QByteArray MqttClient::createPingRequestPacket()
{
    QByteArray packet;
    packet.append((char)0xC0);  // PINGREQ Packet Type
    packet.append((char)0x00);  // Remaining Length = 0
    return packet;
}

/**
 * @brief Kodiert Länge nach MQTT Variable Length Encoding
 *
 * MQTT verwendet 7 Bit pro Byte, das 8. Bit ist Continuation-Bit.
 * Maximal 4 Bytes werden verwendet (268,435,455 max Länge).
 *
 * Beispiel: 321 = 0xC1 0x02 (193 + 128, 2)
 */
quint16 MqttClient::encodeRemainingLength(QByteArray &buffer, quint32 length)
{
    quint16 bytesNeeded = 0;
    do {
        quint8 byte = length % 128;  // Untere 7 Bit
        length /= 128;
        if (length > 0)
            byte |= 0x80;  // Continuation-Bit setzen wenn weitere Bytes folgen
        buffer.append((char)byte);
        bytesNeeded++;
    } while (length > 0);

    return bytesNeeded;
}

/**
 * @brief Dekodiert MQTT Variable Length Encoding
 *
 * Liest 1-4 Bytes und berechnet die tatsächliche Länge.
 * Offset wird automatisch erhöht.
 *
 * @return 0 bei Fehler oder dekodierte Länge
 */
quint32 MqttClient::decodeRemainingLength(const QByteArray &data, int &offset)
{
    quint32 multiplier = 1;
    quint32 value = 0;
    quint8 encodedByte;

    do {
        // Prüfen ob genug Daten vorhanden
        if (offset >= data.length())
            return 0;

        encodedByte = data.at(offset++);
        value += (encodedByte & 127) * multiplier;  // Untere 7 Bit extrahieren
        multiplier *= 128;

        // Schutz gegen ungültige Kodierung (mehr als 4 Bytes)
        if (multiplier > 128 * 128 * 128)
            return 0;
    } while ((encodedByte & 128) != 0);  // Solange Continuation-Bit gesetzt ist

    return value;
}

/**
 * @brief Publiziert eine Nachricht zum angegebenen Topic
 *
 * Prüft zuerst ob Verbindung besteht, erstellt dann PUBLISH-Paket
 * und sendet es über den Socket.
 */
void MqttClient::publish(const QString &topic, const QByteArray &message, quint8 qos, bool retain)
{
    // Prüfen ob verbunden
    if (!m_connected) {
        emit error("Nicht verbunden!");
        return;
    }

    // PUBLISH-Paket erstellen und senden
    QByteArray packet = createPublishPacket(topic, message, qos, retain);
    qint64 written = m_socket->write(packet);

    if (written == -1) {
        emit error("Fehler beim Senden!");
        return;
    }

    m_socket->flush();  // Sofort senden

    qDebug() << "Nachricht publiziert - Topic:" << topic << "| Message:" << message;
    emit published(topic);
}

/**
 * @brief Abonniert ein Topic ohne Handler (nutzt messageReceived Signal)
 *
 * Sendet SUBSCRIBE-Paket. Empfangene Nachrichten werden über
 * das messageReceived() Signal weitergeleitet.
 */
void MqttClient::subscribe(const QString &topic, quint8 qos)
{
    // Prüfen ob verbunden
    if (!m_connected) {
        emit error("Nicht verbunden!");
        return;
    }

    // SUBSCRIBE-Paket erstellen und senden
    QByteArray packet = createSubscribePacket(topic, qos);
    m_socket->write(packet);
    m_socket->flush();

    qDebug() << "Subscribe gesendet - Topic:" << topic << "(ohne Handler)";
    emit subscribed(topic);
}

/**
 * @brief Abonniert ein Topic mit Handler-Funktion
 *
 * Registriert die Handler-Funktion und sendet SUBSCRIBE-Paket.
 * Bei empfangenen Nachrichten wird der Handler direkt aufgerufen.
 */
void MqttClient::subscribe(const QString &topic, TopicHandler handler, quint8 qos)
{
    // Prüfen ob verbunden
    if (!m_connected) {
        emit error("Nicht verbunden!");
        return;
    }

    // Handler registrieren
    m_topicHandlers[topic] = handler;
    qDebug() << "Handler registriert für Topic:" << topic;

    // SUBSCRIBE-Paket erstellen und senden
    QByteArray packet = createSubscribePacket(topic, qos);
    m_socket->write(packet);
    m_socket->flush();

    qDebug() << "Subscribe gesendet - Topic:" << topic << "(mit Handler)";
    emit subscribed(topic);
}

/**
 * @brief Meldet ein Topic ab
 *
 * Sendet UNSUBSCRIBE-Paket und entfernt den registrierten Handler.
 */
void MqttClient::unsubscribe(const QString &topic)
{
    // Prüfen ob verbunden
    if (!m_connected) {
        emit error("Nicht verbunden!");
        return;
    }

    // Handler entfernen (falls vorhanden)
    if (m_topicHandlers.contains(topic)) {
        m_topicHandlers.remove(topic);
        qDebug() << "Handler entfernt für Topic:" << topic;
    }

    // UNSUBSCRIBE-Paket erstellen und senden
    QByteArray packet = createUnsubscribePacket(topic);
    m_socket->write(packet);
    m_socket->flush();

    qDebug() << "Unsubscribe gesendet - Topic:" << topic;
    emit unsubscribed(topic);
}

/**
 * @brief Registriert einen Handler für ein bereits abonniertes Topic
 *
 * Kann verwendet werden um nachträglich Handler zu registrieren.
 */
void MqttClient::registerHandler(const QString &topic, TopicHandler handler)
{
    m_topicHandlers[topic] = handler;
    qDebug() << "Handler nachträglich registriert für Topic:" << topic;
}

/**
 * @brief Entfernt einen Handler für ein Topic
 *
 * Das Topic bleibt abonniert, Nachrichten werden über messageReceived Signal gesendet.
 */
void MqttClient::unregisterHandler(const QString &topic)
{
    if (m_topicHandlers.contains(topic)) {
        m_topicHandlers.remove(topic);
        qDebug() << "Handler entfernt für Topic:" << topic;
    } else {
        qDebug() << "Kein Handler vorhanden für Topic:" << topic;
    }
}

/**
 * @brief Prüft ob ein Handler für ein Topic registriert ist
 */
bool MqttClient::hasHandler(const QString &topic) const
{
    return m_topicHandlers.contains(topic);
}

/**
 * @brief Trennt die Verbindung zum Broker sauber
 *
 * Stoppt Keep-Alive Timer, sendet DISCONNECT-Paket, löscht alle Handler
 * und schließt Socket.
 */
void MqttClient::disconnect()
{
    // Keep-Alive Timer stoppen
    m_keepAliveTimer->stop();

    // Alle Handler löschen
    m_topicHandlers.clear();
    qDebug() << "Alle Handler gelöscht";

    // Nur trennen wenn Socket verbunden ist
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        // DISCONNECT-Paket senden
        QByteArray packet = createDisconnectPacket();
        m_socket->write(packet);
        m_socket->flush();
        m_socket->waitForBytesWritten(1000);  // Max 1 Sekunde warten

        // Socket trennen
        m_socket->disconnectFromHost();

        // Warten bis wirklich getrennt (max 1 Sekunde)
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->waitForDisconnected(1000);
        }
    }
}

/**
 * @brief Verarbeitet empfangene PUBLISH-Nachricht
 *
 * Prüft ob ein Handler registriert ist und ruft diesen auf,
 * andernfalls wird das messageReceived Signal ausgelöst.
 */
void MqttClient::handlePublishMessage(const QString &topic, const QByteArray &message)
{
    // Prüfen ob ein Handler für dieses Topic registriert ist
    if (m_topicHandlers.contains(topic)) {
        qDebug() << "Handler aufgerufen für Topic:" << topic;
        // Handler aufrufen
        m_topicHandlers[topic](message);
    } else {
        // Kein Handler -> Signal aussenden
        qDebug() << "Kein Handler - Signal ausgelöst für Topic:" << topic;
        emit messageReceived(topic, message);
    }
}

/**
 * @brief Slot: TCP-Verbindung wurde hergestellt
 *
 * Sendet automatisch MQTT CONNECT-Paket.
 * Nach CONNACK-Empfang wird connected() Signal ausgelöst.
 */
void MqttClient::onConnected()
{
    qDebug() << "TCP Verbindung hergestellt, sende CONNECT Paket...";

    // MQTT CONNECT-Paket erstellen und senden
    QByteArray connectPacket = createConnectPacket(m_clientId);
    m_socket->write(connectPacket);
    m_socket->flush();
}

/**
 * @brief Slot: TCP-Verbindung wurde getrennt
 *
 * Setzt Status zurück, stoppt Keep-Alive Timer und löscht alle Handler.
 * Löst disconnected() Signal aus.
 */
void MqttClient::onDisconnected()
{
    m_connected = false;
    m_keepAliveTimer->stop();

    // Alle Handler löschen
    m_topicHandlers.clear();
    qDebug() << "Verbindung getrennt - Alle Handler gelöscht";

    emit disconnected();
}

/**
 * @brief Slot: Daten vom Socket empfangen
 *
 * Parsed empfangene MQTT-Pakete:
 * - CONNACK (0x20): Verbindungsbestätigung
 * - PUBLISH (0x30): Empfangene Nachricht -> ruft handlePublishMessage() auf
 * - SUBACK (0x90): Subscription-Bestätigung
 * - UNSUBACK (0xB0): Unsubscription-Bestätigung
 * - PINGRESP (0xD0): Keep-Alive Antwort
 *
 * Puffert unvollständige Pakete in m_buffer.
 */
void MqttClient::onReadyRead()
{
    // Neue Daten zum Puffer hinzufügen
    m_buffer.append(m_socket->readAll());

    // Alle vollständigen Pakete verarbeiten
    while (!m_buffer.isEmpty()) {
        // Mindestens 2 Bytes nötig (Fixed Header + Remaining Length)
        if (m_buffer.length() < 2)
            return;

        // Fixed Header parsen
        quint8 packetType = m_buffer.at(0);
        int offset = 1;

        // Remaining Length dekodieren
        quint32 remainingLength = decodeRemainingLength(m_buffer, offset);

        // Prüfen ob vollständiges Paket vorhanden
        if (m_buffer.length() < offset + remainingLength)
            return;  // Warten auf mehr Daten

        // Paket-Daten extrahieren
        QByteArray packetData = m_buffer.mid(offset, remainingLength);
        m_buffer.remove(0, offset + remainingLength);  // Verarbeitete Daten entfernen

        // ===== Paket-Typ verarbeiten =====

        // CONNACK (0x20) - Verbindungsbestätigung
        if ((packetType & 0xF0) == 0x20) {
            if (packetData.length() >= 2 && packetData.at(1) == 0x00) {
                m_connected = true;
                qDebug() << "MQTT CONNACK empfangen - Verbindung erfolgreich!";

                // Keep-Alive Timer starten (alle 20 Sekunden = 2/3 des Keep-Alive Intervalls)
                m_keepAliveTimer->start((m_keepAliveInterval * 1000 * 2) / 3);

                emit connected();
            } else {
                quint8 returnCode = packetData.at(1);
                qDebug() << "MQTT CONNACK - Verbindung abgelehnt, Code:" << returnCode;
                emit error("Verbindung vom Broker abgelehnt (Code: " + QString::number(returnCode) + ")");
            }
        }
        // PUBLISH (0x30) - Empfangene Nachricht
        else if ((packetType & 0xF0) == 0x30) {
            int pos = 0;

            // Topic Length lesen (2 Bytes)
            if (packetData.length() < 2)
                continue;

            quint16 topicLength = (quint8)packetData.at(pos) << 8 | (quint8)packetData.at(pos + 1);
            pos += 2;

            // Topic lesen
            if (packetData.length() < pos + topicLength)
                continue;

            QString topic = QString::fromUtf8(packetData.mid(pos, topicLength));
            pos += topicLength;

            // Payload extrahieren (Rest des Pakets)
            QByteArray message = packetData.mid(pos);

            qDebug() << "PUBLISH empfangen - Topic:" << topic << "| Message:" << message;

            // Handler aufrufen oder Signal aussenden
            handlePublishMessage(topic, message);
        }
        // SUBACK (0x90) - Subscription-Bestätigung
        else if ((packetType & 0xF0) == 0x90) {
            qDebug() << "SUBACK empfangen - Subscription erfolgreich!";
        }
        // UNSUBACK (0xB0) - Unsubscription-Bestätigung
        else if ((packetType & 0xF0) == 0xB0) {
            qDebug() << "UNSUBACK empfangen - Unsubscription erfolgreich!";
        }
        // PINGRESP (0xD0) - Keep-Alive Antwort
        else if ((packetType & 0xF0) == 0xD0) {
            qDebug() << "PINGRESP empfangen - Keep-Alive OK";
        }
    }
}

/**
 * @brief Slot: Socket-Fehler aufgetreten
 *
 * Setzt Status zurück, stoppt Timer und löst error() Signal aus.
 */
void MqttClient::onSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError)  // Parameter nicht verwendet, verhindert Compiler-Warnung

    QString errorMsg = m_socket->errorString();
    qDebug() << "Socket Fehler:" << errorMsg;

    // Status zurücksetzen
    m_connected = false;
    m_keepAliveTimer->stop();

    emit error(errorMsg);
}

/**
 * @brief Slot: Sendet PINGREQ für Keep-Alive
 *
 * Wird vom Timer aufgerufen (alle 20 Sekunden).
 * Hält die Verbindung zum Broker aufrecht.
 */
void MqttClient::sendPingRequest()
{
    // Prüfen ob Verbindung noch besteht
    if (!m_connected || m_socket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "Kann PINGREQ nicht senden - nicht verbunden";
        m_keepAliveTimer->stop();
        return;
    }

    // PINGREQ-Paket erstellen und senden
    QByteArray packet = createPingRequestPacket();
    qint64 written = m_socket->write(packet);

    if (written == -1) {
        qDebug() << "Fehler beim Senden von PINGREQ";
        return;
    }

    m_socket->flush();
    qDebug() << "PINGREQ gesendet (Keep-Alive)";
}
