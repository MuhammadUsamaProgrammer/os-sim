// DeadlockTab.h - Banker's algorithm (avoidance), deadlock detection and
// recovery GUI with a resource-allocation graph. All algorithms run in core/.
#pragma once
#include <QWidget>
#include <optional>
#include <vector>
#include "deadlock.h"
#include "pcb.h"

class QSpinBox; class QTableWidget; class QLabel; class QListWidget; class QLineEdit;
class QComboBox; class RagView; class QPushButton;

class DeadlockTab : public QWidget {
    Q_OBJECT
public:
    explicit DeadlockTab(QWidget* parent = nullptr);

    bool loadFile(const QString& path);             // banker_sample.txt / deadlock_sample.txt
    void setSize(int procs, int res);
    void clearAll();
    // Uses the Scheduling tab's pids as row labels (shared process list).
    void useProcessPids(const std::vector<PCB>& procs);
    void setCell(const QString& table, int row, int col, int value);   // for the self-test

    bool checkSafety();
    bool requestResources(int row, const QString& requestText);
    bool detect();
    bool recover(int stepDelayMs = 700);            // 0 = apply all steps at once

    // Results for the report
    QString statusText() const;
    QString reportText() const;
    const std::optional<DetectionResult>& lastDetection() const { return lastDetect_; }
    const std::optional<SafetyResult>& lastSafety() const { return lastSafety_; }
    bool recovering() const { return recovering_; }
    const std::vector<int>& rowPids() const { return rowPids_; }

signals:
    void logMessage(const QString& line);
    void recoveryFinished();

private slots:
    void onSizeChanged();
    void updateNeed();
    void redraw();

private:
    bool readAll(Matrix& alloc, Matrix& max, Matrix& req, Vec& avail);
    void writeMatrix(QTableWidget* t, const Matrix& m);
    void writeAvail(const Vec& v);
    void highlightRows(const std::vector<int>& rows, const QColor& c);
    QString pidName(int row) const;
    QString pidList(const std::vector<int>& rows) const;
    void applyRecoveryStep(size_t k, int delay);

    QSpinBox *procSpin_, *resSpin_;
    QTableWidget *allocT_, *maxT_, *needT_, *reqT_, *availT_;
    QLabel* status_;
    QListWidget* trace_;
    QComboBox* reqPid_;
    QLineEdit* reqEdit_;
    RagView* rag_;
    std::vector<QPushButton*> actionBtns_;

    std::vector<int> rowPids_;                      // label of each row (P<pid>)
    std::vector<int> deadlocked_, killed_;
    std::optional<SafetyResult> lastSafety_;
    std::optional<DetectionResult> lastDetect_;
    std::vector<RecoveryStep> recSteps_;
    QStringList history_;                           // lines for the report
    bool recovering_ = false;
};
