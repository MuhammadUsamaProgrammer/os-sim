// NativeRunner.h - runs one "Live Linux Demo" of apps/ossim_helper with QProcess.
// QProcess is asynchronous, so the GUI thread never blocks (no fork/wait/join in the GUI).
// Each run has an id; lines of an older run are ignored. stop() sends SIGTERM to the
// helper's own process group (its children clean up and exit), then SIGKILL after 1.5 s.
#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

struct NativeEvent { long ms = 0; QString role, event, detail; int pid = 0, ppid = 0; };

class QTableWidget;

class NativeRunner : public QObject {
    Q_OBJECT
public:
    explicit NativeRunner(QObject* parent = nullptr);
    ~NativeRunner() override;                  // stops a running demo and waits briefly
    static QString helperPath();               // "" if the helper is not available
    static bool available();                   // Linux + helper present
    bool start(const QStringList& args);       // false if running or unavailable
    void stop();
    bool running() const;
    int lastExitCode() const { return exitCode_; }
    const QList<NativeEvent>& events() const { return events_; }
    // Fills a table (Time, Role, PID, PPID, Event, Detail) with the events received so far.
    static void showEvents(QTableWidget* t, const QList<NativeEvent>& ev);
signals:
    void event(const NativeEvent& e);
    void finished(int exitCode, bool crashed);
private:
    void readLines();
    QProcess* proc_;
    QByteArray partial_;
    QList<NativeEvent> events_;
    int exitCode_ = -1;
    int run_ = 0;
};
