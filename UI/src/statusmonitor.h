#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QVector>

struct WifiNetwork {
    QString ssid;
    QString bssid;
    int signal = 0;
    QString security;
    QString connectionId; // NetworkManager profile id/name when saved
    bool inUse = false;
    bool saved = false;
};

struct BtDevice {
    QString address;
    QString name;
    bool connected = false;
    bool paired = false;
};

class StatusMonitor : public QObject
{
    Q_OBJECT
public:
    explicit StatusMonitor(QObject *parent = nullptr);

    int batteryPercent() const { return m_batteryPercent; }
    bool batteryCharging() const { return m_batteryCharging; }
    bool wifiOn() const { return m_wifiOn; }
    bool wifiConnected() const { return m_wifiConnected; }
    int wifiSignal() const { return m_wifiSignal; }
    QString wifiSsid() const { return m_wifiSsid; }
    bool bluetoothOn() const { return m_bluetoothOn; }
    bool bluetoothConnected() const { return m_bluetoothConnected; }

    QVector<WifiNetwork> wifiNetworks() const { return m_wifiNetworks; }
    QVector<BtDevice> bluetoothDevices() const { return m_btDevices; }
    bool wifiScanning() const { return m_wifiScanning; }
    bool bluetoothScanning() const { return m_btScanning; }

public slots:
    void refresh();
    void toggleWifi();
    void toggleBluetooth();
    void powerAction(const QString &action);

    void scanWifi();
    void connectWifi(const QString &ssid, const QString &bssid, const QString &password = {},
                     const QString &username = {}, const QString &security = {});
    void disconnectWifi();

    void scanBluetooth();
    void connectBluetooth(const QString &address);
    void disconnectBluetooth(const QString &address);

signals:
    void changed();
    void wifiNetworksUpdated();
    void bluetoothDevicesUpdated();
    void wifiConnectFinished(bool ok, const QString &message);
    void bluetoothConnectFinished(bool ok, const QString &message);

private:
    void pollBattery();
    void pollWifi();
    void pollBluetooth();
    void finishBtScan();
    void loadSavedWifiProfiles();
    void tryAutoconnectSaved();
    QString connectionIdForSsid(const QString &ssid) const;

    int m_batteryPercent = 100;
    bool m_batteryCharging = false;
    bool m_wifiOn = false;
    bool m_wifiConnected = false;
    int m_wifiSignal = 0;
    QString m_wifiSsid;
    bool m_bluetoothOn = false;
    bool m_bluetoothConnected = false;

    QVector<WifiNetwork> m_wifiNetworks;
    QVector<BtDevice> m_btDevices;
    QHash<QString, QString> m_savedWifiBySsid; // ssid -> connection id
    bool m_wifiScanning = false;
    bool m_btScanning = false;
    bool m_suppressAutoconnect = false;
    bool m_pendingAutoconnect = true;
    bool m_autoconnecting = false;
    class QProcess *m_btScanProc = nullptr;
};
