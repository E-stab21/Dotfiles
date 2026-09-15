#pragma once

#include <QWidget>

class Pill : public QWidget
{
    Q_OBJECT
public:
    enum class FlushSide { None, Left, Right };

    explicit Pill(QWidget *parent = nullptr);

    void setFlushSide(FlushSide side);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    FlushSide m_flush = FlushSide::None;
};
