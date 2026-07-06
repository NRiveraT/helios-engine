# Helio.Scene — world/actor scene graph + the renderer that draws it.
# Depends downward only: Renderer (render graph, overlays), Resource (meshes,
# materials, instance batching), Core (math, logging, clock).

helio_add_module(Scene
    SOURCES
        HelioObject.cpp
        HelioWorld.cpp
        Actor.cpp
        SceneRenderer.cpp
        Actors/Camera.cpp
        Actors/StaticMeshActor.cpp
        Actors/DirectionalLight.cpp
    PUBLIC_LINK
        Helio.Core
        Helio.Renderer
        Helio.Resource
)
