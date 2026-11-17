#pragma once

#include <QString>
#include <functional>

namespace AccessibilityStatus {
using ReadEnabled = std::function<bool(bool *enabled, QString *error)>;
using SetEnabled = std::function<bool(QString *error)>;

// Shared logic makes bus state and failure handling testable without a live
// accessibility session.
bool ensureEnabled(const ReadEnabled &readEnabled, const SetEnabled &setEnabled,
                   QString *error);

// Initializes AT-SPI, then enables only the accessibility bus. This does not
// turn on the separate screen-reader preference.
bool enableForSession(QString *error);
}
