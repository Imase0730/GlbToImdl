//--------------------------------------------------------------------------------------
// File: GltfLoader.h
//
// glb形式のデータを読み込んで情報を取得するクラス
//
// Date: 2026.2.21
// Author: Hideyasu Imase
//--------------------------------------------------------------------------------------
#pragma once

#include <vector>
#include <filesystem>
#include <DirectXMath.h>

#include "tiny_gltf.h"
#include "../Common/Imdl.h"

class GltfLoader
{
public:

    // イメージデータ
    struct GltfImage
    {
        std::string name;           // 名前
        std::vector<uint8_t> data;  // イメージデータ
        int width;                  // 幅
        int height;                 // 高さ
        int component;              // チャンネル数
        int bits;                   // ビット数（通常8ビット）
    };

    // テクスチャデータ
    struct GltfTexture
    {
        int imageIndex;     // イメージデータのインデックス
        int samplerIndex;   // サンプラーインデックス（現在未使用）
    };

    // サンプラー情報
    struct GltfSampler
    {
        int wrapS;
        int wrapT;
        int minFilter;
        int magFilter;
    };

    // ------------------------------------------------------------------ //
    // Animation
    // ------------------------------------------------------------------ //

    // float3（位置、回転）
    struct KeyframeVec3
    {
        float time;
        DirectX::XMFLOAT3 value;
    };

    // float4（クォータニオン）
    struct KeyframeQuat
    {
        float time;
        DirectX::XMFLOAT4 value;
    };

    // ノードのアニメーション情報
    struct NodeAnimation
    {
        int nodeIndex;  // ノードインデックス

        std::vector<KeyframeVec3> translations; // 位置
        std::vector<KeyframeQuat> rotations;    // 回転
        std::vector<KeyframeVec3> scales;       // スケール
    };

    // アニメーションクリップ
    struct AnimationClip
    {
        std::string name;                   // 名前
        float duration = 0.0f;              // 間隔
        std::vector<NodeAnimation> nodes;   // ノード
    };

    // ------------------------------------------------------------------ //

    // モデルデータ
    struct GltfModel
    {
        // ★共有バッファ
        std::vector<Imase::VertexPositionNormalTextureTangent> vertices;
        std::vector<uint32_t> indices;

        // ★描画情報
        std::vector<Imase::SubMeshInfo> subMeshes;
        std::vector<Imase::MeshGroupInfo> meshGroups;
        std::vector<Imase::NodeInfo> nodes;

        std::vector<Imase::MaterialInfo> materials;

        std::vector<GltfImage> images;
        std::vector<GltfTexture> textures;
        std::vector<GltfSampler> samplers;

        // アニメーション
        std::vector<AnimationClip> animationClips;

        // スキン
        std::vector<Imase::SkinInfo> skins;
    };

private:

    // ノード情報を取得する関数
    static void BuildNode(const tinygltf::Model& model, std::vector<Imase::NodeInfo>& nodes);

    // メッシュ情報を取得する関数
    static void BuildMesh(const tinygltf::Model& model, GltfModel& outModel);

    // マテリアル情報を取得する関数
    static void BuildMaterial(const tinygltf::Model& model, std::vector<Imase::MaterialInfo>& materials);

    // イメージ情報を取得する関数
    static void BuildImage(tinygltf::Model& model, std::vector<GltfImage>& images);

    // テクスチャ情報を取得する関数
    static void BuildTexture(const tinygltf::Model& model, std::vector<GltfTexture>& textures);

    // サンプラー情報を取得する関数
    static void BuildSampler(const tinygltf::Model& model, std::vector<GltfSampler>& samplers);

    // アニメーション情報を取得する関数
    static void BuildAnimation(const tinygltf::Model& model, std::vector<AnimationClip>& animations);

    // スキン情報を取得する関数
    static void BuildSkin(const tinygltf::Model& model, const std::vector<Imase::NodeInfo>& nodes, std::vector<Imase::SkinInfo>& skins);

    // ジョイント情報を取得する関数
    static void ReadJoints(const tinygltf::Model& model, int accessorIndex, std::vector<DirectX::XMUINT4>& outJoints);

    // ウエイト情報を取得する関数
    static void ReadWeights(const tinygltf::Model& model, int accessorIndex, std::vector<DirectX::XMFLOAT4>& outWeights);

    // ルートジョイントインデックスを返す関数
    static int FindRootJoint(const std::vector<int>& joints, const std::vector<Imase::NodeInfo>& nodes);

public:

    // glTFファイルのロード関数
    static GltfLoader::GltfModel Load(const std::filesystem::path& fname);

};
