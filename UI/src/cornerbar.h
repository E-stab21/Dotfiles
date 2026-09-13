#pragma once

#include <QWidget>

class Pill;
class QHBoxLayout;
class QVariantAnimation;
class QTimer;
class QResizeEvent;
class QShowEvent;

// One auto-hiding corner pill as its own layer-shell surface (no full-width
// mask). Hover the top edge to drop it; leave to retract.
class CornerBar : public QWidget
{
    Q_OBJECT
public:
    enum class Edge { Left, Right };

    explicit CornerBar(Edge edge, QWidget *parent = nullptr);

    Pill *pill() const { return m_pill; }
    QHBoxLayout *pillLayout() const { return m_pillLayout; }
    Edge edge() const { return m_edge; }
    bool revealed() const { return m_reveal >= 0.999; }

public slots:
    void reveal();
    void scheduleHide();
    void cancelHide();
    void hideNow();
    void relayout();
    void setHoldOpen(bool hold);

signals:
    void hoverEntered();
    void hoverLeft();
    void fullyRevealed();

protected:
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupLayerShell();
    void applyReveal();
    int hiddenY() const;
    int shownY() const;

    Edge m_edge;
    Pill *m_pill = nullptr;
    QHBoxLayout *m_pillLayout = nullptr;
    QWidget *m_peek = nullptr;
    QVariantAnimation *m_anim = nullptr;
    QTimer *m_hideTimer = nullptr;
    qreal m_reveal = 0.0;
    bool m_layerReady = false;
    bool m_holdOpen = false;
    bool m_pointerInside = false;
};
