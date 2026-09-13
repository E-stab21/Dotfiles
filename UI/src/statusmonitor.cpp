#include "statusmonitor.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll().trimmed();
}

QByteArray run(const QString &program, const QStringList &args, int timeoutMs = 4000)
{
    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(500);
        return {};
    }
    return proc.readAllStandardOutput().trimmed();
}

QStringList splitEscaped(const QByteArray &line)
{
    QStringList out;
    QString cur;
    bool esc = false;
    const QString s = QString::fromUtf8(line);
    for (QChar c : s) {
        if (esc) {
            cur += c;
            esc = false;
            continue;
        }
        if (c == QLatin1Char('\\')) {
            esc = true;
            continue;
        }
        if (c == QLatin1Char(':')) {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    out.push_back(cur);
    return out;
}

QString wifiResultMessage(bool ok, const QString &fallbackFail)
{
    return ok ? QStringLiteral("Connected") : fallbackFail;
}

} // namespace

StatusMonitor::StatusMonitor(QObject *parent)
    : QObject(parent)
{
    refresh();
    auto *timer = new QTimer(this);
    timer->setInterval(2500);
    connect(timer, &QTimer::timeout, this, &StatusMonitor::refresh);
    timer->start();
    // Scan once shortly after start so saved networks can autoconnect.
    QTimer::singleShot(800, this, &StatusMonitor::scanWifi);
}

void StatusMonitor::refresh()
{
    const int oldPct = m_batteryPercent;
    const bool oldChg = m_batteryCharging;
    const bool oldWifi = m_wifiOn;
    const bool oldWifiConn = m_wifiConnected;
    const int oldSig = m_wifiSignal;
    const QString oldSsid = m_wifiSsid;
    const bool oldBt = m_bluetoothOn;
    const bool oldBtConn = m_bluetoothConnected;

    pollBattery();
    pollWifi();
    pollBluetooth();

    if (oldPct != m_batteryPercent || oldChg != m_batteryCharging || oldWifi != m_wifiOn
        || oldWifiConn != m_wifiConnected || oldSig != m_wifiSignal || oldSsid != m_wifiSsid
        || oldBt != m_bluetoothOn || oldBtConn != m_bluetoothConnected) {
        emit changed();
    }
}

void StatusMonitor::pollBattery()
{
    const QDir dir(QStringLiteral("/sys/class/power_supply"));
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!name.startsWith(QLatin1String("BAT")))
            continue;
        const QString base = dir.filePath(name);
        bool ok = false;
        const int pct = QString::fromUtf8(readFile(base + QStringLiteral("/capacity"))).toInt(&ok);
        if (ok)
            m_batteryPercent = qBound(0, pct, 100);
        const QString status = QString::fromUtf8(readFile(base + QStringLiteral("/status")));
        m_batteryCharging = status.contains(QLatin1String("Charging"), Qt::CaseInsensitive)
            || status.contains(QLatin1String("Full"), Qt::CaseInsensitive);
        return;
    }
}

void StatusMonitor::pollWifi()
{
    const QByteArray radio = run(QStringLiteral("nmcli"),
                                 {QStringLiteral("-t"), QStringLiteral("-f"),
                                  QStringLiteral("WIFI"), QStringLiteral("general")});
    m_wifiOn = radio.contains("enabled");

    const QByteArray active = run(QStringLiteral("nmcli"),
                                  {QStringLiteral("-t"), QStringLiteral("-f"),
                                   QStringLiteral("TYPE,STATE,CONNECTION"),
                                   QStringLiteral("device")});
    m_wifiConnected = false;
    m_wifiSsid.clear();
    m_wifiSignal = 0;

    for (const QByteArray &line : active.split('\n')) {
        const QStringList parts = splitEscaped(line);
        if (parts.size() < 3)
            continue;
        if (parts[0] != QLatin1String("wifi"))
            continue;
        if (parts[1].startsWith(QLatin1String("connected"))) {
            m_wifiConnected = true;
            m_wifiSsid = parts[2];
        }
    }

    if (m_wifiConnected) {
        const QByteArray sig = run(QStringLiteral("nmcli"),
                                   {QStringLiteral("-t"), QStringLiteral("-f"),
                                    QStringLiteral("IN-USE,SIGNAL"), QStringLiteral("device"),
                                    QStringLiteral("wifi"), QStringLiteral("list"),
                                    QStringLiteral("--rescan"), QStringLiteral("no")});
        for (const QByteArray &line : sig.split('\n')) {
            if (!line.startsWith('*'))
                continue;
            const QStringList parts = splitEscaped(line);
            if (parts.size() >= 2)
                m_wifiSignal = qBound(0, parts[1].toInt(), 100);
            break;
        }
    }
}

void StatusMonitor::pollBluetooth()
{
    const QByteArray show = run(QStringLiteral("bluetoothctl"), {QStringLiteral("show")});
    m_bluetoothOn = show.contains("Powered: yes");

    const QByteArray info = run(QStringLiteral("bluetoothctl"),
                                {QStringLiteral("devices"), QStringLiteral("Connected")});
    m_bluetoothConnected = !info.trimmed().isEmpty() && !info.contains("No default controller");
}

void StatusMonitor::toggleWifi()
{
    const bool turningOn = !m_wifiOn;
    if (turningOn) {
        m_suppressAutoconnect = false;
        m_pendingAutoconnect = true;
    }
    run(QStringLiteral("nmcli"),
        {QStringLiteral("radio"), QStringLiteral("wifi"),
         m_wifiOn ? QStringLiteral("off") : QStringLiteral("on")});
    QTimer::singleShot(400, this, [this, turningOn]() {
        refresh();
        if (turningOn)
            scanWifi();
    });
}

void StatusMonitor::toggleBluetooth()
{
    run(QStringLiteral("bluetoothctl"),
        {QStringLiteral("power"), m_bluetoothOn ? QStringLiteral("off") : QStringLiteral("on")});
    QTimer::singleShot(400, this, &StatusMonitor::refresh);
}

void StatusMonitor::powerAction(const QString &action)
{
    if (action == QLatin1String("lock")) {
        QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
    } else if (action == QLatin1String("logout")) {
        QProcess::startDetached(QStringLiteral("hyprctl"),
                                {QStringLiteral("dispatch"), QStringLiteral("exit")});
    } else if (action == QLatin1String("reboot")) {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
    } else if (action == QLatin1String("shutdown")) {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
    }
}

void StatusMonitor::loadSavedWifiProfiles()
{
    m_savedWifiBySsid.clear();
    const QByteArray list = run(QStringLiteral("nmcli"),
                                {QStringLiteral("-t"), QStringLiteral("-f"),
                                 QStringLiteral("NAME,TYPE"), QStringLiteral("connection"),
                                 QStringLiteral("show")});
    for (const QByteArray &line : list.split('\n')) {
        if (line.isEmpty())
            continue;
        const QStringList parts = splitEscaped(line);
        if (parts.size() < 2)
            continue;
        if (parts[1] != QLatin1String("802-11-wireless"))
            continue;
        const QString conName = parts[0];
        const QByteArray ssidOut =
            run(QStringLiteral("nmcli"),
                {QStringLiteral("-g"), QStringLiteral("802-11-wireless.ssid"),
                 QStringLiteral("connection"), QStringLiteral("show"), QStringLiteral("id"),
                 conName});
        const QString ssid = QString::fromUtf8(ssidOut).trimmed();
        if (ssid.isEmpty())
            continue;
        m_savedWifiBySsid.insert(ssid, conName);
    }
}

QString StatusMonitor::connectionIdForSsid(const QString &ssid) const
{
    return m_savedWifiBySsid.value(ssid);
}

void StatusMonitor::tryAutoconnectSaved()
{
    if (m_suppressAutoconnect || m_autoconnecting || !m_pendingAutoconnect || !m_wifiOn
        || m_wifiConnected)
        return;

    const WifiNetwork *best = nullptr;
    for (const WifiNetwork &net : m_wifiNetworks) {
        if (!net.saved || net.inUse)
            continue;
        if (net.ssid.isEmpty() || net.ssid == QLatin1String("(hidden)"))
            continue;
        if (!best || net.signal > best->signal)
            best = &net;
    }
    if (!best)
        return;

    m_pendingAutoconnect = false;
    m_autoconnecting = true;
    const QString conId = best->connectionId;
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        m_autoconnecting = false;
        proc->deleteLater();
        refresh();
        // pendingAutoconnect is already false, so this refresh scan won't loop.
        scanWifi();
        const bool ok = code == 0;
        emit wifiConnectFinished(ok, wifiResultMessage(ok, QStringLiteral("Autoconnect failed")));
        if (ok)
            QTimer::singleShot(1800, this, &StatusMonitor::checkCaptivePortal);
    });
    proc->start(QStringLiteral("nmcli"),
                {QStringLiteral("-w"), QStringLiteral("30"), QStringLiteral("connection"),
                 QStringLiteral("up"), QStringLiteral("id"), conId});
}

void StatusMonitor::scanWifi()
{
    if (m_wifiScanning)
        return;

    if (!m_wifiOn) {
        m_wifiScanning = true;
        emit wifiNetworksUpdated();
        run(QStringLiteral("nmcli"),
            {QStringLiteral("radio"), QStringLiteral("wifi"), QStringLiteral("on")});
        QTimer::singleShot(700, this, [this]() {
            m_wifiScanning = false;
            refresh();
            scanWifi();
        });
        return;
    }

    m_wifiScanning = true;
    emit wifiNetworksUpdated();

    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int, QProcess::ExitStatus) {
        proc->deleteLater();

        loadSavedWifiProfiles();

        QVector<WifiNetwork> networks;
        QSet<QString> seen;
        const QByteArray out = proc->readAllStandardOutput().trimmed();
        for (const QByteArray &line : out.split('\n')) {
            if (line.isEmpty())
                continue;
            const QStringList parts = splitEscaped(line);
            if (parts.size() < 5)
                continue;

            WifiNetwork net;
            net.inUse = parts[0] == QLatin1String("*");
            net.bssid = parts[1];
            net.ssid = parts[2];
            net.signal = qBound(0, parts[3].toInt(), 100);
            net.security = parts[4];
            if (net.ssid.isEmpty())
                net.ssid = QStringLiteral("(hidden)");
            net.connectionId = connectionIdForSsid(net.ssid);
            net.saved = !net.connectionId.isEmpty();

            const QString key = net.ssid + QLatin1Char('|') + net.bssid;
            if (seen.contains(key))
                continue;
            seen.insert(key);
            networks.push_back(net);
        }

        std::sort(networks.begin(), networks.end(), [](const WifiNetwork &a, const WifiNetwork &b) {
            if (a.inUse != b.inUse)
                return a.inUse;
            if (a.saved != b.saved)
                return a.saved;
            return a.signal > b.signal;
        });

        m_wifiNetworks = networks;
        m_wifiScanning = false;
        refresh();
        emit wifiNetworksUpdated();
        tryAutoconnectSaved();
    });

    proc->start(QStringLiteral("nmcli"),
                {QStringLiteral("-t"), QStringLiteral("-f"),
                 QStringLiteral("IN-USE,BSSID,SSID,SIGNAL,SECURITY"), QStringLiteral("device"),
                 QStringLiteral("wifi"), QStringLiteral("list"), QStringLiteral("--rescan"),
                 QStringLiteral("yes")});
}

void StatusMonitor::connectWifi(const QString &ssid, const QString &bssid, const QString &password,
                                const QString &username, const QString &security)
{
    m_suppressAutoconnect = false;
    loadSavedWifiProfiles();

    const bool enterprise = !username.isEmpty()
        || security.contains(QLatin1String("802.1X"), Qt::CaseInsensitive)
        || security.contains(QLatin1String("Enterprise"), Qt::CaseInsensitive)
        || (security.contains(QLatin1String("EAP"), Qt::CaseInsensitive)
            && !security.contains(QLatin1String("SAE"), Qt::CaseInsensitive));

    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        m_autoconnecting = false;
        proc->deleteLater();
        refresh();
        scanWifi();
        const bool ok = code == 0;
        emit wifiConnectFinished(ok, wifiResultMessage(ok, QStringLiteral("Connection failed")));
        if (ok) {
            // Portal detection lags association/DHCP by a moment.
            QTimer::singleShot(1800, this, &StatusMonitor::checkCaptivePortal);
        }
    });

    // Prefer activating an existing NetworkManager profile when no new secrets given.
    if (password.isEmpty() && username.isEmpty()) {
        const QString savedId = connectionIdForSsid(ssid);
        if (!savedId.isEmpty()) {
            m_autoconnecting = true;
            proc->start(QStringLiteral("nmcli"),
                        {QStringLiteral("-w"), QStringLiteral("30"), QStringLiteral("connection"),
                         QStringLiteral("up"), QStringLiteral("id"), savedId});
            return;
        }
    }

    if (enterprise) {
        // WPA-Enterprise (PEAP/MSCHAPv2) — most common campus/corporate profile.
        const QString conName = ssid.isEmpty() || ssid == QLatin1String("(hidden)")
            ? QStringLiteral("hypr-pills-%1").arg(bssid)
            : ssid;
        run(QStringLiteral("nmcli"),
            {QStringLiteral("connection"), QStringLiteral("delete"), QStringLiteral("id"), conName},
            3000);

        QStringList args{
            QStringLiteral("-w"),
            QStringLiteral("30"),
            QStringLiteral("connection"),
            QStringLiteral("add"),
            QStringLiteral("type"),
            QStringLiteral("wifi"),
            QStringLiteral("con-name"),
            conName,
            QStringLiteral("ifname"),
            QStringLiteral("*"),
            QStringLiteral("ssid"),
            (ssid.isEmpty() || ssid == QLatin1String("(hidden)")) ? QString() : ssid,
            QStringLiteral("connection.autoconnect"),
            QStringLiteral("yes"),
            QStringLiteral("wifi-sec.key-mgmt"),
            QStringLiteral("wpa-eap"),
            QStringLiteral("802-1x.eap"),
            QStringLiteral("peap"),
            QStringLiteral("802-1x.phase2-auth"),
            QStringLiteral("mschapv2"),
            QStringLiteral("802-1x.identity"),
            username,
            QStringLiteral("802-1x.password"),
            password,
        };
        if (!bssid.isEmpty())
            args << QStringLiteral("802-11-wireless.bssid") << bssid;

        QProcess add;
        add.start(QStringLiteral("nmcli"), args);
        if (!add.waitForFinished(8000) || add.exitCode() != 0) {
            proc->deleteLater();
            emit wifiConnectFinished(false, QStringLiteral("Failed to create connection"));
            return;
        }
        m_autoconnecting = true;
        proc->start(QStringLiteral("nmcli"),
                    {QStringLiteral("-w"), QStringLiteral("30"), QStringLiteral("connection"),
                     QStringLiteral("up"), QStringLiteral("id"), conName});
        return;
    }

    QStringList args{QStringLiteral("device"), QStringLiteral("wifi"), QStringLiteral("connect")};
    if (!ssid.isEmpty() && ssid != QLatin1String("(hidden)"))
        args << ssid;
    else
        args << bssid;

    if (!password.isEmpty())
        args << QStringLiteral("password") << password;
    if (!bssid.isEmpty())
        args << QStringLiteral("bssid") << bssid;

    m_autoconnecting = true;
    proc->start(QStringLiteral("nmcli"), args);
}

void StatusMonitor::disconnectWifi()
{
    m_suppressAutoconnect = true;
    m_pendingAutoconnect = false;
    m_autoconnecting = false;
    run(QStringLiteral("nmcli"), {QStringLiteral("device"), QStringLiteral("disconnect"),
                                  QStringLiteral("wlan0")});
    // Fallback: disconnect whatever wifi device is active
    const QByteArray devices = run(QStringLiteral("nmcli"),
                                   {QStringLiteral("-t"), QStringLiteral("-f"),
                                    QStringLiteral("DEVICE,TYPE,STATE"), QStringLiteral("device")});
    for (const QByteArray &line : devices.split('\n')) {
        const QStringList parts = splitEscaped(line);
        if (parts.size() < 3)
            continue;
        if (parts[1] == QLatin1String("wifi") && parts[2].startsWith(QLatin1String("connected")))
            run(QStringLiteral("nmcli"),
                {QStringLiteral("device"), QStringLiteral("disconnect"), parts[0]});
    }
    refresh();
    scanWifi();
}

void StatusMonitor::checkCaptivePortal()
{
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [proc](int, QProcess::ExitStatus) {
        const QString state = QString::fromUtf8(proc->readAllStandardOutput()).trimmed().toLower();
        proc->deleteLater();
        // NetworkManager: none | portal | limited | full | unknown
        if (state != QLatin1String("portal"))
            return;
        // HTTP probe URL — the portal redirects the browser to its login page.
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("http://detectportal.firefox.com/canonical.html")));
    });
    proc->start(QStringLiteral("nmcli"),
                {QStringLiteral("networking"), QStringLiteral("connectivity"),
                 QStringLiteral("check")});
}

void StatusMonitor::scanBluetooth()
{
    if (m_btScanning)
        return;

    if (!m_bluetoothOn) {
        run(QStringLiteral("bluetoothctl"), {QStringLiteral("power"), QStringLiteral("on")});
        QTimer::singleShot(500, this, [this]() {
            refresh();
            scanBluetooth();
        });
        return;
    }

    m_btScanning = true;
    emit bluetoothDevicesUpdated();

    run(QStringLiteral("bluetoothctl"), {QStringLiteral("scan"), QStringLiteral("off")});
    m_btScanProc = new QProcess(this);
    m_btScanProc->start(QStringLiteral("bluetoothctl"), {QStringLiteral("scan"), QStringLiteral("on")});

    QTimer::singleShot(5500, this, &StatusMonitor::finishBtScan);
}

void StatusMonitor::finishBtScan()
{
    if (m_btScanProc) {
        m_btScanProc->kill();
        m_btScanProc->waitForFinished(500);
        m_btScanProc->deleteLater();
        m_btScanProc = nullptr;
    }
    run(QStringLiteral("bluetoothctl"), {QStringLiteral("scan"), QStringLiteral("off")});

    QSet<QString> connected;
    for (const QByteArray &line :
         run(QStringLiteral("bluetoothctl"), {QStringLiteral("devices"), QStringLiteral("Connected")})
             .split('\n')) {
        // Device AA:BB:CC:DD:EE:FF Name
        if (!line.startsWith("Device "))
            continue;
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() >= 2)
            connected.insert(QString::fromUtf8(parts[1]));
    }

    QSet<QString> paired;
    for (const QByteArray &line :
         run(QStringLiteral("bluetoothctl"), {QStringLiteral("devices"), QStringLiteral("Paired")})
             .split('\n')) {
        if (!line.startsWith("Device "))
            continue;
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() >= 2)
            paired.insert(QString::fromUtf8(parts[1]));
    }

    QVector<BtDevice> devices;
    QSet<QString> seen;
    for (const QByteArray &line : run(QStringLiteral("bluetoothctl"), {QStringLiteral("devices")}).split('\n')) {
        if (!line.startsWith("Device "))
            continue;
        const QByteArray rest = line.mid(7);
        const int sp = rest.indexOf(' ');
        if (sp <= 0)
            continue;
        BtDevice dev;
        dev.address = QString::fromUtf8(rest.left(sp));
        dev.name = QString::fromUtf8(rest.mid(sp + 1)).trimmed();
        if (dev.name.isEmpty())
            dev.name = dev.address;
        if (seen.contains(dev.address))
            continue;
        seen.insert(dev.address);
        dev.connected = connected.contains(dev.address);
        dev.paired = paired.contains(dev.address);
        devices.push_back(dev);
    }

    std::sort(devices.begin(), devices.end(), [](const BtDevice &a, const BtDevice &b) {
        if (a.connected != b.connected)
            return a.connected;
        if (a.paired != b.paired)
            return a.paired;
        return a.name.toLower() < b.name.toLower();
    });

    m_btDevices = devices;
    m_btScanning = false;
    refresh();
    emit bluetoothDevicesUpdated();
}

void StatusMonitor::connectBluetooth(const QString &address)
{
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc, address](int code, QProcess::ExitStatus) {
        proc->deleteLater();

        if (code != 0) {
            // Try pair then connect for new devices
            run(QStringLiteral("bluetoothctl"), {QStringLiteral("pair"), address}, 15000);
            run(QStringLiteral("bluetoothctl"), {QStringLiteral("trust"), address});
            const QByteArray again =
                run(QStringLiteral("bluetoothctl"), {QStringLiteral("connect"), address}, 15000);
            const bool ok = again.contains("Connection successful") || again.contains("successful");
            refresh();
            scanBluetooth();
            emit bluetoothConnectFinished(ok, ok ? QStringLiteral("Connected")
                                                 : QStringLiteral("Connection failed"));
            return;
        }

        refresh();
        scanBluetooth();
        emit bluetoothConnectFinished(true, QStringLiteral("Connected"));
    });
    proc->start(QStringLiteral("bluetoothctl"), {QStringLiteral("connect"), address});
}

void StatusMonitor::disconnectBluetooth(const QString &address)
{
    run(QStringLiteral("bluetoothctl"), {QStringLiteral("disconnect"), address}, 8000);
    refresh();
    scanBluetooth();
}
