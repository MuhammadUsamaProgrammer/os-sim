// BarChartWidget.h - small grouped bar chart drawn with QPainter (no QtCharts needed).
#pragma once
#include <QColor>
#include <QStringList>
#include <QWidget>
#include <vector>

class BarChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit BarChartWidget(QWidget* parent = nullptr);
    // categories = groups on the x axis; series = one bar per group each;
    // values[s][c] = value of series s in category c.
    void setData(const QString& title, const QStringList& categories, const QStringList& series,
                 const std::vector<std::vector<double>>& values,
                 const std::vector<QColor>& colors = {});
    void clear();
    QSize sizeHint() const override { return QSize(500, 260); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString title_;
    QStringList cats_, series_;
    std::vector<std::vector<double>> vals_;
    std::vector<QColor> colors_;
};
