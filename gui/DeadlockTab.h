// DeadlockTab.h - Banker's algorithm / detection GUI (implemented in Stage 8).
#pragma once
#include <QWidget>
class DeadlockTab : public QWidget {
    Q_OBJECT
public:
    explicit DeadlockTab(QWidget* parent = nullptr);
signals:
    void logMessage(const QString& line);
};
