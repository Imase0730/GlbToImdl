#pragma once

#include "Imdl.h"

void GenerateTangentsMikkTSpace(
    std::vector<Imase::VertexPositionNormalTextureTangent>& vertices,
    const std::vector<uint32_t>& indices,
    uint32_t indexStart,
    uint32_t indexCount
);
