find_package(SDL3 CONFIG REQUIRED)

helio_add_module(Platform.Windows
    SOURCES
        Window.cpp
        FileWatcher.cpp
    PUBLIC_LINK
        Helio.Core
        Helio.Input
        SDL3::SDL3
)
