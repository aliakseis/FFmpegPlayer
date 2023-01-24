#include "overlay.h"

#include <QPainter>
#include <QPainterPath>

Overlay::Overlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
}

void Overlay::paintEvent(QPaintEvent *e)
{
    const int currWidth = width();
    const int currHeight = height();

    QPainter painter(this);

    QPolygon poly;

    poly << QPoint(0, 0) << QPoint(currWidth - 1, 0)
        << QPoint(currWidth - 1, m_rightProportion * currHeight)
        << QPoint(0, m_leftProportion * currHeight);

    // style(), width(), brush(), capStyle() and joinStyle().
    QPen pen(Qt::red, 1);
    painter.setPen(pen);

    // Brush
    QBrush brush;
    brush.setColor(QColor(255, 0, 255, 128));//Qt::green);
    brush.setStyle(Qt::SolidPattern);

    // Fill polygon
    QPainterPath path;
    path.addPolygon(poly);

    // Draw polygon
    painter.drawPolygon(poly);
    painter.fillPath(path, brush);
}

void Overlay::onLeftProportionChanged(double proportion)
{
    m_leftProportion = proportion;
    update();
}

void Overlay::onRightProportionChanged(double proportion)
{
    m_rightProportion = proportion;
    update();
}
