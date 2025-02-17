#ifndef ENERGYLOGS_H
#define ENERGYLOGS_H

#include "engine.h"

#include <QObject>
#include <QUuid>
#include <QQmlParserStatus>


class EnergyLogEntry: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QDateTime timestamp READ timestamp CONSTANT)
public:
    EnergyLogEntry(QObject *parent = nullptr);
    EnergyLogEntry(const QDateTime &timestamp, QObject *parent = nullptr);

    QDateTime timestamp() const;
private:
    QDateTime m_timestamp;

};

class EnergyLogs : public QAbstractListModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(Engine *engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(SampleRate sampleRate READ sampleRate WRITE setSampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(QDateTime startTime READ startTime WRITE setStartTime NOTIFY startTimeChanged)
    Q_PROPERTY(QDateTime endTime READ endTime WRITE setEndTime NOTIFY endTimeChanged)
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
    Q_PROPERTY(bool fetchingData READ fetchingData NOTIFY fetchingDataChanged)
    Q_PROPERTY(bool loadingInhibited READ loadingInhibited WRITE setLoadingInhibited NOTIFY loadingInhibitedChanged)

public:
    enum SampleRate {
        SampleRate1Min = 1,
        SampleRate15Mins = 15,
        SampleRate1Hour = 60,
        SampleRate3Hours = 180,
        SampleRate1Day = 1440,
        SampleRate1Week = 10080,
        SampleRate1Month = 43200,
        SampleRate1Year = 525600
    };
    Q_ENUM(SampleRate)

    explicit EnergyLogs(QObject *parent = nullptr);
    ~EnergyLogs();

    Engine *engine() const;
    void setEngine(Engine *engine);

    SampleRate sampleRate() const;
    void setSampleRate(SampleRate sampleRate);

    QDateTime startTime() const;
    void setStartTime(const QDateTime &startTime);

    QDateTime endTime() const;
    void setEndTime(const QDateTime &endTime);

    bool live() const;
    void setLive(bool live);

    bool fetchingData() const;

    bool loadingInhibited() const;
    void setLoadingInhibited(bool loadingInhibited);

    void classBegin() override;
    void componentComplete() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    Q_INVOKABLE EnergyLogEntry* get(int index) const;

signals:
    void engineChanged();
    void sampleRateChanged();
    void fetchPowerBalanceChanged();
    void thingIdsChanged();
    void startTimeChanged();
    void endTimeChanged();
    void liveChanged();
    void fetchingDataChanged();
    void loadingInhibitedChanged();

    void countChanged();
    void entryAdded(EnergyLogEntry *entry);
    void entriesAdded(const QList<EnergyLogEntry*> entries);

protected:
    virtual QString logsName() const = 0;
    virtual QVariantMap fetchParams() const;
    virtual void logEntriesReceived(const QVariantMap &params) = 0;
    virtual void notificationReceived(const QVariantMap &data) = 0;

    void appendEntry(EnergyLogEntry *entry);
    void appendEntries(const QList<EnergyLogEntry *> &entries);

private slots:
    void getLogsResponse(int commandId, const QVariantMap &params);
    void notificationReceivedInternal(const QVariantMap &data);

    void fetchLogs();
private:
    Engine *m_engine = nullptr;
    SampleRate m_sampleRate = SampleRate15Mins;
    bool m_fetchPowerBalance = true;
    QList<QUuid> m_thingIds;
    QDateTime m_startTime;
    QDateTime m_endTime;
    bool m_live = true;
    bool m_fetchingData = false;
    bool m_loadingInhibited = false;
    bool m_ready = false;

    QList<EnergyLogEntry*> m_list;
};

#endif // ENERGYLOGS_H
