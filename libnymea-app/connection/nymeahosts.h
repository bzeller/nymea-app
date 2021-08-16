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

#ifndef NYMEAHOSTS_H
#define NYMEAHOSTS_H

#include <QAbstractListModel>
#include <QList>
#include <QBluetoothAddress>
#include <QSortFilterProxyModel>
#include "nymeahost.h"
class JsonRpcClient;

class NymeaHosts : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum HostRole {
        UuidRole,
        NameRole,
        VersionRole
    };
    Q_ENUM(HostRole)

    explicit NymeaHosts(QObject *parent = nullptr);

    int rowCount(const QModelIndex & parent = QModelIndex()) const;
    QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const;

    void addHost(NymeaHost *host);
    void removeHost(NymeaHost *host);
    Q_INVOKABLE NymeaHost* createLanHost(const QString &name, const QUrl &url);
    Q_INVOKABLE NymeaHost* createWanHost(const QString &name, const QUrl &url);
    Q_INVOKABLE NymeaHost* createCloudHost(const QString &name, const QUrl &url);
    NymeaHost* createHost(const QString &name, const QUrl &url, Connection::BearerType bearerType);

    Q_INVOKABLE NymeaHost *get(int index) const;
    Q_INVOKABLE NymeaHost *find(const QUuid &uuid);

    void clearModel();

signals:
    void hostAdded(NymeaHost* host);
    void hostRemoved(NymeaHost* host);
    void countChanged();
    void hostChanged();

protected:
    QHash<int, QByteArray> roleNames() const;

private:
    QList<NymeaHost*> m_hosts;
};

#endif // NYMEAHOSTS_H
