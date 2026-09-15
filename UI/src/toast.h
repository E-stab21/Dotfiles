#pragma once

#include <QHash>
#include <QIcon>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QWidget>

class QLabel;
class QTimer;
class QToolButton;
class QVariantAnimation;
class QScreen;
class NotificationsAdaptor;

struct ToastData {
    uint id = 0;
    QString appName;
    QString appIcon;
    QString summary;
    QString body;
    QStringList actions;
    QVariantMap hints;
    int expireMs = 6000; // -1 = never
    QIcon icon;
};

// Owns the Freedesktop notification bus name and the bottom-right toast stack.
class ToastService : public QObject
{
    Q_OBJECT
public:
    explicit ToastService(QObject *parent = nullptr);
    bool start();

    uint notify(const QString &appName, uint replacesId, const QString &appIcon,
                const QString &summary, const QString &body, const QStringList &actions,
                const QVariantMap &hints, int expireTimeout);
    void closeNotification(uint id, uint reason);

signals:
    void notificationClosed(uint id, uint reason);
    void actionInvoked(uint id, const QString &actionKey);

private:
    class ToastHost;
    class ToastCard;

    ToastData buildData(uint id, const QString &appName, const QString &appIcon,
                        const QString &summary, const QString &body, const QStringList &actions,
                        const QVariantMap &hints, int expireTimeout) const;
    int defaultExpireMs(const QVariantMap &hints, int expireTimeout) const;
    QIcon resolveIcon(const QString &appIcon, const QVariantMap &hints) const;
    static QString stripMarkup(QString text);

    NotificationsAdaptor *m_adaptor = nullptr;
    ToastHost *m_host = nullptr;
    uint m_nextId = 1;
};

// One floating toast = one layer-shell surface (blur applies to the card only).
class ToastService::ToastCard : public QWidget
{
    Q_OBJECT
public:
    explicit ToastCard(const ToastData &data, QScreen *screen, QObject *guardParent);

    uint id() const { return m_id; }
    void apply(const ToastData &data);
    void pauseExpiry();
    void resumeExpiry();
    int contentHeight() const;
    int bottomMargin() const { return m_bottom; }
    qreal fadeOpacity() const { return m_fadeOpacity; }
    void setFadeOpacity(qreal opacity);
    void setScreen(QScreen *screen);
    // Place above the screen bottom by `bottom` px (plus the shared side inset).
    void moveToBottom(int bottom, bool animate);

signals:
    void dismissRequested(uint id);
    void expired(uint id);
    void activated(uint id);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void armTimer();
    void refreshUi();
    void ensureLayerShell();
    void applyLayerGeometry();
    void animateChromeTo(int toY);

    uint m_id = 0;
    int m_expireMs = 6000;
    qint64 m_deadlineMs = 0;
    bool m_neverExpires = false;
    int m_bottom = 0;
    bool m_layerReady = false;
    QWidget *m_chrome = nullptr;
    QLabel *m_icon = nullptr;
    QLabel *m_app = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_body = nullptr;
    QToolButton *m_close = nullptr;
    QTimer *m_timer = nullptr;
    QVariantAnimation *m_slide = nullptr;
    ToastData m_data;
    qreal m_fadeOpacity = 1.0;
    QPointer<QScreen> m_screen;
};

// Positions the stack; not itself a surface.
class ToastService::ToastHost : public QObject
{
    Q_OBJECT
public:
    explicit ToastHost(ToastService *service);

    void upsert(const ToastData &data);
    void remove(uint id, uint reason);

private:
    void relayout(ToastCard *slideIn = nullptr);
    QVector<int> targetBottoms() const;
    QScreen *pickScreen() const;

    ToastService *m_service = nullptr;
    QHash<uint, ToastCard *> m_cards;
    QVector<uint> m_order;
    QSet<uint> m_fading;
};
