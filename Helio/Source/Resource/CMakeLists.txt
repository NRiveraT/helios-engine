find_package(meshoptimizer CONFIG REQUIRED)

helio_add_module(Resource
    SOURCES
        MeshData.cpp
        MeshPrimitives.cpp
        Mesh.cpp
        MeshPipeline.cpp
        InstanceBatch.cpp
    PUBLIC_LINK
        Helio.Core
        Helio.RHI
    PRIVATE_LINK
        meshoptimizer::meshoptimizer
)
