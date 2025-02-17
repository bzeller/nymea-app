/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright 2013 - 2020, nymea GmbH
* Contact: contact@nymea.io
*
* This file is part of nymea.
* This project including source code and documentation is protected by
* copyright law, and remains the property of nymea GmbH. All rights, including
* reproduction, publication, editing and translation, are reserved. The use of
* this project is subject to the terms of a license agreement to be concluded
* with nymea GmbH in accordance with the terms of use of nymea GmbH, available
* under https://nymea.io/license
*
* GNU General Public License Usage
* Alternatively, this project may be redistributed and/or modified under the
* terms of the GNU General Public License as published by the Free Software
* Foundation, GNU version 3. This project is distributed in the hope that it
* will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
* Public License for more details.
*
* You should have received a copy of the GNU General Public License along with
* this project. If not, see <https://www.gnu.org/licenses/>.
*
* For any further details and any questions please contact us under
* contact@nymea.io or see our FAQ/Licensing Information on
* https://nymea.io/license/faq
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "jsonrpcclient.h"
#include "connection/nymeaconnection.h"
#include "types/param.h"
#include "types/params.h"

#include "connection/tcpsockettransport.h"
#include "connection/websockettransport.h"
#include "connection/bluetoothtransport.h"
#include "connection/cloudtransport.h"

#include <QJsonDocument>
#include <QVariantMap>
#include <QDebug>
#include <QUuid>
#include <QSettings>
#include <QVersionNumber>
#include <QMetaEnum>
#include <QLocale>
#include <QDir>
#include <QStandardPaths>

#include "logging.h"
NYMEA_LOGGING_CATEGORY(dcJsonRpc, "JsonRpc")

JsonRpcClient::JsonRpcClient(QObject *parent) :
    QObject(parent),
    m_id(0)
{
    m_connection = new NymeaConnection(this);
    m_connection->registerTransport(new TcpSocketTransportFactory());
    m_connection->registerTransport(new WebsocketTransportFactory());
    m_connection->registerTransport(new BluetoothTransportFactoy());
    m_connection->registerTransport(new CloudTransportFactory());

    connect(m_connection, &NymeaConnection::availableBearerTypesChanged, this, &JsonRpcClient::availableBearerTypesChanged);
    connect(m_connection, &NymeaConnection::connectionStatusChanged, this, &JsonRpcClient::connectionStatusChanged);
    connect(m_connection, &NymeaConnection::connectedChanged, this, &JsonRpcClient::onInterfaceConnectedChanged);
    connect(m_connection, &NymeaConnection::currentHostChanged, this, &JsonRpcClient:: currentHostChanged);
    connect(m_connection, &NymeaConnection::currentConnectionChanged, this, &JsonRpcClient:: currentConnectionChanged);
    // We'll connect this Queued, because in case of a disconnect we'll want to react on that ASAP instead of processing a queue that may be left in buffers
    // Especially on mobile platforms (hello Android) we get a huge queue of buffers upon resume from suspend just to get a disconnect after that.
    connect(m_connection, &NymeaConnection::dataAvailable, this, &JsonRpcClient::dataReceived, Qt::QueuedConnection);

    registerNotificationHandler(this, QStringLiteral("JSONRPC"), "notificationReceived");
}

void JsonRpcClient::registerNotificationHandler(QObject *handler, const QString &nameSpace, const QString &method)
{
    if (m_notificationHandlers.key(handler) == nameSpace) {
        qWarning() << "Notification handler" << handler << " already registered for namespace" << nameSpace;
        return;
    }
    m_notificationHandlers.insert(nameSpace, handler);
    m_notificationHandlerMethods.insert(handler, method);
    setNotificationsEnabled();
}

void JsonRpcClient::unregisterNotificationHandler(QObject *handler)
{
    foreach (const QString nameSpace, m_notificationHandlers.keys(handler)) {
        m_notificationHandlers.remove(nameSpace, handler);
    }
    m_notificationHandlerMethods.remove(handler);
    setNotificationsEnabled();
}

int JsonRpcClient::sendCommand(const QString &method, const QVariantMap &params, QObject *caller, const QString &callbackMethod)
{

    JsonRpcReply *reply = createReply(method, params, caller, callbackMethod);

    if (m_cacheHashes.contains(method)) {
        QString hash = m_cacheHashes.value(method);
        QString callSignature = method + '-' + QJsonDocument::fromVariant(params).toJson() + '-' + QLocale().name();
        QString callSignatureHash = QCryptographicHash::hash(callSignature.toUtf8(), QCryptographicHash::Md5).toHex();
        QFile f(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + '/' + method + '-' + callSignatureHash + '-' + hash + ".cache");
        if (f.exists() && f.open(QFile::ReadOnly)) {
            QJsonParseError error;
            QVariantMap cachedParams = QJsonDocument::fromJson(f.readAll(), &error).toVariant().toMap();
            f.close();
            if (error.error == QJsonParseError::NoError) {
                qDebug() << "Loaded results for" << reply->nameSpace() + '.' + reply->method() << "from cache";
                // We want to make sure this is an async operation even if we have stuff in cache, so only call callbacks using Qt::QueuedConnection
                if (!reply->caller().isNull() && !reply->callback().isEmpty()) {
                    QMetaObject::invokeMethod(reply->caller(), reply->callback().toLatin1().data(), Qt::QueuedConnection, Q_ARG(int, reply->commandId()), Q_ARG(QVariantMap, cachedParams));
                }
                QMetaObject::invokeMethod(this, "responseReceived", Qt::QueuedConnection, Q_ARG(int, reply->commandId()), Q_ARG(QVariantMap, cachedParams));
                QMetaObject::invokeMethod(reply, "deleteLater", Qt::QueuedConnection);
                return reply->commandId();
            }
        }
    }

    m_replies.insert(reply->commandId(), reply);
    sendRequest(reply->requestMap());
    return reply->commandId();
}

int JsonRpcClient::sendCommand(const QString &method, QObject *caller, const QString &callbackMethod)
{

    return sendCommand(method, QVariantMap(), caller, callbackMethod);
}

NymeaConnection::BearerTypes JsonRpcClient::availableBearerTypes() const
{
    return m_connection->availableBearerTypes();
}

NymeaConnection::ConnectionStatus JsonRpcClient::connectionStatus() const
{
    return m_connection->connectionStatus();
}

void JsonRpcClient::connectToHost(NymeaHost *host, Connection *connection)
{
    if (m_connection->currentHost()) {
        disconnect(m_connection->currentHost(), &NymeaHost::nameChanged, this, &JsonRpcClient::serverNameChanged);
    }

    m_connection->connectToHost(host, connection);

    connect(host, &NymeaHost::nameChanged, this, &JsonRpcClient::serverNameChanged);
    emit serverNameChanged();
}

void JsonRpcClient::disconnectFromHost()
{
    m_connection->disconnectFromHost();
}

void JsonRpcClient::acceptCertificate(const QString &serverUuid, const QByteArray &pem)
{
    qDebug() << "Pinning new certificate for" << serverUuid << pem;
    storePem(serverUuid, pem);
}

bool JsonRpcClient::tokenExists(const QString &serverUuid) const
{
    QSettings settings;
    settings.beginGroup("jsonTokens");
    return settings.contains(QUuid(serverUuid).toString());
}

void JsonRpcClient::getCloudConnectionStatus()
{
    JsonRpcReply *reply = createReply("JSONRPC.IsCloudConnected", QVariantMap(), this, "isCloudConnectedReply");
    m_replies.insert(reply->commandId(), reply);
    sendRequest(reply->requestMap());
}

void JsonRpcClient::setNotificationsEnabledResponse(int commandId, const QVariantMap &params)
{
    qCDebug(dcJsonRpc()) << "Notification configuration response:" << commandId << qUtf8Printable(QJsonDocument::fromVariant(params).toJson());

    if (!m_connected) {
        m_connected = true;
        emit connectedChanged(true);
    }
}

void JsonRpcClient::notificationReceived(const QVariantMap &data)
{
//    qDebug() << "Notification received:" << data;
    if (data.value("notification").toString() == "JSONRPC.PushButtonAuthFinished") {
        qDebug() << "Push button auth finished.";
        if (data.value("params").toMap().value("transactionId").toInt() != m_pendingPushButtonTransaction) {
            qDebug() << "This push button transaction is not what we're waiting for...";
            return;
        }
        m_pendingPushButtonTransaction = -1;
        if (data.value("params").toMap().value("success").toBool()) {
            qDebug() << "Push button auth succeeded";
            m_token = data.value("params").toMap().value("token").toByteArray();
            QSettings settings;
            settings.beginGroup("jsonTokens");
            settings.setValue(m_serverUuid, m_token);
            settings.endGroup();

            m_initialSetupRequired = false;

            emit authenticationRequiredChanged();

            setNotificationsEnabled();
        } else {
            emit pushButtonAuthFailed();
        }
        return;
    }

    if (data.value("notification").toString() == "JSONRPC.CloudConnectedChanged") {
        QMetaEnum connectionStateEnum = QMetaEnum::fromType<CloudConnectionState>();
        m_cloudConnectionState = static_cast<CloudConnectionState>(connectionStateEnum.keyToValue(data.value("params").toMap().value("connectionState").toByteArray().data()));
        emit cloudConnectionStateChanged();
        return;
    }

    qDebug() << "JsonRpcClient: Unhandled notification received" << data;
}

void JsonRpcClient::isCloudConnectedReply(int /*commandId*/, const QVariantMap &data)
{
    //    qDebug() << "Cloud is connected" << data;
    QMetaEnum connectionStateEnum = QMetaEnum::fromType<CloudConnectionState>();
    m_cloudConnectionState = static_cast<CloudConnectionState>(connectionStateEnum.keyToValue(data.value("connectionState").toByteArray().data()));
    emit cloudConnectionStateChanged();
}

void JsonRpcClient::setupRemoteAccessReply(int commandId, const QVariantMap &data)
{
    qDebug() << "Setup Remote Access reply" << commandId << data;
}

void JsonRpcClient::deployCertificateReply(int commandId, const QVariantMap &data)
{
    qDebug() << "deploy certificate reply:" << commandId << data;
}

void JsonRpcClient::getVersionsReply(int /*commandId*/, const QVariantMap &data)
{
    m_serverQtVersion = data.value("qtVersion").toString();
    m_serverQtBuildVersion = data.value("qtBuildVersion").toString();
    if (!m_serverQtVersion.isEmpty()) {
        emit serverQtVersionChanged();
    }
}

bool JsonRpcClient::connected() const
{
    return m_connected;
}

NymeaHost *JsonRpcClient::currentHost() const
{
    return m_connection->currentHost();
}

Connection *JsonRpcClient::currentConnection() const
{
    return m_connection->currentConnection();
}

QVariantMap JsonRpcClient::certificateIssuerInfo() const
{
    QVariantMap issuerInfo;
    foreach (const QByteArray &attr, m_connection->sslCertificate().issuerInfoAttributes()) {
        issuerInfo.insert(attr, m_connection->sslCertificate().issuerInfo(attr));
    }

    QByteArray certificateFingerprint;
    QByteArray digest = m_connection->sslCertificate().digest(QCryptographicHash::Sha256);
    for (int i = 0; i < digest.length(); i++) {
        if (certificateFingerprint.length() > 0) {
            certificateFingerprint.append(":");
        }
        certificateFingerprint.append(digest.mid(i,1).toHex().toUpper());
    }

    issuerInfo.insert("fingerprint", certificateFingerprint);

    return issuerInfo;
}

bool JsonRpcClient::initialSetupRequired() const
{
    return m_initialSetupRequired;
}

bool JsonRpcClient::authenticationRequired() const
{
    return m_authenticationRequired && m_token.isEmpty();
}

bool JsonRpcClient::pushButtonAuthAvailable() const
{
    return m_pushButtonAuthAvailable;
}

bool JsonRpcClient::authenticated() const
{
    return m_authenticated;
}

JsonRpcClient::CloudConnectionState JsonRpcClient::cloudConnectionState() const
{
    return m_cloudConnectionState;
}

void JsonRpcClient::deployCertificate(const QByteArray &rootCA, const QByteArray &certificate, const QByteArray &publicKey, const QByteArray &privateKey, const QString &endpoint)
{
    QVariantMap params;
    params.insert("rootCA", rootCA);
    params.insert("certificatePEM", certificate);
    params.insert("publicKey", publicKey);
    params.insert("privateKey", privateKey);
    params.insert("endpoint", endpoint);

    sendCommand("JSONRPC.SetupCloudConnection", params, this, "deployCertificateReply");
}

QHash<QString, QString> JsonRpcClient::cacheHashes() const
{
    return m_cacheHashes;
}

UserInfo::PermissionScopes JsonRpcClient::permissions() const
{
    return m_permissionScopes;
}

QString JsonRpcClient::serverVersion() const
{
    return m_serverVersion;
}

QString JsonRpcClient::jsonRpcVersion() const
{
    return m_jsonRpcVersion.toString();
}

QString JsonRpcClient::serverUuid() const
{
    return m_serverUuid;
}

QString JsonRpcClient::serverName() const
{
    return m_connection->currentHost() ? m_connection->currentHost()->name() : "";
}

QString JsonRpcClient::serverQtVersion()
{
    if (!m_serverQtVersion.isEmpty()) {
        return m_serverQtVersion;
    }
    if (ensureServerVersion("4.0")) {
        sendCommand("JSONRPC.Version", QVariantMap(), this, "getVersionsReply");
    }
    return QString();
}

QString JsonRpcClient::serverQtBuildVersion()
{
    return m_serverQtBuildVersion;
}

QVariantMap JsonRpcClient::experiences() const
{
    return m_experiences;
}

int JsonRpcClient::createUser(const QString &username, const QString &password)
{
    QVariantMap params;
    params.insert("username", username);
    params.insert("password", password);
    JsonRpcReply* reply = createReply("JSONRPC.CreateUser", params, this, "processCreateUser");
    m_replies.insert(reply->commandId(), reply);
    m_connection->sendData(QJsonDocument::fromVariant(reply->requestMap()).toJson());
    return reply->commandId();
}

int JsonRpcClient::authenticate(const QString &username, const QString &password, const QString &deviceName)
{
    QVariantMap params;
    params.insert("username", username);
    params.insert("password", password);
    params.insert("deviceName", deviceName);
    qDebug() << "Authenticating:" << username << password << deviceName;
    JsonRpcReply* reply = createReply("JSONRPC.Authenticate", params, this, "processAuthenticate");
    m_replies.insert(reply->commandId(), reply);
    m_connection->sendData(QJsonDocument::fromVariant(reply->requestMap()).toJson());
    return reply->commandId();
}

int JsonRpcClient::requestPushButtonAuth(const QString &deviceName)
{
    qDebug() << "Requesting push button auth for device:" << deviceName;
    QVariantMap params;
    params.insert("deviceName", deviceName);
    JsonRpcReply *reply = createReply("JSONRPC.RequestPushButtonAuth", params, this, "processRequestPushButtonAuth");
    m_replies.insert(reply->commandId(), reply);
    m_connection->sendData(QJsonDocument::fromVariant(reply->requestMap()).toJson());
    return reply->commandId();
}

int JsonRpcClient::setupRemoteAccess(const QString &idToken, const QString &userId)
{
    qDebug() << "Calling SetupRemoteAccess";
    QVariantMap params;
    params.insert("idToken", idToken);
    params.insert("userId", userId);
    return sendCommand("JSONRPC.SetupRemoteAccess", params, this, "setupRemoteAccessReply");
}

bool JsonRpcClient::ensureServerVersion(const QString &jsonRpcVersion)
{
    return QVersionNumber(m_jsonRpcVersion) >= QVersionNumber::fromString(jsonRpcVersion);
}

void JsonRpcClient::processAuthenticate(int /*commandId*/, const QVariantMap &data)
{
    if (data.value("success").toBool()) {
        qDebug() << "authentication successful";
        m_token = data.value("token").toByteArray();
        m_username = data.value("username").toString();
        if (m_jsonRpcVersion.majorVersion() >= 6) {
            m_permissionScopes = UserInfo::listToScopes(data.value("scopes").toStringList());
        } else {
            m_permissionScopes = UserInfo::PermissionScopeAdmin;
        }
        QSettings settings;
        settings.beginGroup("jsonTokens");
        settings.setValue(m_serverUuid, m_token);
        settings.endGroup();
        emit authenticationRequiredChanged();

        m_authenticated = true;
        emit authenticatedChanged();

        setNotificationsEnabled();
    } else {
        qWarning() << "Authentication failed" << data;
        emit authenticationFailed();
    }
}

void JsonRpcClient::processCreateUser(int /*commandId*/, const QVariantMap &data)
{
    qDebug() << "create user response:" << data;
    if (data.value("error").toString() == "UserErrorNoError") {
        emit createUserSucceeded();
        m_initialSetupRequired = false;
        emit initialSetupRequiredChanged();
    } else {
        qDebug() << "Emitting create user failed";
        emit createUserFailed(data.value("error").toString());
    }
}

void JsonRpcClient::processRequestPushButtonAuth(int /*commandId*/, const QVariantMap &data)
{
    qDebug() << "requestPushButtonAuth response" << data;
    if (data.value("success").toBool()) {
        m_pendingPushButtonTransaction = data.value("transactionId").toInt();
    } else {
        emit pushButtonAuthFailed();
    }
}

JsonRpcReply *JsonRpcClient::createReply(const QString &method, const QVariantMap &params, QObject* caller, const QString &callback)
{
    QStringList callParts = method.split('.');
    if (callParts.count() != 2) {
        qWarning() << "Invalid method. Must be Namespace.Method";
        return nullptr;
    }
    m_id++;
    return new JsonRpcReply(m_id, callParts.first(), callParts.last(), params, caller, callback);
}

void JsonRpcClient::setNotificationsEnabled()
{
    QStringList namespaces;
    foreach (const QString &nameSpace, m_notificationHandlers.keys()) {
        namespaces.append(nameSpace);
    }

    if (!m_connection->connected()) {
        return;
    }

    // We always want the Users notification to check for changed permissions
    if (!namespaces.contains("Users")) {
        namespaces.append("Users");
    }

    QVariantMap params;

    if (ensureServerVersion("3.1")) {
        params.insert("namespaces", namespaces);
    } else {
        params.insert("enabled", namespaces.count() > 0);
    }
    JsonRpcReply *reply = createReply("JSONRPC.SetNotificationStatus", params, this, "setNotificationsEnabledResponse");
    m_replies.insert(reply->commandId(), reply);
    sendRequest(reply->requestMap());
}

void JsonRpcClient::sendRequest(const QVariantMap &request)
{
    QVariantMap newRequest = request;
    newRequest.insert("token", m_token);
    //    qDebug() << "Sending request" << qUtf8Printable(QJsonDocument::fromVariant(newRequest).toJson());
    m_connection->sendData(QJsonDocument::fromVariant(newRequest).toJson(QJsonDocument::Compact) + "\n");
}

bool JsonRpcClient::loadPem(const QUuid &serverUud, QByteArray &pem)
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/sslcerts/");
    QFile certFile(dir.absoluteFilePath(serverUud.toString().remove(QRegExp("[{}]")) + ".pem"));
    if (!certFile.open(QFile::ReadOnly)) {
        return false;
    }
    pem.clear();
    pem.append(certFile.readAll());
    return true;
}

bool JsonRpcClient::storePem(const QUuid &serverUuid, const QByteArray &pem)
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/sslcerts/");
    if (!dir.exists()) {
        dir.mkpath(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/sslcerts/");
    }
    QFile certFile(dir.absoluteFilePath(serverUuid.toString().remove(QRegExp("[{}]")) + ".pem"));
    if (!certFile.open(QFile::WriteOnly | QFile::Truncate)) {
        return false;
    }
    certFile.write(pem);
    certFile.close();
    return true;
}

void JsonRpcClient::onInterfaceConnectedChanged(bool connected)
{

    if (!connected) {
        qCInfo(dcJsonRpc()) << "JsonRpcClient: Transport disconnected.";
        m_initialSetupRequired = false;
        m_authenticationRequired = false;
        m_authenticated = false;
        m_receiveBuffer.clear();
        m_serverQtVersion.clear();
        m_serverQtBuildVersion.clear();
        if (m_connected) {
            m_connected = false;
            emit connectedChanged(false);
        }
    } else {
        qCInfo(dcJsonRpc()) << "JsonRpcClient: Transport connected. Starting handshake.";
        // Clear anything that might be left in the buffer from a previous connection.
        m_receiveBuffer.clear();

        // Load token for this host
        QSettings settings;
        settings.beginGroup("jsonTokens");
        m_token = settings.value(currentHost()->uuid().toString()).toByteArray();
        settings.endGroup();


        QVariantMap params;
        params.insert("locale", QLocale().name());
        sendCommand("JSONRPC.Hello", params, this, "helloReply");
    }
}

void JsonRpcClient::dataReceived(const QByteArray &data)
{
    if (!m_connection->connected()) {
        // Given this slot is invoked with QueuedConnection, we might still get pending data packages after a disconnected event
        // In that case we can discard all pending packages as we'll have to reconnect anyways.
        return;
    }
    //    qDebug() << "JsonRpcClient: received data:" << qUtf8Printable(data);
    m_receiveBuffer.append(data);

    int splitIndex = m_receiveBuffer.indexOf("}\n{") + 1;
    if (splitIndex <= 0) {
        splitIndex = m_receiveBuffer.length();
    }
    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(m_receiveBuffer.left(splitIndex), &error);
    if (error.error != QJsonParseError::NoError) {
        //        qWarning() << "Could not parse json data from nymea" << m_receiveBuffer.left(splitIndex) << error.errorString();
        return;
    }
    //    qDebug() << "received response" << qUtf8Printable(jsonDoc.toJson(QJsonDocument::Indented));
    m_receiveBuffer = m_receiveBuffer.right(m_receiveBuffer.length() - splitIndex - 1);
    if (!m_receiveBuffer.isEmpty()) {
        staticMetaObject.invokeMethod(this, "dataReceived", Qt::QueuedConnection, Q_ARG(QByteArray, QByteArray()));
    }

    QVariantMap dataMap = jsonDoc.toVariant().toMap();

    // check if this is a notification
    if (dataMap.contains("notification")) {
        qCDebug(dcJsonRpc()) << "Incoming notification:" << qUtf8Printable(jsonDoc.toJson());
        // Check if our permissions changed
        if (dataMap.value("notification").toString() == "Users.UserChanged") {
            QVariantMap userMap = dataMap.value("params").toMap().value("userInfo").toMap();
            if (userMap.value("username").toString() == m_username) {
                m_permissionScopes = UserInfo::listToScopes(userMap.value("scopes").toStringList());
                qCritical() << "Permissions changed for" << userMap.value("username") << userMap.value("scopes").toStringList().join(",") << m_permissionScopes;
                qCritical() << "***" << (m_permissionScopes & UserInfo::PermissionScopeConfigureThings);
                emit permissionsChanged();
            }
        }
        QStringList notification = dataMap.value("notification").toString().split(".");
        QString nameSpace = notification.first();
        foreach (QObject *handler, m_notificationHandlers.values(nameSpace)) {
            QMetaObject::invokeMethod(handler, m_notificationHandlerMethods.value(handler).toLatin1().data(), Q_ARG(QVariantMap, dataMap));
        }
        return;
    }

    // check if this is a reply to a request
    int commandId = dataMap.value("id").toInt();
    JsonRpcReply *reply = m_replies.take(commandId);
    if (reply) {
        reply->deleteLater();
        //        qDebug() << QString("JsonRpc: got response for %1.%2: %3").arg(reply->nameSpace(), reply->method(), QString::fromUtf8(jsonDoc.toJson(QJsonDocument::Indented))) << reply->callback() << reply->callback();

        if (dataMap.value("status").toString() == "unauthorized") {
            qWarning() << "Something's off with the token";
            m_authenticationRequired = true;
            m_token.clear();
            QSettings settings;
            settings.beginGroup("jsonTokens");
            settings.setValue(m_serverUuid, m_token);
            settings.endGroup();
            emit authenticationRequiredChanged();
            m_authenticated = false;
            emit authenticatedChanged();
        }

        if (dataMap.value("status").toString() == "error") {
            qWarning() << "An error happened in the JSONRPC layer:" << dataMap.value("error").toString();
            qWarning() << "Request was:" << qUtf8Printable(QJsonDocument::fromVariant(reply->requestMap()).toJson());
            if (reply->nameSpace() == "JSONRPC" && reply->method() == "Hello") {
                qWarning() << "Hello call failed. Trying again without locale";
                m_id = 0;
                sendCommand("JSONRPC.Hello", QVariantMap(), this, "helloReply");
            }
        }
        // Note: We're still forwarding a failed call, params will be empty tho...
        // This should never really happen as errors on this layer indicate a but in the caller code in the first place
        // Some methods however, like authenticate might fail on an invalid token tho and stil need to act on it

        if (!reply->caller().isNull() && !reply->callback().isEmpty()) {
            QMetaObject::invokeMethod(reply->caller(), reply->callback().toLatin1().data(), Q_ARG(int, commandId), Q_ARG(QVariantMap, dataMap.value("params").toMap()));
        }

        emit responseReceived(reply->commandId(), dataMap.value("params").toMap());


        // If the server supports cache hashes, cache stuff locally
        QString fullMethod = reply->nameSpace() + '.' + reply->method();
        if (m_cacheHashes.contains(fullMethod)) {
            QString hash = m_cacheHashes.value(fullMethod);
            QString callSignature = fullMethod + '-' + QJsonDocument::fromVariant(reply->params()).toJson() + '-' + QLocale().name();
            QString callSignatureHash = QCryptographicHash::hash(callSignature.toUtf8(), QCryptographicHash::Md5).toHex();
            QFile f(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + '/' + fullMethod + '-' + callSignatureHash + '-' + hash + ".cache");
            if (!f.exists() && f.open(QFile::WriteOnly | QFile::Truncate)) {
                f.write(QJsonDocument::fromVariant(dataMap.value("params")).toJson());
                f.close();
            }
        }

        return;
    }
}

void JsonRpcClient::helloReply(int /*commandId*/, const QVariantMap &params)
{
    m_initialSetupRequired = params.value("initialSetupRequired").toBool();
    m_authenticationRequired = params.value("authenticationRequired").toBool();
    m_pushButtonAuthAvailable = params.value("pushButtonAuthAvailable").toBool();
    emit pushButtonAuthAvailableChanged();

    m_serverUuid = params.value("uuid").toString();
    m_serverVersion = params.value("version").toString();
    m_experiences.clear();
    foreach (const QVariant &experience, params.value("experiences").toList()) {
        m_experiences.insert(experience.toMap().value("name").toString(), experience.toMap().value("version").toString());
    }

    QString protoVersionString = params.value("protocol version").toString();
    if (!protoVersionString.contains('.')) {
        protoVersionString.prepend("0.");
    }

    m_jsonRpcVersion = QVersionNumber::fromString(protoVersionString);

    qCInfo(dcJsonRpc()) << "Handshake reply:" << "Protocol version:" << protoVersionString << "InitRequired:" << m_initialSetupRequired << "AuthRequired:" << m_authenticationRequired << "PushButtonAvailable:" << m_pushButtonAuthAvailable;

    QVersionNumber minimumRequiredVersion = QVersionNumber(5, 0);
    QVersionNumber maximumMajorVersion = QVersionNumber(6);
    if (m_jsonRpcVersion < minimumRequiredVersion) {
        qCWarning(dcJsonRpc()) << "Nymea core doesn't support minimum required version. Required:" << minimumRequiredVersion << "Found:" << m_jsonRpcVersion;
        emit invalidMinimumVersion(m_jsonRpcVersion.toString(), minimumRequiredVersion.toString());
        return;
    }
    if (m_jsonRpcVersion.majorVersion() > maximumMajorVersion.majorVersion()) {
        qCWarning(dcJsonRpc()) << "Nymea core has breaking API changes not supported by this app version. Core major version:" << m_jsonRpcVersion.majorVersion() << "Maximum supported major version:" << maximumMajorVersion.majorVersion();
        emit invalidMaximumVersion(m_jsonRpcVersion.toString(), QString("%1.x").arg(maximumMajorVersion.majorVersion()));
        return;
    }


    // Verify SSL certificate
    if (m_connection->isEncrypted()) {
        QByteArray oldPem;
        QSslCertificate certificate = m_connection->sslCertificate();
        if (!loadPem(m_serverUuid, oldPem)) {
            qCInfo(dcJsonRpc()) << "No SSL certificate for this host stored. Accepting and pinning new certificate.";
            // No certificate yet! Inform ui about it.
            emit newSslCertificate();
            storePem(m_serverUuid, m_connection->sslCertificate().toPem());
        } else {
            // We have a certificate pinned already. Check if it's the same
            if (certificate.toPem() != oldPem) {
                // Uh oh, the certificate has changed
                qCWarning(dcJsonRpc()) << "This connections certificate has changed!";
                qCWarning(dcJsonRpc()) << "Old PEM:" << oldPem;
                qCWarning(dcJsonRpc()) << "New PEM:" << certificate.toPem();

                // Extract certificate info before disconnecting.
                QVariantMap issuerInfo = certificateIssuerInfo();

                // Reject the connection until the UI explicitly accepts this...
                m_connection->disconnectFromHost();

                emit verifyConnectionCertificate(m_serverUuid, issuerInfo, certificate.toPem());
                return;
            }
            qCInfo(dcJsonRpc()) << "This connections certificate is trusted.";
        }
    }

    m_cacheHashes.clear();
    qCDebug(dcJsonRpc()) << "Hello reply:" << qUtf8Printable(QJsonDocument::fromVariant(params).toJson());
    QVariantList cacheHashes = params.value("cacheHashes").toList();
    foreach (const QVariant &cacheHash, cacheHashes) {
        m_cacheHashes.insert(cacheHash.toMap().value("method").toString(), cacheHash.toMap().value("hash").toString());
    }
//    qDebug() << "Caches:" << m_cacheHashes;

    if (m_jsonRpcVersion.majorVersion() >= 6) {
        m_permissionScopes = UserInfo::listToScopes(params.value("permissionScopes").toStringList());
    } else {
        m_permissionScopes = UserInfo::PermissionScopeAdmin;
    }
    m_username = params.value("username").toString();
    qCInfo(dcJsonRpc()) << "User:" << m_username << "Permissions:" << UserInfo::scopesToList(m_permissionScopes);
    emit permissionsChanged();

    emit handshakeReceived();

    if (m_connection->currentHost()->uuid().isNull()) {
        qCDebug(dcJsonRpc()) << "Updating Server UUID in connection:" << m_connection->currentHost()->uuid().toString() << "->" << m_serverUuid;
        m_connection->currentHost()->setUuid(m_serverUuid);
    }

    if (m_initialSetupRequired) {
        qCInfo(dcJsonRpc()) << "Initial setup is required for this nymea instance!";
        emit initialSetupRequiredChanged();
        return;
    }

    if (m_authenticationRequired) {
        // Reload the token, now that we're certain about the server uuid.
        QSettings settings;
        settings.beginGroup("jsonTokens");
        m_token = settings.value(m_serverUuid).toByteArray();
        settings.endGroup();
        emit authenticationRequiredChanged();

        if (m_token.isEmpty()) {
            return;
        }

        m_authenticated = true;
        qCInfo(dcJsonRpc()) << "Authenticated to nymea instance.";
        emit authenticatedChanged();
    }

    setNotificationsEnabled();
    getCloudConnectionStatus();

}

JsonRpcReply::JsonRpcReply(int commandId, QString nameSpace, QString method, QVariantMap params, QPointer<QObject> caller, const QString &callback):
    m_commandId(commandId),
    m_nameSpace(nameSpace),
    m_method(method),
    m_params(params),
    m_caller(caller),
    m_callback(callback)
{
}

JsonRpcReply::~JsonRpcReply()
{
}

int JsonRpcReply::commandId() const
{
    return m_commandId;
}

QString JsonRpcReply::nameSpace() const
{
    return m_nameSpace;
}

QString JsonRpcReply::method() const
{
    return m_method;
}

QVariantMap JsonRpcReply::params() const
{
    return m_params;
}

QVariantMap JsonRpcReply::requestMap()
{
    QVariantMap request;
    request.insert("id", m_commandId);
    request.insert("method", m_nameSpace + "." + m_method);
    if (!m_params.isEmpty())
        request.insert("params", m_params);

    return request;
}

QPointer<QObject> JsonRpcReply::caller() const
{
    return m_caller;
}

QString JsonRpcReply::callback() const
{
    return m_callback;
}
