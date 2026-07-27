import { BufferAttribute, Mesh } from "three"

export type ModelMeshData = {
    indices: BufferAttribute,
    vertexstart: number,//used when merging partial meshes
    vertexend: number,//used when merging partial meshes
    indexLODs: BufferAttribute[],
    materialId: number,
    priority?: number,   // per-render-group draw priority (meshdata renders[].unkint; film/glass layers sit higher)
    unkint?: number,
    hasVertexAlpha: boolean,
    needsNormalBlending: boolean,
    attributes: {
        pos: BufferAttribute,
        normals?: BufferAttribute,
        color?: BufferAttribute,
        colorHsl?: BufferAttribute,   // per-vertex PACKED HSL (pre-palette), for palette-space lighting + exact op40
        texuvs?: BufferAttribute,
        //new skeletal animations
        skinids?: BufferAttribute,
        skinweights?: BufferAttribute,
        //old transform based animations
        boneids?: BufferAttribute,
        boneweights?: BufferAttribute
    }
}

export type ModelData = {
    maxy: number,
    miny: number,
    skincount: number,
    bonecount: number,
    meshes: ModelMeshData[],
    debugmeshes?: Mesh[]
}
