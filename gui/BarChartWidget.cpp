// BarChartWidget.cpp - axis, grid lines, grouped bars, value labels and a legend.
#include "BarChartWidget.h"
#include <QPainter>
#include <cmath>
#include "Style.h"

BarChartWidget::BarChartWidget(QWidget* parent) : QWidget(parent) { setMinimumHeight(200); }

void BarChartWidget::setData(const QString& title, const QStringList& categories, const QStringList& series,
                             const std::vector<std::vector<double>>& values, const std::vector<QColor>& colors) {
    title_ = title; cats_ = categories; series_ = series; vals_ = values; colors_ = colors;
    update();
}

void BarChartWidget::clear() { cats_.clear(); series_.clear(); vals_.clear(); update(); }

// Rounds the axis maximum up to 1, 2 or 5 times a power of ten.
static double niceMax(double v) {
    if (v <= 0) return 1;
    double p = std::pow(10.0, std::floor(std::log10(v)));
    for (double m : {1.0, 2.0, 5.0, 10.0}) if (m * p >= v) return m * p;
    return 10 * p;
}

void BarChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    p.setPen(palette().text().color());
    if (cats_.isEmpty() || vals_.empty()) { p.drawText(rect(), Qt::AlignCenter, "No chart data yet"); return; }

    const int left = 46, right = 12, top = 46, bottom = 46;
    QRect plot(left, top, width() - left - right, height() - top - bottom);
    p.drawText(QRect(0, 4, width(), 18), Qt::AlignHCenter, title_);

    // Legend
    int lx = left;
    for (int s = 0; s < series_.size(); ++s) {
        QColor c = s < (int)colors_.size() ? colors_[s] : ui::pidColor(s);
        p.fillRect(lx, 26, 12, 12, c);
        p.drawText(lx + 16, 37, series_[s]);
        lx += 24 + p.fontMetrics().horizontalAdvance(series_[s]);
    }

    double maxV = 0;
    for (const auto& row : vals_) for (double v : row) maxV = std::max(maxV, v);
    maxV = niceMax(maxV);

    // Grid lines and y labels
    for (int i = 0; i <= 4; ++i) {
        int y = plot.bottom() - plot.height() * i / 4;
        p.setPen(QPen(palette().mid().color(), 1, Qt::DotLine));
        p.drawLine(plot.left(), y, plot.right(), y);
        p.setPen(palette().text().color());
        p.drawText(QRect(0, y - 8, left - 4, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(maxV * i / 4, 'g', 3));
    }
    // Bars
    int nC = cats_.size(), nS = (int)vals_.size();
    double groupW = double(plot.width()) / nC;
    double barW = groupW * 0.8 / nS;
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    for (int c = 0; c < nC; ++c) {
        for (int s = 0; s < nS; ++s) {
            double v = c < (int)vals_[s].size() ? vals_[s][c] : 0;
            double h = plot.height() * v / maxV;
            QRectF bar(plot.left() + c * groupW + groupW * 0.1 + s * barW, plot.bottom() - h, barW - 1, h);
            p.fillRect(bar, s < (int)colors_.size() ? colors_[s] : ui::pidColor(s));
            p.setFont(small);
            if (barW >= p.fontMetrics().horizontalAdvance(QString::number(v, 'g', 4)) + 2)
              p.drawText(QRectF(bar.left() - 10, bar.top() - 14, bar.width() + 20, 14), Qt::AlignHCenter,
                       QString::number(v, 'g', 4));
            p.setFont(font());
        }
        p.drawText(QRectF(plot.left() + c * groupW, plot.bottom() + 4, groupW, bottom - 4),
                   Qt::AlignHCenter | Qt::TextWordWrap, cats_[c]);
    }
    p.setPen(palette().text().color());
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
    p.drawLine(plot.bottomLeft(), plot.topLeft());
}
