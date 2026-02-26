//--------------------------------------------------------------------------------------
// File: Imdl.h
//
// モデルデータ共通ヘッダ（変換コンバーターと読み込み側共通）
//
// Date: 2026.2.16
// Author: Hideyasu Imase
//--------------------------------------------------------------------------------------
#pragma once

#include <vector>
#include <DirectXMath.h>

namespace Imase
{
    // マテリアル情報
    struct MaterialInfo
    {
        // -------------------
        // 基本色 / PBRパラメータ
        // -------------------
        DirectX::XMFLOAT4 diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };    // BaseColor (白)
        float metallicFactor = 0.0f;                                    // 金属度 (0:非金属, 1:金属)
        float roughnessFactor = 1.0f;                                   // 粗さ (0:ツルツル, 1:粗い)
        DirectX::XMFLOAT3 emissiveColor = { 0.0f, 0.0f, 0.0f };         // 放射色
        float emissiveStrength = 1.0f;                                  // 発光強度

        // -------------------
        // テクスチャインデックス (-1 = 無効)
        // -------------------
        int baseColorTexIndex = -1;   // BaseColor テクスチャ
        int normalTexIndex = -1;      // NormalMap テクスチャ
        int metalRoughTexIndex = -1;  // Metallic-Roughness テクスチャ
        int emissiveTexIndex = -1;    // Emissive テクスチャ
    };

    // サブメッシュ情報
    struct SubMeshInfo
    {
        uint32_t startIndex;        // スタートインデックス  
        uint32_t indexCount;        // インデックス数
        uint32_t materialIndex;     // マテリアルインデックス
    };

    // メッシュグループ情報
    struct MeshGroupInfo
    {
        uint32_t subMeshStart;
        uint32_t subMeshCount;
    };

    // ノード情報
    struct NodeInfo
    {
        int32_t meshGroupIndex;
        int32_t parentIndex;
        DirectX::XMFLOAT4X4 localMatrix;
    };

    // 頂点情報
    struct VertexPositionNormalTextureTangent
    {
        DirectX::XMFLOAT3 position;    // 位置
        DirectX::XMFLOAT3 normal;      // 法線
        DirectX::XMFLOAT2 texcoord;    // テクスチャ座標
        DirectX::XMFLOAT4 tangent;     // xyz = 接線, w = 従接線の向きを調整（1,-1)
    };

    // -------------------------------------------------------------------------------------- //
    // ヘッダ
    struct FileHeader
    {
        uint32_t magic;      // 'IMDL'
        uint32_t version;
        uint32_t chunkCount;
    };

    // チャンクタイプ
    enum ChunkType : uint32_t
    {
        CHUNK_TEXTURE = 'TXTR',
        CHUNK_MATERIAL = 'MTRL',
        CHUNK_SUBMESH = 'MESH',
        CHUNK_MESHGROUP = 'MGRP',
        CHUNK_NODE = 'NODE',
        CHUNK_VERTEX = 'VERT',
        CHUNK_INDEX = 'INDX',
        CHUNK_ANIMATION = 'ANIM'
    };

    // テクスチャタイプ
    enum class TextureType
    {
        BaseColor,
        Normal,
        MetalRough,
        Emissive
    };

    // テクスチャデータ
    struct TextureEntry
    {
        TextureType type;           // 種類
        std::vector<uint8_t> data;  // データ
    };

    // -------------------------------------------------------------------------------------- //
    // アニメーション
    // -------------------------------------------------------------------------------------- //

    // 平行移動、スケールに使用 
    struct AnimationChannelVec3
    {
        uint32_t nodeIndex;

        std::vector<float> times;
        std::vector<DirectX::XMFLOAT3> values;
    };

    // 回転に使用
    struct AnimationChannelQuat
    {
        uint32_t nodeIndex;

        std::vector<float> times;
        std::vector<DirectX::XMFLOAT4> values;
    };

    // アニメーションクリップ
    struct AnimationClip
    {
        std::string name;   // アニメーションの名前
        float duration;     // アニメーションの時間

        std::vector<AnimationChannelVec3> translations; // 移動
        std::vector<AnimationChannelQuat> rotations;    // 回転
        std::vector<AnimationChannelVec3> scales;       // スケール
    };

}
