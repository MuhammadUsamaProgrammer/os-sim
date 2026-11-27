// SyncBridge.h - runs a real pthread synchronization demo (engine/sync_demo.*) on a
// background std::thread and hands its log lines to the GUI through QUEUED signals,
// so the worker threads never touch a widget and the GUI never freezes.
#pragma once
#include <QObject>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include "sync_demo.h"

class SyncBridge : public QObject {
    Q_OBJECT
public:
    explicit SyncBridge(QObject* parent = nullptr);
    ~SyncBridge() override;                // cancels and joins a running demo

    // Starts a demo. Throws std::invalid_argument on bad parameters; false if one is running.
    bool start(SyncDemoKind kind, const SyncDemoParams& params);
    void stop();                           // cancel + join (returns quickly)
    bool running() const { return running_; }
    SyncDemoResult result() const;         // result of the last finished run

signals:
    void line(const QString& text);        // GUI thread: one log line of the current run
    void finished();                       // GUI thread: the run ended (normally or cancelled)
    // Internal: emitted on the background thread, delivered queued to the GUI thread.
    void rawLine(int gen, const QString& text);
    void rawFinished(int gen);

private slots:
    void onRawLine(int gen, const QString& text);
    void onRawFinished(int gen);

private:
    void joinThread();
    std::unique_ptr<SyncDemoRunner> runner_;
    std::thread thread_;
    std::atomic<int> gen_{0};
    mutable std::mutex resultMtx_;
    SyncDemoResult result_;
    bool running_ = false;
};
