# Helio.Gameplay — application framework (engine loop, camera controller).
# Sits above Scene; the world/actor layer lives in Helio.Scene.

helio_add_module(Gameplay
    SOURCES
        HelioEngine.cpp
        FlyCameraController.cpp
    PUBLIC_LINK
        Helio.Core
        Helio.Scene
        Helio.Editor
        Helio.Platform.Windows
        Helio.Input
        Helio.Resource
        Helio.RHI
)
