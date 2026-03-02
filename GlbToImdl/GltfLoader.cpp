//--------------------------------------------------------------------------------------
// File: GltfLoader.cpp
//
// glb形式のデータを読み込んで情報を取得するクラス
//
// Date: 2026.2.21
// Author: Hideyasu Imase
//--------------------------------------------------------------------------------------
#include <iostream>
#include <filesystem>
#include "GltfLoader.h"
#include "AccessorView.h"

using namespace DirectX;

// glTFファイルのロード関数
GltfLoader::GltfModel GltfLoader::Load(const std::filesystem::path& fname)
{
    GltfLoader::GltfModel gltfModel = {};

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;

    std::string err, warn;

    // glbファイルのオープン
    bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, fname.string());

    if (!ret)
    {
        std::cout << "Failed to load glTF\n";
    }

    // ノード情報の取得
    BuildNode(model, gltfModel.nodes);

    // メッシュ情報の取得
    BuildMesh(model, gltfModel);

    // マテリアル情報の取得
    BuildMaterial(model, gltfModel.materials);

    // イメージ情報の取得
    BuildImage(model, gltfModel.images);

    // テクスチャ情報取得
    BuildTexture(model, gltfModel.textures);

    // サンプラー情報取得
    BuildSampler(model, gltfModel.samplers);

    // アニメーション情報取得
    BuildAnimation(model, gltfModel.animationClips);

    // スキニング情報取得
    BuildSkin(model, gltfModel.nodes, gltfModel.skins);

    return gltfModel;
}

// ノード情報を取得する関数
void GltfLoader::BuildNode(
    const tinygltf::Model& model,
    std::vector<Imase::NodeInfo>& nodes
)
{
    nodes.resize(model.nodes.size());

    for (size_t i = 0; i < nodes.size(); i++)
    {
        const auto& n = model.nodes[i];

        nodes[i].meshGroupIndex = n.mesh;
        nodes[i].parentIndex = -1;
        nodes[i].skinIndex = n.skin;  // -1なら無し

        // 初期化
        nodes[i].defaultTranslation = { 0.0f, 0.0f, 0.0f };
        nodes[i].defaultRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        nodes[i].defaultScale = { 1.0f, 1.0f, 1.0f };

        if (n.matrix.size() == 16)
        {
            // 行列として保存されている場合
            XMFLOAT4X4 m = {};
            float* dst = reinterpret_cast<float*>(&m);

            for (int j = 0; j < 16; j++)
                dst[j] = static_cast<float>(n.matrix[j]);

            XMMATRIX M = XMLoadFloat4x4(&m);
            M = XMMatrixTranspose(M);

            XMVECTOR S, R, T;
            XMMatrixDecompose(&S, &R, &T, M);

            XMStoreFloat3(&nodes[i].defaultScale, S);       // スケール
            XMStoreFloat4(&nodes[i].defaultRotation, R);    // 回転
            XMStoreFloat3(&nodes[i].defaultTranslation, T); // 移動
        }
        else
        {
            // 移動
            if (n.translation.size() == 3)
            {
                nodes[i].defaultTranslation =
                {
                    (float)n.translation[0],
                    (float)n.translation[1],
                    (float)n.translation[2]
                };
            }
            // 回転
            if (n.rotation.size() == 4)
            {
                nodes[i].defaultRotation =
                {
                    (float)n.rotation[0],
                    (float)n.rotation[1],
                    (float)n.rotation[2],
                    (float)n.rotation[3]
                };
            }
            // スケール
            if (n.scale.size() == 3)
            {
                nodes[i].defaultScale =
                {
                    (float)n.scale[0],
                    (float)n.scale[1],
                    (float)n.scale[2]
                };
            }
        }
    }

    // 親子関係構築
    for (size_t parent = 0; parent < model.nodes.size(); parent++)
    {
        for (int child : model.nodes[parent].children)
        {
            nodes[child].parentIndex = static_cast<int>(parent);
        }
    }
}

// ジョイント情報を取得する関数
void GltfLoader::ReadJoints(
    const tinygltf::Model& model,
    int accessorIndex,
    std::vector<DirectX::XMUINT4>& outJoints
)
{
    const auto& accessor = model.accessors[accessorIndex];
    assert(accessor.type == TINYGLTF_TYPE_VEC4);

    outJoints.resize(accessor.count);
    
    // uint8_t
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
    {
        AccessorView<uint8_t[4]> view(model, accessorIndex);
        for (size_t i = 0; i < accessor.count; ++i)
        {
            const auto& v = view[i];
            outJoints[i] = DirectX::XMUINT4(v[0], v[1], v[2], v[3]);
        }
    }
    // uint16_t
    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
    {
        AccessorView<uint16_t[4]> view(model, accessorIndex);

        for (size_t i = 0; i < accessor.count; ++i)
        {
            const auto& v = view[i];
            outJoints[i] = DirectX::XMUINT4(v[0], v[1], v[2], v[3]);
        }
    }
    else
    {
        throw std::runtime_error("Unsupported JOINT component type");
    }
}

// ウエイト情報を取得する関数
void GltfLoader::ReadWeights(
    const tinygltf::Model& model,
    int accessorIndex,
    std::vector<DirectX::XMFLOAT4>& outWeights
)
{
    const auto& accessor = model.accessors[accessorIndex];

    outWeights.resize(accessor.count);

    switch (accessor.componentType)
    {

    case TINYGLTF_COMPONENT_TYPE_FLOAT: // float
    {
        AccessorView<DirectX::XMFLOAT4> view(model, accessorIndex);

        for (size_t i = 0; i < view.size(); ++i)
        {
            outWeights[i] = view[i];
        }
        break;
    }

    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: // 1byte
    {
        AccessorView<uint8_t[4]> view(model, accessorIndex);

        for (size_t i = 0; i < view.size(); ++i)
        {
            const uint8_t* w = view[i];
            outWeights[i] = DirectX::XMFLOAT4(w[0] / 255.0f, w[1] / 255.0f, w[2] / 255.0f, w[3] / 255.0f);
        }
        break;
    }

    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:    // 2byte
    {
        AccessorView<uint16_t[4]> view(model, accessorIndex);

        for (size_t i = 0; i < view.size(); ++i)
        {
            const uint16_t* w = view[i];
            outWeights[i] = DirectX::XMFLOAT4(w[0] / 65535.0f, w[1] / 65535.0f, w[2] / 65535.0f, w[3] / 65535.0f);
        }
        break;
    }

    default:
        throw std::runtime_error("Unsupported WEIGHTS component type");
    }

    // 念のため正規化補正（誤差対策）
    for (auto& w : outWeights)
    {
        float sum = w.x + w.y + w.z + w.w;

        if (sum > 0.00001f)
        {
            w.x /= sum;
            w.y /= sum;
            w.z /= sum;
            w.w /= sum;
        }
    }
}

// モデル情報を取得する関数
void GltfLoader::BuildMesh(
    const tinygltf::Model& model,
    GltfModel& outModel
)
{
    outModel.meshGroups.resize(model.meshes.size());

    for (size_t i = 0; i < model.meshes.size(); i++)
    {
        auto& mesh = model.meshes[i];

        Imase::MeshGroupInfo group = {};
        group.subMeshStart = (uint32_t)outModel.subMeshes.size();
        group.subMeshCount = (uint32_t)mesh.primitives.size();

        // プリミティブ数ループ
        for (auto& primitive : mesh.primitives)
        {
            uint32_t vertexStart = (uint32_t)outModel.vertices.size();
            uint32_t indexStart = (uint32_t)outModel.indices.size();

            // ----- 位置 ----- //
            AccessorView<XMFLOAT3> positions(model, primitive.attributes.at("POSITION"));

            // ----- 法線 ----- //
            std::unique_ptr<AccessorView<XMFLOAT3>> normals;
            bool hasNormal = primitive.attributes.count("NORMAL") > 0;
            if (hasNormal)
            {
                normals = std::make_unique<AccessorView<XMFLOAT3>>(model, primitive.attributes.at("NORMAL"));
            }

            // ----- テクスチャ座標 ----- //
            std::unique_ptr<AccessorView<XMFLOAT2>> uvs;
            bool hasUV = primitive.attributes.count("TEXCOORD_0") > 0;
            if (hasUV)
            {
                uvs = std::make_unique<AccessorView<XMFLOAT2>>(model, primitive.attributes.at("TEXCOORD_0"));
            }

            // ----- 接線 ----- //
            std::unique_ptr<AccessorView<XMFLOAT4>> tangents;
            bool hasTangent = primitive.attributes.count("TANGENT") > 0;
            if (hasTangent)
            {
                tangents = std::make_unique<AccessorView<XMFLOAT4>>(model, primitive.attributes.at("TANGENT"));
            }

            // ----- スキン ----- //
            bool hasSkin = primitive.attributes.count("JOINTS_0") > 0 && primitive.attributes.count("WEIGHTS_0") > 0;
            std::vector<DirectX::XMUINT4> joints;
            std::vector<DirectX::XMFLOAT4> weights;
            if (hasSkin)
            {
                // ジョイント
                ReadJoints(model, primitive.attributes.at("JOINTS_0"), joints);

                // ウエイト
                ReadWeights(model, primitive.attributes.at("WEIGHTS_0"), weights);
            }
            else
            {
                joints.resize(positions.size(), DirectX::XMUINT4(0, 0, 0, 0));
                weights.resize(positions.size(), DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f));
            }

            // 頂点追加
            for (size_t j = 0; j < positions.size(); j++)
            {
                Imase::VertexPositionNormalTextureTangent vertex
                {
                    positions[j],
                    hasNormal ? (*normals)[j] : XMFLOAT3(0.0f, 0.0f, 1.0f),
                    hasUV ? (*uvs)[j] : XMFLOAT2(0.0f, 0.0f),
                    hasTangent ? (*tangents)[j] : XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),
                    joints[j],
                    weights[j],
                };
                outModel.vertices.push_back(vertex);
            }

            // トポロジーはトライアングルリストのみ対応
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
                throw std::runtime_error("Only TRIANGLES supported.");

            // インデックス
            if (primitive.indices < 0)
                throw std::runtime_error("Primitive has no indices.");

            // インデックス追加
            IndexAccessorView indices(model, primitive.indices);
            for (size_t j = 0; j < indices.size(); j++)
            {
                outModel.indices.push_back(vertexStart + indices[j]);
            }

            // SubMesh作成
            Imase::SubMeshInfo sub = {};
            sub.startIndex = indexStart;
            sub.indexCount = (uint32_t)indices.size();
            // 使用マテリアル番号（０はディフォルトマテリアル）
            sub.materialIndex = (primitive.material >= 0) ? (primitive.material + 1) : 0;

            outModel.subMeshes.push_back(sub);
        }

        outModel.meshGroups[i] = group;
    }

    return;
}

// マテリアル情報を取得する関数
void GltfLoader::BuildMaterial(
    const tinygltf::Model& model,
    std::vector<Imase::MaterialInfo>& materials
)
{
    materials.resize(model.materials.size());

    for (size_t i = 0; i < model.materials.size(); i++)
    {
        auto& material = model.materials[i];

        // PBR
        materials[i].diffuseColor = {
            static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[0]),
            static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[1]),
            static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[2]),
            static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[3]),
        };
        materials[i].metallicFactor = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
        materials[i].roughnessFactor = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);

        materials[i].baseColorTexIndex = material.pbrMetallicRoughness.baseColorTexture.index;
        materials[i].normalTexIndex = material.normalTexture.index;
        materials[i].metalRoughTexIndex = material.pbrMetallicRoughness.metallicRoughnessTexture.index;

        // Emissive
        materials[i].emissiveColor = {
            static_cast<float>(material.emissiveFactor[0]),
            static_cast<float>(material.emissiveFactor[1]),
            static_cast<float>(material.emissiveFactor[2]),
        };

        materials[i].emissiveStrength = 1.0f;
        // emissiveStrengthは拡張
        auto it = material.extensions.find("KHR_materials_emissive_strength");
        if (it != material.extensions.end())
        {
            const tinygltf::Value& ext = it->second;

            if (ext.Has("emissiveStrength"))
            {
                materials[i].emissiveStrength = static_cast<float>(ext.Get("emissiveStrength").Get<double>());
            }
        }

        materials[i].emissiveTexIndex = material.emissiveTexture.index;
    }

    return;
}

// イメージ情報を取得する関数
void GltfLoader::BuildImage(
    tinygltf::Model& model,
    std::vector<GltfImage>& images
)
{
    images.resize(model.images.size());

    for (size_t i = 0; i < model.images.size(); i++)
    {
        auto& image = model.images[i];

        images[i].name = image.name;
        images[i].data = std::move(image.image);
        images[i].width = image.width;
        images[i].height = image.height;
        images[i].component = image.component;
        images[i].bits = image.bits;
    }

    return;
}

// テクスチャ情報を取得する関数
void GltfLoader::BuildTexture(
    const tinygltf::Model& model,
    std::vector<GltfTexture>& textures
)
{
    textures.resize(model.textures.size());

    for (size_t i = 0; i < model.textures.size(); i++)
    { 
        auto& texture = model.textures[i];

        textures[i].imageIndex = texture.source;
        textures[i].samplerIndex = texture.sampler;
    }

    return;
}

// サンプラー情報を取得する関数
void GltfLoader::BuildSampler(const tinygltf::Model& model, std::vector<GltfSampler>& samplers)
{
    samplers.resize(model.samplers.size());

    for(size_t i = 0; i < model.samplers.size(); i++)
    {
        auto& sampler = model.samplers[i];

        samplers[i].wrapS = sampler.wrapS;
        samplers[i].wrapT = sampler.wrapT;
        samplers[i].minFilter = sampler.minFilter;
        samplers[i].magFilter = sampler.magFilter;
    }

    return;
}

// アニメーション情報を取得する関数
void GltfLoader::BuildAnimation(const tinygltf::Model& model, std::vector<AnimationClip>& clips)
{
    clips.reserve(model.animations.size());

    for (const auto& animation : model.animations)
    {
        AnimationClip clip = {};

        // 名前
        clip.name = animation.name;

        // ノードインデックスとノードの対応マップ
        std::unordered_map<int, size_t> nodeMap;

        // channelとは、「対象ノードがどのsamplerを使用するか」
        for (const auto& channel : animation.channels)
        {
            // samplerとは、「この時間列とこの値列を、この補間方法で評価してください」
            const auto& sampler = animation.samplers[channel.sampler];
            
            // 対象ノード
            int nodeIndex = channel.target_node;
            std::string path = channel.target_path; // translation / rotation / scale / weights
 
            // AnimationClipのnodesのインデックスとnodeIndexの対応マップ
            // 対象ノードが未登録の場合NodeAnimationを新規で追加
            if (nodeMap.find(nodeIndex) == nodeMap.end())
            {
                NodeAnimation nodeAnim;
                nodeAnim.nodeIndex = nodeIndex; // ← 対象ノード
                nodeMap[nodeIndex] = clip.nodes.size(); // nodes[?]の?番目に対象ノードの情報を追加
                clip.nodes.push_back(nodeAnim);
            }

            // 対象ノードのアニメーションクリップを取得
            NodeAnimation& nodeAnim = clip.nodes[nodeMap[nodeIndex]];

            const tinygltf::Accessor& inputAccessor = model.accessors[sampler.input];

            // 時間
            AccessorView<float> times(model, sampler.input);

            // アニメーションの時間
            for (size_t i = 0; i < inputAccessor.count; i++)
            {
                clip.duration = std::max(clip.duration, times[i]);
            }

            // 移動
            if (path == "translation")
            {
                AccessorView<DirectX::XMFLOAT3> values(model, sampler.output);
                for (size_t i = 0; i < inputAccessor.count; i++)
                {
                    KeyframeVec3 key = {};
                    key.time = times[i];
                    key.value = values[i];
                    nodeAnim.translations.push_back(key);
                }
            }
            // 回転
            else if (path == "rotation")
            {
                AccessorView<DirectX::XMFLOAT4> values(model, sampler.output);
                for (size_t i = 0; i < inputAccessor.count; i++)
                {
                    KeyframeQuat key = {};
                    key.time = times[i];
                    key.value = values[i];
                    nodeAnim.rotations.push_back(key);
                }
            }
            // 拡大縮小
            else if (path == "scale")
            {
                AccessorView<DirectX::XMFLOAT3> values(model, sampler.output);
                for (size_t i = 0; i < inputAccessor.count; i++)
                {
                    KeyframeVec3 key = {};
                    key.time = times[i];
                    key.value = values[i];
                    nodeAnim.scales.push_back(key);
                }
            }
        }
        clips.push_back(clip);
    }
}

// ルートジョイントインデックスを返す関数
int GltfLoader::FindRootJoint(const std::vector<int>& joints, const std::vector<Imase::NodeInfo>& nodes)
{
    for (int joint : joints)
    {
        int parent = nodes[joint].parentIndex;

        // 親を持たないのでルートノード
        if (parent < 0)
        {
            return joint;
        }

        // スキンに含まれるジョイントの中で、一番上にあるもの
        if (std::find(joints.begin(), joints.end(), parent) == joints.end())
        {
            return joint;
        }
    }

    return joints.empty() ? -1 : joints[0];
}

// スキン情報を取得する関数
void GltfLoader::BuildSkin(
    const tinygltf::Model& model,
    const std::vector<Imase::NodeInfo>& nodes,
    std::vector<Imase::SkinInfo>& skins
)
{
    skins.resize(model.skins.size());

    // スキン数分ループ
    for (size_t i = 0; i < model.skins.size(); i++)
    {
        const tinygltf::Skin& skin = model.skins[i];
        auto& outSkin = skins[i];

        // ルートノード
        if (skin.skeleton >= 0)
        {
            outSkin.rootNode = skin.skeleton;
        }
        else
        {
            // ルートノードがない場合
            outSkin.rootNode = FindRootJoint(skin.joints, nodes);
        }

        // ジョイント
        for (int joint : skin.joints)
        {
            outSkin.jointIndices.push_back(static_cast<uint32_t>(joint));
        }

        // バインド時のジョイント空間を打ち消す行列
        if (skin.inverseBindMatrices >= 0)
        {
            AccessorView<XMFLOAT4X4> values(model, skin.inverseBindMatrices);

            if (values.size() != skin.joints.size())
                throw std::runtime_error("inverseBindMatrices count mismatch.");

            outSkin.inverseBindMatrices.reserve(values.size());

            for (size_t j = 0; j < values.size(); j++)
            {
                outSkin.inverseBindMatrices.push_back(values[j]);   // <- glTLは列優先だがXMFLOAT4X4にコピーすると転置する
            }
        }
        else
        {
            outSkin.inverseBindMatrices.resize(skin.joints.size());
            for (auto& m : outSkin.inverseBindMatrices)
            {
                XMStoreFloat4x4(&m, XMMatrixIdentity());
            }
        }
    }

    return;
}
