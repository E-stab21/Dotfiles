#pragma once

#include <QWidget>

class Pill : public QWidget
{
    Q_OBJECT
public:
    explicit Pill(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};
