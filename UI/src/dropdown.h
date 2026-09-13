#pragma once

#include <QIcon>
#include <QPointer>
#include <QVector>
#include <QWidget>

class QLabel;
class QListWidget;
class QLineEdit;
class QAbstractButton;
class QVariantAnimation;
class QLayout;
class QScreen;

class DropMenu : public QWidget
{
    Q_OBJECT
public:
    enum class Align { Left, Right };

    explicit DropMenu(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setStatusText(const QString &text);
    void clearItems();
    void addItem(const QString &id, const QString &title, const QString &subtitle = {},
                 bool active = false, const QIcon &icon = {}, const QIcon &trailingIcon = {});
    void setToggleChecked(bool on);
    void setToggleVisible(bool visible);
    void setRefreshVisible(bool visible);
    void setHeaderVisible(bool visible);
    void setSearchVisible(bool visible);
    void setBusy(bool busy);
    void commitItems();
    void popupBelow(QWidget *anchor, Align align = Align::Right, bool quiet = false);
    void softHide();

    void showPasswordPrompt(const QString &networkName, const QString &token);
    void hidePasswordPrompt();
    void setPasswordPromptResult(bool ok, const QString &message);
    bool passwordPromptVisible() const;
    int itemCount() const;
    bool containsGlobalPos(const QPoint &globalPos) const;

signals:
    void itemActivated(const QString &id);
    void toggleRequested(bool enable);
    void refreshRequested();
    void passwordSubmitted(const QString &token, const QString &username, const QString &password);
    void passwordCancelled();
    void hoverEntered();
    void hoverLeft();

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct StoredItem {
        QString id;
        QString title;
        QString subtitle;
        bool active = false;
        QIcon icon;
        QIcon trailingIcon;
    };

    void ensureLayerShell(QScreen *screen);
    void placeOnLayer(const QPoint &topLeft, const QSize &size);
    void relayout();
    void submitPassword();
    void startSlideIn();
    void startFadeIn();
    void appendRow(const StoredItem &item);
    void applySearchFilter();
    void activateCurrentItem();
    void setKeyboardCapture(bool on);
    void selectRelative(int delta);

    QWidget *m_chrome = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_status = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QAbstractButton *m_toggle = nullptr;
    QAbstractButton *m_refresh = nullptr;
    QWidget *m_header = nullptr;
    QVector<StoredItem> m_allItems;
    bool m_rebuildingList = false;

    QWidget *m_passPanel = nullptr;
    QLabel *m_passLabel = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLineEdit *m_passEdit = nullptr;
    QAbstractButton *m_passConnect = nullptr;
    QAbstractButton *m_passCancel = nullptr;
    QString m_passToken;

    QVariantAnimation *m_slide = nullptr;
    QVariantAnimation *m_fade = nullptr;
    bool m_layerReady = false;
    QPoint m_targetPos;
    QPointer<QScreen> m_screen;
};

class PowerStrip : public QWidget
{
    Q_OBJECT
public:
    explicit PowerStrip(QWidget *parent = nullptr);

    void popupBelow(QWidget *anchor);
    bool containsGlobalPos(const QPoint &globalPos) const;

signals:
    void actionChosen(const QString &id);
    void hoverEntered();
    void hoverLeft();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void ensureLayerShell(QScreen *screen);
    void placeOnLayer(const QPoint &topLeft, const QSize &size);
    void startSlideIn();
    QAbstractButton *addGlyph(const QString &id, const QString &glyph, const QString &tip);

    QWidget *m_chrome = nullptr;
    QLayout *m_col = nullptr;
    QVariantAnimation *m_slide = nullptr;
    bool m_layerReady = false;
    QPoint m_targetPos;
    QPointer<QScreen> m_screen;
};
