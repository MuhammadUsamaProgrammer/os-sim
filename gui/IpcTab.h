// IpcTab.h - M3 Inter-Process Communication: shared memory, message passing and pipes
// (deterministic simulation) plus Live Linux demos (System V shm / msg, pipe, FIFO).
#pragma once
#include <QVariantAnimation>
#include <QWidget>
#include <memory>
#include "ipc.h"

class QComboBox; class QSpinBox; class QCheckBox; class QLineEdit; class QTableWidget; class QPlainTextEdit; class QTabWidget;
class QListWidget; class QLabel; class QPushButton; class PlaybackBar; class ModuleHeader; class NativeRunner;

// Process cards around the communication object; data moves with an animation.
class IpcView : public QWidget {
    Q_OBJECT
public:
    explicit IpcView(QWidget* parent = nullptr);
    void setSnapshot(const IpcConfig* cfg, const IpcSnapshot* s, int animMs);
    QSize sizeHint() const override { return QSize(640, 400); }
    QSize minimumSizeHint() const override { return QSize(420, 300); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QRectF procRect(int i, int n) const;
    QRectF objectRect() const;
    const IpcConfig* cfg_ = nullptr;
    const IpcSnapshot* s_ = nullptr;
    double t_ = 1;
    QVariantAnimation anim_;
};

class IpcTab : public QWidget {
    Q_OBJECT
public:
    explicit IpcTab(QWidget* parent = nullptr);
    bool loadPreset(int index);
    bool rebuild();                                     // new simulation from the configuration (no operations)
    QString applyOp(const IpcOp& op);                   // "" or the error
    bool startDemo(const QString& demo);                // shm, msg, pipe, fifo
    void stopDemo();
    bool demoRunning() const;
    int demoExitCode() const;
    PlaybackBar* playback() const { return bar_; }
    const IpcSim* sim() const { return sim_.get(); }
    QString statusText() const;
    QString reportText() const;
    QString csvSection() const;
signals:
    void logMessage(const QString& line);
private:
    IpcConfig readConfig() const;
    void setConfig(const IpcConfig& c);
    void showStep(int pos, bool forward);
    void refreshOps();
    void afterChange();

    ModuleHeader* header_;
    QComboBox *mode_, *preset_, *opProc_, *opKind_, *opPeer_, *demo_;
    QSpinBox *shmSize_, *qCap_, *pipeCap_, *opOffset_, *opLen_;
    QCheckBox *direct_, *named_, *opNb_, *opRo_, *opWriteEnd_;
    QLineEdit* opData_;
    QListWidget* opsList_;
    IpcView* view_;
    QTableWidget* state_;
    PlaybackBar* bar_;
    QTabWidget* bottom_;
    QPlainTextEdit* log_;
    QTableWidget* demoTable_;
    QLabel* demoStatus_;
    QPushButton *demoStart_, *demoStop_;
    NativeRunner* native_;
    std::unique_ptr<IpcSim> sim_;
    bool loading_ = false;
};
