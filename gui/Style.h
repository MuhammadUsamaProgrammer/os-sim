// Style.h - shared look and helpers for the GUI (colours, warnings, sample folder).
#pragma once
#include <QColor>
#include <QDir>
#include <QMessageBox>
#include <QString>
#include <QTableWidget>
#include <QWidget>
#include <cstdio>

namespace ui {

// True while `--selftest` runs: warnings are printed instead of shown as modal dialogs.
inline bool& selfTestMode() { static bool on = false; return on; }
// Number of warnings raised (the self-test checks that bad input produced one).
inline int& warningCount() { static int n = 0; return n; }

// Friendly input-error dialog (never a crash).
inline void warn(QWidget* parent, const QString& msg) {
    ++warningCount();
    if (selfTestMode()) { std::fprintf(stderr, "[selftest] warning: %s\n", qPrintable(msg)); return; }
    QMessageBox::warning(parent, "OS-Sim", msg);
}
// Information dialog (silent in self-test mode).
inline void info(QWidget* parent, const QString& msg) {
    if (selfTestMode()) { std::fprintf(stderr, "[selftest] info: %s\n", qPrintable(msg)); return; }
    QMessageBox::information(parent, "OS-Sim", msg);
}

inline QColor idleColor()  { return QColor("#c8c8c8"); }
inline QColor faultColor() { return QColor("#e15759"); }   // red: faults / deadlock / unsafe
inline QColor hitColor()   { return QColor("#59a14f"); }   // green: hits / safe
inline QColor lightRed()   { return QColor("#f8c9ca"); }
inline QColor lightGreen() { return QColor("#cfe8c9"); }

// Fixed colour per pid (12-colour palette, repeated).
inline QColor pidColor(int pid) {
    static const QColor pal[12] = {
        QColor("#4e79a7"), QColor("#f28e2b"), QColor("#59a14f"), QColor("#b07aa1"),
        QColor("#76b7b2"), QColor("#edc948"), QColor("#ff9da7"), QColor("#9c755f"),
        QColor("#bab0ac"), QColor("#86bcb6"), QColor("#d37295"), QColor("#a0cbe8")};
    if (pid < 0) return idleColor();
    return pal[pid % 12];
}

// Folder with the sample files (source tree, or ./samples next to the working dir).
inline QString samplesDir() {
    for (const QString& d : {QString("samples"), QString("../samples"), QString(OSSIM_SOURCE_DIR "/samples")})
        if (QDir(d).exists()) return QDir(d).absolutePath();
    return QString(OSSIM_SOURCE_DIR "/samples");
}

// Puts a read-only, centred cell into a table.
inline QTableWidgetItem* roItem(const QString& text) {
    auto* it = new QTableWidgetItem(text);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    it->setTextAlignment(Qt::AlignCenter);
    return it;
}

} // namespace ui
