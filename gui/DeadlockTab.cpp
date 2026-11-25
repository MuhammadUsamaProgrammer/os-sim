// DeadlockTab.cpp - placeholder until Stage 8.
#include "DeadlockTab.h"
#include <QLabel>
#include <QVBoxLayout>
DeadlockTab::DeadlockTab(QWidget* parent) : QWidget(parent) {
    (new QVBoxLayout(this))->addWidget(new QLabel("Deadlock tab: coming in Stage 8"));
}
