#pragma once

#include <QWidget>

class Overlay : public QWidget
{
    Q_OBJECT

public:
    explicit Overlay(QWidget* parent = 0);

    void paintEvent(QPaintEvent *e) override;

    void onLeftProportionChanged(double proportion);
    void onRightProportionChanged(double proportion);

private:
    double m_leftProportion = 0.5;
    double m_rightProportion = 0.5;
};
