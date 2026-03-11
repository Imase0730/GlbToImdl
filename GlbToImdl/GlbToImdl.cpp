//--------------------------------------------------------------------------------------
// File: glTFToImdl.cpp
//
// glb形式のファイルを独自形式のモデルデータ(.imdl)に変換するツール
//
// Date: 2026.2.17
// Author: Hideyasu Imase
//--------------------------------------------------------------------------------------

// ------------------------------------------------------------ //
// モデルデータフォーマット
//
// ファイルヘッダ (FileHeader)
//   uint32_t magic      // 0x4C444D49; 'IMDL'
//   uint32_t version    // 1
//   uint32_t chunkCount // 9
//
// ----- チャンク -----
//
// 1. テクスチャチャンク (CHUNK_TEXTURE)
//   uint32_t textureCount
//   テクスチャごとに以下を繰り返す
//     uint32_t type       // TextureType enum
//     uint32_t size       // DDS データサイズ
//     uint8_t[size] data  // DDS 本体データ
//
// 2. マテリアルチャンク (CHUNK_MATERIAL)
//   uint32_t materialCount
//   Material[materialCount] // MaterialInfo 構造体配列
//
// 3. サブメッシュチャンク (CHUNK_SUBMESH)
//   uint32_t subMeshCount
//   SubMeshInfo[subMeshCount] // SubMeshInfo 構造体配列
//
// 4. メッシュグループチャンク (CHUNK_MESHGROUP)
//   uint32_t meshGroupCount
//   MeshGroupInfo[meshGroupCount] // MeshGroupInfo 構造体配列
//
// 5. ノードチャンク (CHUNK_NODE)
//   uint32_t nodeCount
//   NodeInfo[nodeCount] // NodeInfo 構造体配列
//
// 6. 頂点チャンク (CHUNK_VERTEX)
//   uint32_t vertexCount
//   VertexPositionNormalTextureTangent[vertexCount] // 頂点配列
//
// 7. インデックスチャンク(CHUNK_INDEX)
//   uint32_t indexCount
//   uint32_t[indexCount] // インデックス配列
//
// 8. アニメーションチャンク(CHUNK_ANIMATION)
//   uint32_t animationCount
//   AnimationClip[animationCount] // AnimationClip 構造体配列
//
// 9. スキンチャンク(CHUNK_SKIN)
//   uint32_t skinCount
//   SkinInfo[skinCount] // SkinInfo 構造体配列
//
// ------------------------------------------------------------ //

#include <iostream>
#include <windows.h>
#include <wrl.h>
#include <vector>
#include <fstream>
#include <filesystem>
#include <string>
#include <d3d11.h>
#include "../Common/cxxopts.hpp"
#include "../Common/BinaryWriter.h"
#include "../Common/ChunkIO.h"
#include "../Common/Imdl.h"
#include "DirectXTex.h"
#include "AccessorView.h"
#include "GltfLoader.h"

using namespace DirectX;
using namespace Imase;

#pragma comment(lib, "d3d11.lib")

using TextureKey = std::pair<int, TextureType>;

// hash 特殊化
namespace std
{
    template<>
    struct hash<TextureType>
    {
        size_t operator()(TextureType t) const noexcept
        {
            using Underlying = std::underlying_type_t<TextureType>;
            return std::hash<Underlying>{}(static_cast<Underlying>(t));
        }
    };

    template<>
    struct hash<TextureKey>
    {
        size_t operator()(const TextureKey& key) const noexcept
        {
            size_t h1 = std::hash<int>{}(key.first);
            size_t h2 = std::hash<TextureType>{}(key.second);

            // boost風ハッシュ合成
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };
}

// UTF-16 → UTF-8 変換
static std::string WStringToUtf8(const std::wstring& ws)
{
    int size = WideCharToMultiByte(
        CP_UTF8, 0,
        ws.c_str(), -1,
        nullptr, 0,
        nullptr, nullptr);

    std::string result(size - 1, 0);

    WideCharToMultiByte(
        CP_UTF8, 0,
        ws.c_str(), -1,
        result.data(), size,
        nullptr, nullptr);

    return result;
}

// パス付きファイル名を出力用ファイル名に変換する関数
// 例　C:¥Imase\A.imdl → C:¥Imase\A_anim.h 
std::filesystem::path MakeOutputPath(const std::filesystem::path& input, const std::string& suffix)
{
    return input.parent_path() / (input.stem().string() + suffix);
}

// ヘルプ表示
static void Help()
{
    std::cout <<
        "Usage:\n"
        "  glTFToImdl <input.glb> [-o output.imdl]\n\n"
        "Options:\n"
        "  -o, --output <file>   Output file\n"
        "  -h, --help            Show help\n";
}

// 引数から入力ファイル名と出力ファイル名を取得する関数
static int AnalyzeOption(int argc, char* argv[], std::filesystem::path& input, std::filesystem::path& output)
{
    // cxxoptsで引数解析
    cxxopts::Options options("glTFToImdl");
    options.add_options()
        ("input", "Input model file (.glb)",
            cxxopts::value<std::string>())
        ("o,output", "Output file",
            cxxopts::value<std::string>())
        ("h,help", "Show help");
    options.parse_positional({ "input" });

    try
    {
        auto result = options.parse(argc, argv);

        // -h,--help 指定された
        if (result.count("help"))
        {
            Help();
            return 0;
        }

        // 入力ファイル名
        input = std::filesystem::path(result["input"].as<std::string>());

        // -o,-output 出力ファイル名
        if (result.count("output") == 0) {
            // 指定されていない場合は出力ファイル名は、入力ファイル名.mdlにする
            output = MakeOutputPath(input, ".imdl");
        }
        else
        {
            // 指定された
            output = std::filesystem::path(result["output"].as<std::string>());
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        Help();
        return 1;
    }

    return 0;
}

// DirectXのデバイスを作成する関数
static void CreateD3DDevice(ID3D11Device** device)
{
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // アダプタ
        D3D_DRIVER_TYPE_HARDWARE,   // GPU使用
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        device,
        &featureLevel,
        nullptr
    );

    if (FAILED(hr))
    {
        // ハードウェアが無理ならWARP
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            device,
            &featureLevel,
            nullptr
        );
    }
}

// テクスチャタイプによる変換ファイルフォーマットを取得する関数
static DXGI_FORMAT GetFormat(TextureType type)
{
    switch (type)
    {
    case TextureType::BaseColor:
        return DXGI_FORMAT_BC7_UNORM_SRGB;

    case TextureType::Normal:
        return DXGI_FORMAT_BC5_UNORM;

    case TextureType::MetalRough:
        return DXGI_FORMAT_BC7_UNORM;

    case TextureType::Emissive:
        return DXGI_FORMAT_BC7_UNORM_SRGB;

    default:
        return DXGI_FORMAT_BC7_UNORM;
    }
}

// テクスチャデータをDDS形式にしてメモリに書き出す関数
static HRESULT ConvertToDDSMemory(
    ID3D11Device* device,
    const uint8_t* rgbaData,
    size_t width,
    size_t height,
    int bits,
    TextureType type,
    std::vector<uint8_t>& outDDS)
{
    HRESULT hr;

    bool is16bit = (bits == 16);

    // ----------------------------------
    // 1. ScratchImage 初期化
    // ----------------------------------

    DXGI_FORMAT srcFormat =
        is16bit ?
        DXGI_FORMAT_R16G16B16A16_UNORM :
        DXGI_FORMAT_R8G8B8A8_UNORM;

    ScratchImage scratch;

    hr = scratch.Initialize2D(
        srcFormat,
        width,
        height,
        1,
        1);

    if (FAILED(hr))
        return hr;

    const Image* img = scratch.GetImage(0, 0, 0);

    size_t pixelSize = is16bit ? 8 : 4;

    for (size_t y = 0; y < height; ++y)
    {
        memcpy(
            img->pixels + y * img->rowPitch,
            rgbaData + y * width * pixelSize,
            width * pixelSize);
    }

    // ----------------------------------
    // 2. 16bitなら8bitへ変換
    // ----------------------------------

    ScratchImage converted;

    if (is16bit)
    {
        hr = Convert(
            scratch.GetImages(),
            scratch.GetImageCount(),
            scratch.GetMetadata(),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            TEX_FILTER_DEFAULT,
            TEX_THRESHOLD_DEFAULT,
            converted);

        if (FAILED(hr))
            return hr;
    }

    const ScratchImage& srcImage = is16bit ? converted : scratch;

    // ----------------------------------
    // 3. ミップ生成
    // ----------------------------------

    ScratchImage mipChain;

    hr = GenerateMipMaps(
        srcImage.GetImages(),
        srcImage.GetImageCount(),
        srcImage.GetMetadata(),
        TEX_FILTER_FANT,
        0,
        mipChain);

    if (FAILED(hr))
        return hr;

    // ----------------------------------
    // 4. BC圧縮
    // ----------------------------------

    ScratchImage compressed;

    if (type == TextureType::Normal)
    {
        hr = Compress(
            mipChain.GetImages(),
            mipChain.GetImageCount(),
            mipChain.GetMetadata(),
            DXGI_FORMAT_BC5_UNORM,
            TEX_COMPRESS_DEFAULT,
            TEX_THRESHOLD_DEFAULT,
            compressed);
    }
    else
    {
        DXGI_FORMAT format = GetFormat(type);

        hr = Compress(
            device,
            mipChain.GetImages(),
            mipChain.GetImageCount(),
            mipChain.GetMetadata(),
            format,
            TEX_COMPRESS_DEFAULT,
            0.5f,
            compressed);
    }

    if (FAILED(hr))
        return hr;

    // ----------------------------------
    // 5. DDS生成
    // ----------------------------------

    Blob ddsBlob;

    hr = SaveToDDSMemory(
        compressed.GetImages(),
        compressed.GetImageCount(),
        compressed.GetMetadata(),
        DDS_FLAGS_NONE,
        ddsBlob);

    if (FAILED(hr))
        return hr;

    // ----------------------------------
    // 6. vectorへコピー
    // ----------------------------------

    outDDS.resize(ddsBlob.GetBufferSize());

    memcpy(
        outDDS.data(),
        ddsBlob.GetBufferPointer(),
        ddsBlob.GetBufferSize());

    return S_OK;
}

// テクスチャデータをDDSに変換してメモリに展開する関数
static const std::vector<uint8_t>& ConvertImageToDDSMemory(
    ID3D11Device* device,
    const GltfLoader::GltfModel& gltf,
    int imageIndex,
    TextureType type,
    std::unordered_map<TextureKey, std::vector<uint8_t>>& textureCache
)
{
    const auto& image = gltf.images[imageIndex];

    TextureKey key = { imageIndex, type };

    // すでにキャッシュ済み？
    auto it = textureCache.find(key);
    if (it != textureCache.end())
    {
        return it->second;
    }

    std::vector<uint8_t> dds;
    HRESULT hr = ConvertToDDSMemory(device, image.data.data(), image.width, image.height, image.bits, type, dds);
    if (FAILED(hr))
    {
        char msg[256];
        sprintf_s(msg, "DDS conversion failed (imageIndex=%d, type=%d, hr=0x%08X)", imageIndex, (int)type, hr);
        throw std::runtime_error(msg);
    }

    // 成功したのでキャッシュへ保存
    return textureCache.emplace(key, std::move(dds)).first->second;
}

// テクスチャをDDSに変換して登録する関数
static int RegisterTexture(
    ID3D11Device* device,
    const GltfLoader::GltfModel& gltf,
    int texIndex,
    TextureType type,
    std::vector<TextureEntry>& textures,
    std::unordered_map<TextureKey, int>& textureIndexMap,
    std::unordered_map<TextureKey, std::vector<uint8_t>>& textureCache
)
{
    int imageIndex = gltf.textures[texIndex].imageIndex;
    auto key = std::make_pair(imageIndex, type);

    // 既に登録済み？
    auto it = textureIndexMap.find(key);
    if (it != textureIndexMap.end())
        return it->second;

    // 未登録ならDDS生成
    const auto& dds = ConvertImageToDDSMemory(device, gltf, imageIndex, type, textureCache);

    int newIndex = (int)textures.size();

    textures.push_back({ type, dds });
    textureIndexMap[key] = newIndex;

    return newIndex;
}


// アニメーションクリップをAoS→SoAに変換する関数
static Imase::AnimationClip ConvertClipAoSToSoA(const GltfLoader::AnimationClip& src)
{
    Imase::AnimationClip dst;
    dst.name = src.name;
    dst.duration = src.duration;

    for (const GltfLoader::NodeAnimation& node : src.nodes)
    {
        // 移動
        if (!node.translations.empty())
        {
            AnimationChannelVec3 ch;
            ch.nodeIndex = static_cast<uint32_t>(node.nodeIndex);

            ch.times.reserve(node.translations.size());
            ch.values.reserve(node.translations.size());

            for (const GltfLoader::KeyframeVec3& key : node.translations)
            {
                ch.times.push_back(key.time);
                ch.values.push_back(key.value);
            }

            dst.translations.push_back(std::move(ch));
        }

        // 回転
        if (!node.rotations.empty())
        {
            AnimationChannelQuat ch;
            ch.nodeIndex = static_cast<uint32_t>(node.nodeIndex);

            ch.times.reserve(node.rotations.size());
            ch.values.reserve(node.rotations.size());

            for (const GltfLoader::KeyframeQuat& key : node.rotations)
            {
                ch.times.push_back(key.time);
                ch.values.push_back(key.value);
            }

            dst.rotations.push_back(std::move(ch));
        }

        // スケール
        if (!node.scales.empty())
        {
            AnimationChannelVec3 ch;
            ch.nodeIndex = static_cast<uint32_t>(node.nodeIndex);

            ch.times.reserve(node.scales.size());
            ch.values.reserve(node.scales.size());

            for (const GltfLoader::KeyframeVec3& key : node.scales)
            {
                ch.times.push_back(key.time);
                ch.values.push_back(key.value);
            }

            dst.scales.push_back(std::move(ch));
        }
    }

    return dst;
}

// 複数のアニメーションクリップをAoS→SoAに変換する関数
static std::vector<Imase::AnimationClip> ConvertAllClips(const std::vector<GltfLoader::AnimationClip>& clips)
{
    std::vector<Imase::AnimationClip> result;
    result.reserve(clips.size());

    for (const auto& clip : clips)
    {
        result.push_back(ConvertClipAoSToSoA(clip));
    }

    return result;
}

// ノードを並び替え『親→子』する関数
static void ReorderNodesRaw(
    std::vector<Imase::NodeInfo>& nodes,
    std::vector<GltfLoader::AnimationClip>& animationClips,
    std::vector<Imase::SkinInfo>& skins)
{
    const int nodeCount = (int)nodes.size();
    if (nodeCount == 0) return;

    //--------------------------------------------
    // 一時的に children リストを構築
    //--------------------------------------------
    std::vector<std::vector<int>> children(nodeCount);

    for (int i = 0; i < nodeCount; i++)
    {
        int parent = nodes[i].parentIndex;
        if (parent >= 0)
            children[parent].push_back(i);
    }

    //--------------------------------------------
    // ルート検出（parent == -1）
    //--------------------------------------------
    std::vector<int> roots;
    for (int i = 0; i < nodeCount; i++)
    {
        if (nodes[i].parentIndex < 0)
            roots.push_back(i);
    }

    //--------------------------------------------
    // DFS順を作る
    //--------------------------------------------
    std::vector<int> newOrder;
    newOrder.reserve(nodeCount);

    std::vector<bool> visited(nodeCount, false);

    std::function<void(int)> dfs = [&](int index)
        {
            if (visited[index])
                return;

            visited[index] = true;
            newOrder.push_back(index);

            for (int child : children[index])
                dfs(child);
        };

    for (int root : roots)
        dfs(root);

    if ((int)newOrder.size() != nodeCount)
    {
        throw std::runtime_error("Node reorder failed.");
    }

    //--------------------------------------------
    // old→new マップ作成
    //--------------------------------------------
    std::vector<int> oldToNew(nodeCount);

    for (int newIndex = 0; newIndex < nodeCount; newIndex++)
    {
        oldToNew[newOrder[newIndex]] = newIndex;
    }

    //--------------------------------------------
    // ノード並び替え
    //--------------------------------------------
    std::vector<Imase::NodeInfo> reordered(nodeCount);

    for (int i = 0; i < nodeCount; i++)
    {
        reordered[i] = nodes[newOrder[i]];
    }

    nodes = std::move(reordered);

    //--------------------------------------------
    // parentIndex 更新
    //--------------------------------------------
    for (auto& node : nodes)
    {
        if (node.parentIndex >= 0)
            node.parentIndex = oldToNew[node.parentIndex];
    }

    //--------------------------------------------
    // animation 更新
    //--------------------------------------------
    for (auto& clip : animationClips)
    {
        for (auto& nodeAnim : clip.nodes)
        {
            nodeAnim.nodeIndex = oldToNew[nodeAnim.nodeIndex];
        }
    }
    //--------------------------------------------
    // skin 更新
    //--------------------------------------------
    for (auto& skin : skins)
    {
        if (skin.rootNode >= 0)
            skin.rootNode = oldToNew[skin.rootNode];

        for (auto& joint : skin.jointIndices)
        {
            joint = oldToNew[joint];
        }
    }
}

// glTFファイルの情報取得関数
static void ConvertImdl(
    ID3D11Device* device,
    GltfLoader::GltfModel& gltf,
    std::vector<MaterialInfo>& materials,
    std::vector<SubMeshInfo>& subMeshes,
    std::vector<MeshGroupInfo>& meshGroups,
    std::vector<NodeInfo>& nodes,
    std::vector<TextureEntry>& textures,
    std::vector<VertexPositionNormalTextureTangent>& vertexBuffer,
    std::vector<uint32_t>& indexBuffer,
    std::vector<Imase::AnimationClip>& animationClips,
    std::vector<SkinInfo>& skins,
    std::unordered_map<TextureKey, std::vector<uint8_t>>& textureCache
)
{
    std::unordered_map<TextureKey, int> textureIndexMap;

    // ノードを並び替え『親→子』する
    ReorderNodesRaw(gltf.nodes, gltf.animationClips, gltf.skins);

    // ----- デフォルトマテリアル追加 ---- //
    MaterialInfo defaultMat;
    materials.push_back(defaultMat);

    // マテリアル登録
    for (const auto& mat : gltf.materials)
    {
        MaterialInfo m = mat;

        // 各テクスチャ登録
        if (mat.baseColorTexIndex >= 0)
        {
            m.baseColorTexIndex = RegisterTexture(device, gltf, mat.baseColorTexIndex, TextureType::BaseColor, textures, textureIndexMap, textureCache);
        }
        if (mat.normalTexIndex >= 0)
        {
            m.normalTexIndex = RegisterTexture(device, gltf, mat.normalTexIndex, TextureType::Normal, textures, textureIndexMap, textureCache);
        }
        if (mat.metalRoughTexIndex >= 0)
        {
            m.metalRoughTexIndex = RegisterTexture(device, gltf, mat.metalRoughTexIndex, TextureType::MetalRough, textures, textureIndexMap, textureCache);
        }
        if (mat.emissiveTexIndex >= 0)
        {
            m.emissiveTexIndex = RegisterTexture(device, gltf, mat.emissiveTexIndex, TextureType::Emissive, textures, textureIndexMap, textureCache);
        }

        materials.push_back(m);
    }

    // ノード
    nodes.resize(gltf.nodes.size());
    for (size_t i = 0; i < gltf.nodes.size(); i++)
    {
        nodes[i] = gltf.nodes[i];
    }

    // メッシュグループ
    meshGroups.resize(gltf.meshGroups.size());
    for (size_t i = 0; i < gltf.meshGroups.size(); i++)
    {
        meshGroups[i] = gltf.meshGroups[i];
    }

    // サブメッシュ
    subMeshes.resize(gltf.subMeshes.size());
    for (size_t i = 0; i < gltf.subMeshes.size(); i++)
    {
        subMeshes[i] = gltf.subMeshes[i];
    }

    // 頂点
    for (const auto& vertex : gltf.vertices)
    {
        vertexBuffer.push_back(vertex);
    }

    // インデックス
    for (const auto& index : gltf.indices)
    {
        indexBuffer.push_back(index);
    }

    // アニメション
    animationClips = ConvertAllClips(gltf.animationClips);

    // スキン
    skins = gltf.skins;

    return;
}

// テクスチャデータ作成
static std::vector<uint8_t> BuildTextureChunk(const std::vector<TextureEntry>& textures)
{
    BinaryWriter writer;

    writer.WriteUInt32(static_cast<uint32_t>(textures.size()));

    for (const auto& tex : textures)
    {
        writer.WriteUInt32(static_cast<uint32_t>(tex.type));
        writer.WriteUInt32(static_cast<uint32_t>(tex.data.size()));
        writer.WriteBytes(tex.data.data(), tex.data.size());
    }

    return writer.GetBuffer();
}

// マテリアル情報のシリアライズ関数
inline void SerializeMaterial(BinaryWriter& writer, const MaterialInfo& m)
{
    writer.WriteFloat(m.diffuseColor.x);
    writer.WriteFloat(m.diffuseColor.y);
    writer.WriteFloat(m.diffuseColor.z);
    writer.WriteFloat(m.diffuseColor.w);

    writer.WriteFloat(m.metallicFactor);
    writer.WriteFloat(m.roughnessFactor);

    writer.WriteFloat(m.emissiveColor.x);
    writer.WriteFloat(m.emissiveColor.y);
    writer.WriteFloat(m.emissiveColor.z);

    writer.WriteInt32(m.baseColorTexIndex);
    writer.WriteInt32(m.normalTexIndex);
    writer.WriteInt32(m.metalRoughTexIndex);
    writer.WriteInt32(m.emissiveTexIndex);
}

// マテリアル情報データ作成
static std::vector<uint8_t> BuildMaterialChunk(const std::vector<MaterialInfo>& materials)
{
    BinaryWriter writer;

    writer.WriteUInt32(static_cast<uint32_t>(materials.size()));

    for (const auto& m : materials)
    {
        SerializeMaterial(writer, m);
    }

    return writer.GetBuffer();
}

// メッシュ情報のシリアライズ関数
inline void SerializeSubMesh(BinaryWriter& writer, const SubMeshInfo& m)
{
    writer.WriteUInt32(m.startIndex);
    writer.WriteUInt32(m.indexCount);
    writer.WriteUInt32(m.materialIndex);
}

// サブメッシュ情報データ作成
static std::vector<uint8_t> BuildSubMeshChunk(const std::vector<SubMeshInfo>& meshes)
{
    BinaryWriter writer;

    writer.WriteUInt32(static_cast<uint32_t>(meshes.size()));

    for (const auto& m : meshes)
    {
        SerializeSubMesh(writer, m);
    }

    return writer.GetBuffer();
}

// メッシュグループ情報のシリアライズ関数
inline void SerializeMeshGroup(BinaryWriter& writer, const MeshGroupInfo& m)
{
    writer.WriteUInt32(m.subMeshStart);
    writer.WriteUInt32(m.subMeshCount);
}

// メッシュグループ情報データ作成
static std::vector<uint8_t> BuildMeshGroupChunk(const std::vector<MeshGroupInfo>& groups)
{
    BinaryWriter writer;

    writer.WriteUInt32(static_cast<uint32_t>(groups.size()));

    for (const auto& m : groups)
    {
        SerializeMeshGroup(writer, m);
    }

    return writer.GetBuffer();
}

// ノード情報のシリアライズ関数
inline void SerializeNode(BinaryWriter& writer, const NodeInfo& m)
{
    writer.WriteInt32(m.meshGroupIndex);
    writer.WriteInt32(m.parentIndex);
    writer.WriteInt32(m.skinIndex);

    writer.WriteFloat(m.defaultTranslation.x);
    writer.WriteFloat(m.defaultTranslation.y);
    writer.WriteFloat(m.defaultTranslation.z);

    writer.WriteFloat(m.defaultRotation.x);
    writer.WriteFloat(m.defaultRotation.y);
    writer.WriteFloat(m.defaultRotation.z);
    writer.WriteFloat(m.defaultRotation.w);

    writer.WriteFloat(m.defaultScale.x);
    writer.WriteFloat(m.defaultScale.y);
    writer.WriteFloat(m.defaultScale.z);
}

// ノード情報データ作成
static std::vector<uint8_t> BuildNodeChunk(const std::vector<NodeInfo>& nodes)
{
    BinaryWriter writer;

    writer.WriteUInt32(static_cast<uint32_t>(nodes.size()));

    for (const auto& m : nodes)
    {
        SerializeNode(writer, m);
    }

    return writer.GetBuffer();
}

// 頂点データのシリアライズ関数
inline void SerializeVertex(BinaryWriter& writer, const VertexPositionNormalTextureTangent& v)
{
    writer.WriteFloat(v.position.x);
    writer.WriteFloat(v.position.y);
    writer.WriteFloat(v.position.z);

    writer.WriteFloat(v.normal.x);
    writer.WriteFloat(v.normal.y);
    writer.WriteFloat(v.normal.z);

    writer.WriteFloat(v.texcoord.x);
    writer.WriteFloat(v.texcoord.y);

    writer.WriteFloat(v.tangent.x);
    writer.WriteFloat(v.tangent.y);
    writer.WriteFloat(v.tangent.z);
    writer.WriteFloat(v.tangent.w);

    writer.WriteUInt32(v.joint.x);
    writer.WriteUInt32(v.joint.y);
    writer.WriteUInt32(v.joint.z);
    writer.WriteUInt32(v.joint.w);

    writer.WriteFloat(v.weight.x);
    writer.WriteFloat(v.weight.y);
    writer.WriteFloat(v.weight.z);
    writer.WriteFloat(v.weight.w);
}

// 頂点データ作成
static std::vector<uint8_t> BuildVertexChunk(
    const std::vector<VertexPositionNormalTextureTangent>& vertices)
{
    BinaryWriter writer;

    writer.WriteUInt32(static_cast<uint32_t>(vertices.size()));

    for (const auto& v : vertices)
    {
        SerializeVertex(writer, v);
    }

    return writer.GetBuffer();
}

// インデックスデータ作成
static std::vector<uint8_t> BuildIndexChunk(const std::vector<uint32_t>& indices)
{
    BinaryWriter writer;
    writer.WriteCountedArray(indices);
    return writer.GetBuffer();
}

// アニメションチャンネル（x, y, z）のシリアライズ関数
inline void SerializeChannelVec3(BinaryWriter& writer, const AnimationChannelVec3& vec3)
{
    writer.WriteUInt32(vec3.nodeIndex);


    writer.WriteUInt32(static_cast<uint32_t>(vec3.times.size()));
    for (const auto& time : vec3.times)
    {
        writer.WriteFloat(time);
    }

    writer.WriteCountedArray(vec3.values);
}

// アニメションチャンネル（x, y, z, w）のシリアライズ関数
inline void SerializeChannelQuat(BinaryWriter& writer, const AnimationChannelQuat& quat)
{
    writer.WriteUInt32(quat.nodeIndex);

    writer.WriteUInt32(static_cast<uint32_t>(quat.times.size()));
    for (const auto& time : quat.times)
    {
        writer.WriteFloat(time);
    }

    writer.WriteCountedArray(quat.values);
}

// アニメションクリップ情報のシリアライズ関数
inline void SerializeAnimationClip(BinaryWriter& writer, const Imase::AnimationClip& clip)
{
    writer.WriteString(clip.name);
    writer.WriteFloat(clip.duration);

    writer.WriteUInt32(static_cast<uint32_t>(clip.translations.size()));
    for (const auto& trans : clip.translations)
    {
        SerializeChannelVec3(writer, trans);
    }

    writer.WriteUInt32(static_cast<uint32_t>(clip.rotations.size()));
    for (const auto& rot : clip.rotations)
    {
        SerializeChannelQuat(writer, rot);
    }

    writer.WriteUInt32(static_cast<uint32_t>(clip.scales.size()));
    for (const auto& scale : clip.scales)
    {
        SerializeChannelVec3(writer, scale);
    }
}

// アニメションデータ作成
static std::vector<uint8_t> BuildAnimationChunk(const std::vector<Imase::AnimationClip>& animationClips)
{
    BinaryWriter writer;

    writer.WriteUInt32(static_cast<uint32_t>(animationClips.size()));

    for (const auto& clip : animationClips)
    {
        SerializeAnimationClip(writer, clip);
    }

    return writer.GetBuffer();
}

// 頂点データのシリアライズ関数
inline void SerializeSkin(BinaryWriter& writer, const SkinInfo& skin)
{
    writer.WriteInt32(skin.rootNode);
    writer.WriteCountedArray<uint32_t>(skin.jointIndices);
    writer.WriteCountedArray<DirectX::XMFLOAT4X4>(skin.inverseBindMatrices);
}

// スキンデータ作成
static std::vector<uint8_t> BuildSkinChunk(const std::vector<SkinInfo>& skins)
{
    BinaryWriter writer;

    writer.WriteUInt32(static_cast<uint32_t>(skins.size()));

    for (const auto& skin : skins)
    {
        SerializeSkin(writer, skin);
    }

    return writer.GetBuffer();
}

// ファイルへの出力関数
static HRESULT OutputImdl( const std::filesystem::path& path,
                           std::vector<MaterialInfo>& materials,
                           std::vector<SubMeshInfo>& subMeshes,
                           std::vector<MeshGroupInfo>& meshGroups,
                           std::vector<NodeInfo>& nodes,
                           std::vector<TextureEntry>& textures,
                           std::vector<VertexPositionNormalTextureTangent>& vertexBuffer,
                           std::vector<uint32_t>& indexBuffer,
                           std::vector<Imase::AnimationClip>& animationClips,
                           std::vector<SkinInfo>& skins
)
{
    // 出力ファイルオープン
    std::ofstream ofs(path.c_str(), std::ios::binary);

    if (!ofs.is_open())
    {
        std::wcout << "Could not open " << path.c_str() << std::endl;
        return E_FAIL;
    }

    // チャンク数
    uint32_t chunkCount = 0;

    // ----- Header ----- //
    // いったんヘッダを書き込む（仮）
    FileHeader fileHeader{};
    fileHeader.magic = 0x4C444D49; // 'IMDL'
    fileHeader.version = 1;
    fileHeader.chunkCount = 0;  // ←後で書き込む

    ofs.write((char*)&fileHeader, sizeof(fileHeader));

    // ----- Texture ----- //
    WriteChunk(ofs, chunkCount, CHUNK_TEXTURE, BuildTextureChunk(textures));

    // ----- Material ----- //
    WriteChunk(ofs, chunkCount, CHUNK_MATERIAL, BuildMaterialChunk(materials));

    // ----- SubMesh ----- //
    WriteChunk(ofs, chunkCount, CHUNK_SUBMESH, BuildSubMeshChunk(subMeshes));

    // ----- MeshGroup ----- //
    WriteChunk(ofs, chunkCount, CHUNK_MESHGROUP, BuildMeshGroupChunk(meshGroups));

    // ----- Node ----- //
    WriteChunk(ofs, chunkCount, CHUNK_NODE, BuildNodeChunk(nodes));

    // ----- Vertex ----- //
    WriteChunk(ofs, chunkCount, CHUNK_VERTEX, BuildVertexChunk(vertexBuffer));

    // ----- Index ----- //
    WriteChunk(ofs, chunkCount, CHUNK_INDEX, BuildIndexChunk(indexBuffer));

    // ----- ANIMATION ----- //
    WriteChunk(ofs, chunkCount, CHUNK_ANIMATION, BuildAnimationChunk(animationClips));

    // ----- SKIN ----- //
    WriteChunk(ofs, chunkCount, CHUNK_SKIN, BuildSkinChunk(skins));

    // チャンク数を追加したヘッダを書き戻す
    fileHeader.chunkCount = chunkCount;
    ofs.seekp(0);
    ofs.write((char*)&fileHeader, sizeof(fileHeader));

    return S_OK;
}

// 文字列にスペースなどが入っていた場合に_に置き換えるサニタイズ関数
static std::string SanitizeIdentifier(const std::string& name)
{
    std::string result;

    for (char c : name)
    {
        // 文字が英数字か調べる
        if (std::isalnum(static_cast<unsigned char>(c)))
        {
            result += c;
        }
        else
        {
            result += '_';
        }
    }

    // 先頭が数字なら _
    if (!result.empty() && std::isdigit(static_cast<unsigned char>(result[0])))
    {
        result.insert(result.begin(), '_');
    }

    // 連続する _ をまとめる
    std::string cleaned;
    bool prevUnderscore = false;

    for (char c : result)
    {
        if (c == '_')
        {
            if (!prevUnderscore)
            {
                cleaned += c;
                prevUnderscore = true;
            }
        }
        else
        {
            cleaned += c;
            prevUnderscore = false;
        }
    }

    return cleaned;
}

// ----- アニメーションインデックス書き出し ----- //
HRESULT OutputIndexHeader(std::filesystem::path path, const std::vector<Imase::AnimationClip>& animationClips)
{
    std::ofstream ofs(path.c_str());

    if (!ofs.is_open())
    {
        std::wcout << "Could not open " << path.c_str() << std::endl;
        return E_FAIL;
    }

    ofs << "#pragma once\n\n";

    ofs << "// Auto-generated file. Do not edit.\n\n";

    ofs << "namespace AnimationIndex\n";
    ofs << "{\n";

    // ----- enum -----
    ofs << "    enum : int\n";
    ofs << "    {\n";

    for (size_t i = 0; i < animationClips.size(); ++i)
    {
        ofs << "        " << SanitizeIdentifier(animationClips[i].name) << " = " << i << ",\n";
    }

    ofs << "        Count = " << animationClips.size() << "\n";
    ofs << "    };\n\n";

    // ----- 名前テーブル（デバッグ用）-----
    ofs << "    static const char* Names[] =\n";
    ofs << "    {\n";

    for (const auto& clip : animationClips)
    {
        ofs << "        \"" << clip.name << "\",\n";
    }

    ofs << "    };\n";

    ofs << "}\n";

    return S_OK;
}

int wmain(int argc, wchar_t* wargv[])
{
    HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
    if (FAILED(hr))
        return 1;

    Microsoft::WRL::ComPtr<ID3D11Device> device;

    // DirectXのデバイスを作成（テクスチャ圧縮で使用）
    CreateD3DDevice(device.GetAddressOf());

    std::vector<std::string> args;
    std::vector<char*> argv;

    // 文字コードをUTF-8へ変換する
    for (int i = 0; i < argc; ++i)
    {
        args.push_back(WStringToUtf8(wargv[i]));
    }

    for (auto& s : args)
    {
        argv.push_back(s.data());
    }

    std::filesystem::path input, output;

    // 入力ファイル名と出力ファイル名を取得
    if (AnalyzeOption(argc, argv.data(), input, output)) return 1;

    // ----- 情報取得 ----- //

    std::vector<MaterialInfo> materials;    // マテリアル
    std::vector<TextureEntry> textures;     // テクスチャ
    std::vector<SubMeshInfo> subMeshes;     // サブメッシュ
    std::vector<MeshGroupInfo> meshGroups;  // メッシュグループ
    std::vector<NodeInfo> nodes;            // ノード
    std::vector<VertexPositionNormalTextureTangent> vertexBuffer;   // 頂点
    std::vector<uint32_t> indexBuffer;      // インデックス
    std::vector<Imase::AnimationClip> animationClips;   // アニメションクリップ  
    std::vector<SkinInfo> skins;            // スキン  
    std::unordered_map<TextureKey, std::vector<uint8_t>> textureCache;    // テクスチャキャッシュ

    // glTFファイルのロード
    GltfLoader::GltfModel gltf = GltfLoader::Load(input);

    // Imdl用の情報に変換
    ConvertImdl(device.Get(), gltf,
        materials, subMeshes, meshGroups, nodes, textures, vertexBuffer, indexBuffer, animationClips, skins, textureCache);

    // ----- 書き出し ----- //

    OutputImdl(output, materials, subMeshes, meshGroups, nodes, textures, vertexBuffer, indexBuffer, animationClips, skins);

    // ----- アニメーションインデックス書き出し ----- //
    if (!animationClips.empty())
    {
        OutputIndexHeader(MakeOutputPath(input, "_anim.h"), animationClips);
    }

    CoUninitialize();

    return 0;
}

