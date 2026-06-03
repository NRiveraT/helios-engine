file(GLOB_RECURSE GAMEPLAY_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "*.h"
)

helio_add_module(Gameplay
    SOURCES
        ${GAMEPLAY_SOURCES}
    PUBLIC_LINK
        Helio.Core
		Helio.Renderer
		Helio.Resource
		Helio.Input
)

