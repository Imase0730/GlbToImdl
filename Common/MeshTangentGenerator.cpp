#pragma once

#include "MeshTangentGenerator.h"
#include <iostream>
#include "mikktspace.h"
#include "DirectXMath.h"

//------------------------------------------------------------
// MikkTSpace context wrapper
//------------------------------------------------------------
struct MikkUserData
{
    Imase::VertexPositionNormalTextureTangent* vertices;
    const uint32_t* indices;
    uint32_t indexStart;
    uint32_t indexCount;
};

static int GetNumFaces(const SMikkTSpaceContext* context)
{
    auto* data = (MikkUserData*)context->m_pUserData;
    return data->indexCount / 3;
}

static int GetNumVerticesOfFace(const SMikkTSpaceContext*, const int)
{
    return 3;
}

static void GetPosition(
    const SMikkTSpaceContext* context,
    float pos[3],
    const int face,
    const int vert)
{
    auto* data = (MikkUserData*)context->m_pUserData;

    uint32_t idx =
        data->indices[data->indexStart + face * 3 + vert];

    auto& v = data->vertices[idx];

    pos[0] = v.position.x;
    pos[1] = v.position.y;
    pos[2] = v.position.z;
}

static void GetNormal(
    const SMikkTSpaceContext* context,
    float normal[3],
    const int face,
    const int vert)
{
    auto* data = (MikkUserData*)context->m_pUserData;

    uint32_t idx =
        data->indices[data->indexStart + face * 3 + vert];

    auto& v = data->vertices[idx];

    normal[0] = v.normal.x;
    normal[1] = v.normal.y;
    normal[2] = v.normal.z;
}

static void GetTexCoord(
    const SMikkTSpaceContext* context,
    float uv[2],
    const int face,
    const int vert)
{
    auto* data = (MikkUserData*)context->m_pUserData;

    uint32_t idx =
        data->indices[data->indexStart + face * 3 + vert];

    auto& v = data->vertices[idx];

    uv[0] = v.texcoord.x;
    uv[1] = 1.0f - v.texcoord.y;
}

static void SetTSpaceBasic(
    const SMikkTSpaceContext* context,
    const float tangent[3],
    const float sign,
    const int face,
    const int vert)
{
    auto* data = (MikkUserData*)context->m_pUserData;

    uint32_t idx =
        data->indices[data->indexStart + face * 3 + vert];

    auto& v = data->vertices[idx];

    v.tangent = DirectX::XMFLOAT4(
        tangent[0],
        tangent[1],
        tangent[2],
        sign);
}

void GenerateTangentsMikkTSpace(
    std::vector<Imase::VertexPositionNormalTextureTangent>& vertices,
    const std::vector<uint32_t>& indices,
    uint32_t indexStart,
    uint32_t indexCount)
{
    MikkUserData userData = {};
    userData.vertices = vertices.data();
    userData.indices = indices.data();
    userData.indexStart = indexStart;
    userData.indexCount = indexCount;

    SMikkTSpaceInterface iface = {};
    iface.m_getNumFaces = GetNumFaces;
    iface.m_getNumVerticesOfFace = GetNumVerticesOfFace;
    iface.m_getPosition = GetPosition;
    iface.m_getNormal = GetNormal;
    iface.m_getTexCoord = GetTexCoord;
    iface.m_setTSpaceBasic = SetTSpaceBasic;

    SMikkTSpaceContext context = {};
    context.m_pInterface = &iface;
    context.m_pUserData = &userData;

    if (!genTangSpaceDefault(&context))
        throw std::runtime_error("MikkTSpace tangent generation failed.");
}
