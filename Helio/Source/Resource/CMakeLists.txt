find_package(meshoptimizer CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(Stb REQUIRED)

helio_add_module(Resource
    SOURCES
        MeshData.cpp
        MeshPrimitives.cpp
        Mesh.cpp
        MeshPipeline.cpp
        MeshImport.cpp
        TextureCache.cpp
        InstanceBatch.cpp
        Material.h
    PRIVATE_INCLUDE
        ${Stb_INCLUDE_DIR}
    PUBLIC_LINK
        Helio.Core
        Helio.RHI
    PRIVATE_LINK
        meshoptimizer::meshoptimizer
        fastgltf::fastgltf
)
