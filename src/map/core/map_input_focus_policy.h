#ifndef MAP_INPUT_FOCUS_POLICY_H
#define MAP_INPUT_FOCUS_POLICY_H

#include <QEvent>
#include <Qt>

// The embedded bgfx native window and its QWidget input controller do not
// necessarily receive the same activation events. A missing release must
// terminate navigation whether the compositor sends FocusOut or deactivates
// the native window, and whenever the application itself becomes inactive.
inline bool mapNativeInputFocusLost(QEvent::Type type)
{
    return type == QEvent::FocusOut || type == QEvent::WindowDeactivate;
}

inline bool mapApplicationInputFocusLost(Qt::ApplicationState state)
{
    return state != Qt::ApplicationActive;
}

#endif // MAP_INPUT_FOCUS_POLICY_H
