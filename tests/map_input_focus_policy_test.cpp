#include "map/core/map_input_focus_policy.h"

#include <cstdio>

namespace
{
bool require(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "map_input_focus_policy: %s\n", message);
    return condition;
}
}

int main()
{
    if (!require(mapNativeInputFocusLost(QEvent::FocusOut),
                 "native QWindow focus-out must cancel held input")
        || !require(mapNativeInputFocusLost(QEvent::WindowDeactivate),
                    "native window deactivation must cancel held input")
        || !require(!mapNativeInputFocusLost(QEvent::FocusIn),
                    "focus gain must not cancel a new gesture")
        || !require(!mapNativeInputFocusLost(QEvent::MouseButtonRelease),
                    "ordinary releases must retain their normal event routing")
        || !require(!mapNativeInputFocusLost(QEvent::MouseMove),
                    "ordinary mouse moves must retain their normal event routing")
        || !require(mapApplicationInputFocusLost(Qt::ApplicationInactive),
                    "inactive application must cancel held input")
        || !require(mapApplicationInputFocusLost(Qt::ApplicationHidden),
                    "hidden application must cancel held input")
        || !require(mapApplicationInputFocusLost(Qt::ApplicationSuspended),
                    "suspended application must cancel held input")
        || !require(!mapApplicationInputFocusLost(Qt::ApplicationActive),
                    "active application must retain input"))
        return 1;

    return 0;
}
