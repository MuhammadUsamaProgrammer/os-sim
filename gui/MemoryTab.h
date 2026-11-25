// MemoryTab.h - page replacement GUI (implemented in Stage 7).
#pragma once
#include <QWidget>
class MemoryTab : public QWidget {
    Q_OBJECT
public:
    explicit MemoryTab(QWidget* parent = nullptr);
signals:
    void logMessage(const QString& line);
};
