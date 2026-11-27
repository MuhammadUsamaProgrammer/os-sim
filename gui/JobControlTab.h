// JobControlTab.h - Processes > Job Control (Phase 3): the deterministic job-control model of
// core/jobctl.h with an educational console, GUI buttons, an animated process-group view, job list,
// process tree, transcript and event timeline. fg / bg / Ctrl-Z are SIMULATED (QProcess has no
// controlling terminal); the separate Live Linux demo shows real process groups and job-control
// signals on helper-owned children only.
#pragma once
#include <QVariantAnimation>
#include <QWidget>
#include <vector>
#include "jobctl.h"

class QComboBox; class QSpinBox; class QLineEdit; class QCheckBox; class QTableWidget; class QTreeWidget; class QPlainTextEdit;
class QLabel; class PlaybackBar; class ModuleHeader; class NativeRunner;

class JobView : public QWidget {
    Q_OBJECT
public:
    explicit JobView(QWidget* parent = nullptr);
    void setSnapshot(const JcSnapshot* cur, const JcSnapshot* prev, int animMs);
    QSize sizeHint() const override { return QSize(620, 380); }
    QSize minimumSizeHint() const override { return QSize(260, 180); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QRectF boxOf(int pgid, const JcSnapshot& s) const;
    std::vector<const JcJob*> shownJobs(const JcSnapshot& s) const;
    const JcSnapshot *cur_ = nullptr, *prev_ = nullptr;
    double t_ = 1;
    QVariantAnimation anim_;
};

class JobControlPage : public QWidget {
    Q_OBJECT
public:
    explicit JobControlPage(QWidget* parent = nullptr);
    bool loadScenario(int i);
    bool command(const QString& line);          // appended after the current snapshot (new branch after Previous)
    const JobShell& shell() const { return shell_; }
    const JcSnapshot* current() const;
    PlaybackBar* playback() const { return bar_; }
    bool startNativeDemo();
    bool nativeRunning() const;
    int nativeExitCode() const;
    int nativeEventCount() const;
    QString reportText() const;
signals:
    void logMessage(const QString& line);
private:
    void rebuild(bool toEnd);
    void onStep(int pos, bool forward);
    ModuleHeader* header_;
    QComboBox *scenario_, *program_, *target_;
    QSpinBox* amount_;
    QCheckBox* background_;
    QLineEdit* input_;
    QLabel *scenarioText_, *details_;
    JobView* view_;
    QTableWidget *jobs_, *timeline_, *native_;
    QTreeWidget* tree_;
    QPlainTextEdit* transcript_;
    PlaybackBar* bar_;
    NativeRunner* runner_;
    std::vector<std::string> commands_;
    std::vector<JcResult> results_;
    JobShell shell_;
    bool loading_ = false;
};
