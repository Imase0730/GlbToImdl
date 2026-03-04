//--------------------------------------------------------------------------------------
// File: ObjToImdl.cpp
//
// wavefront形式のファイルを独自形式のモデルデータ(.imdl)に変換するツール
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
//   uint32_t chunkCount // 7
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
// ------------------------------------------------------------ //

#include <iostream>
#include <windows.h>
#include <wrl.h>
#include <vector>
#include <filesystem>
#include <string>
#include <d3d11.h>
#include "DirectXTex.h"
#include "../Common/cxxopts.hpp"
#include "../Common/BinaryWriter.h"
#include "../Common/ChunkIO.h"
#include "../Common/Imdl.h"
#include "../Common/MeshTangentGenerator.h"

using namespace DirectX;
using namespace Imase;

#pragma comment(lib, "d3d11.lib")

// 面の各頂点を構成するインデックス
struct FaceIndex
{
    int v;  // 位置
    int vt; // テクスチャ座標
    int vn; // 法線

    bool operator==(const FaceIndex& other) const
    {
        return v == other.v && vt == other.vt && vn == other.vn;
    }
};

// ハッシュ値を生成する関数
namespace std
{
    template <>
    struct hash<FaceIndex>
    {
        size_t operator()(const FaceIndex& f) const
        {
            size_t h1 = std::hash<int>()(f.v);
            size_t h2 = std::hash<int>()(f.vt);
            size_t h3 = std::hash<int>()(f.vn);

            // ハッシュ合成
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}

// 面（三角形）
struct Face
{
    FaceIndex faceIndices[3];
};

// サブメッシュ
struct SubMesh
{
    std::string material;      // マテリアル名
    std::vector<Face> faces;    // 面（三角形）情報
};

// メッシュ
struct Mesh
{
    std::vector<SubMesh> subMeshs;  // サブメッシュ
};

// obj形式の情報取得用構造体
struct Object
{
    std::filesystem::path mtllib;               // マテリアルファイル名
    std::vector<DirectX::XMFLOAT3> positions;   // 位置
    std::vector<DirectX::XMFLOAT3> normals;     // 法線
    std::vector<DirectX::XMFLOAT2> texcoords;   // テクスチャ座標
    std::vector<Mesh> meshes;                   // メッシュ
};

// パス名付きファイル名のファイル名を取得する関数
static std::wstring GetFileNameOnly(const std::wstring& path)
{
    return std::filesystem::path(path).filename().wstring();
}

// ファイルから読み込む関数（XMFLOAT2）
static XMFLOAT2 ReadFloat2(std::istringstream& iss)
{
    XMFLOAT2 val = {};
    iss >> val.x >> val.y;
    return val;
}

// ファイルから読み込む関数（XMFLOAT3）
static XMFLOAT3 ReadFloat3(std::istringstream& iss)
{
    XMFLOAT3 val = {};
    iss >> val.x >> val.y >> val.z;
    return val;
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

// UTF-8 → UTF-16 変換
static std::wstring StringToWString(const std::string& str)
{
    if (str.empty()) return std::wstring();

    int size_needed = MultiByteToWideChar(
        CP_UTF8,                // UTF-8
        0,
        str.c_str(),
        (int)str.size(),
        nullptr,
        0);

    std::wstring wstr(size_needed, 0);

    MultiByteToWideChar(
        CP_UTF8,
        0,
        str.c_str(),
        (int)str.size(),
        &wstr[0],
        size_needed);

    return wstr;
}

// ヘルプ表示
static void Help()
{
    std::cout <<
        "Usage:\n"
        "  ObjToImdl <input.obj> [-o output.imdl]\n\n"
        "Options:\n"
        "  -o, --output <file>   Output file\n"
        "  -h, --help            Show help\n";
}

// 引数から入力ファイル名と出力ファイル名を取得する関数
static int AnalyzeOption(int argc, char* argv[], std::filesystem::path& input, std::filesystem::path& output)
{
    // cxxoptsで引数解析
    cxxopts::Options options("ObjToMdl");
    options.add_options()
        ("input", "Input model file (.obj)",
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
            output = std::filesystem::path(input);
            output.replace_extension(".imdl");
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

// 面の各頂点を構成するインデックス取得関数
static std::vector<FaceIndex> ParseFaceLine(std::istringstream& iss, Object& object)
{
    auto fixIndex = [&](int raw, int size) {
        if (raw > 0)  return raw - 1;
        if (raw < 0)  return size + raw;
        throw std::runtime_error("OBJ index cannot be zero");
    };

    std::vector<FaceIndex> result;

    std::string token;
    while (iss >> token)
    {
        FaceIndex idx{ -1, -1, -1 };

        std::istringstream tss(token);
        std::string s;

        // v
        if (std::getline(tss, s, '/')) idx.v = fixIndex(std::stoi(s), static_cast<int>(object.positions.size()));

        // vt
        if (std::getline(tss, s, '/'))
        {
            if (!s.empty()) idx.vt = fixIndex(std::stoi(s), static_cast<int>(object.texcoords.size()));
        }

        // vn
        if (std::getline(tss, s, '/'))
        {
            if (!s.empty()) idx.vn = fixIndex(std::stoi(s), static_cast<int>(object.normals.size()));
        }

        result.push_back(idx);
    }

    return result;
}

// objファイルの情報取得関数
static int AnalyzeObj(const std::filesystem::path& fname, Object& object)
{
    // objファイルのオープン
    std::ifstream ifs(fname);

    if (!ifs)
    {
        // ファイルのオープン失敗
        std::wcout << "Could not open " << fname << std::endl;
        return 1;
    }

    std::string object_name;
 
    Mesh* currentMesh = nullptr;
    SubMesh* currentSubMesh = nullptr;

    std::string line;
    while (std::getline(ifs, line))
    {
        // 空行やコメントをスキップ
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);

        // 先頭のトークン
        std::string type;
        iss >> type;

        // オブジェクト名
        if (type == "o")
        {
            iss >> object_name;
            object.meshes.emplace_back();
            currentMesh = &object.meshes.back();
            currentSubMesh = nullptr;
        }

        // 頂点
        else if (type == "v")
        {
            object.positions.push_back(ReadFloat3(iss));
        }

        // 法線
        else if (type == "vn")
        {
            object.normals.push_back(ReadFloat3(iss));
        }

        // テクスチャ座標
        else if (type == "vt")
        {
            // BlenderのV座標は上が＋
            XMFLOAT2 uv = ReadFloat2(iss);
            uv.y = 1.0f - uv.y;
            object.texcoords.push_back(uv);
        }

        // 面情報
        else if (type == "f")
        {
            // meshがない
            if (!currentMesh)
            {
                object.meshes.emplace_back();
                currentMesh = &object.meshes.back();
            }

            // subMeshがない
            if (!currentSubMesh)
            {
                currentMesh->subMeshs.emplace_back();
                currentSubMesh = &currentMesh->subMeshs.back();
                currentSubMesh->material = "default";
            }

            // 面の各頂点を構成するインデックスを取得
            std::vector<FaceIndex> result = ParseFaceLine(iss, object);

            // 四角形の場合は三角形２枚に置き換える
            for (size_t i = 0; i < result.size() - 2; i++)
            {
                // 反時計回りが表
                Face face{ result[0], result[i + 1], result[i + 2] };
                currentSubMesh->faces.push_back(face);
            }
        }

        // マテリアル名
        else if (type == "usemtl")
        {
            // meshがない
            if (!currentMesh)
            {
                object.meshes.emplace_back();
                currentMesh = &object.meshes.back();
            }

            // メッシュを追加
            currentMesh->subMeshs.emplace_back();
            currentSubMesh = &currentMesh->subMeshs.back();

            // マテリアル名
            iss >> currentSubMesh->material;
        }

        // マテリアルファイル名
        else if (type == "mtllib")
        {
            std::string name;
            iss >> name;
            // utf8 → path
            object.mtllib = std::filesystem::path(name);
        }
    }

    return 0;
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
        return DXGI_FORMAT_BC1_UNORM;

    case TextureType::Emissive:
        return DXGI_FORMAT_BC7_UNORM_SRGB;

    default:
        return DXGI_FORMAT_BC7_UNORM;
    }
}

// テクスチャデータをDDS形式にしてメモリに書き出す関数
// ※法線マップのみY要素を反転する（TexConvの-invertY相当）
static HRESULT ConvertToDDSMemory(
    ID3D11Device* device,
    ScratchImage scratch,
    size_t width,
    size_t height,
    TextureType type,
    std::vector<uint8_t>& outDDS)
{
    HRESULT hr;

    // ----------------------------------
    // 1. 法線マップのみY反転（TexConv.exeの-inverty相当）
    // ----------------------------------
    //if (type == TextureType::Normal)
    //{
    //    ScratchImage flipped;

    //    auto invertY = [](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t y) noexcept
    //        {
    //            for (size_t x = 0; x < width; ++x)
    //            {
    //                XMVECTOR v = inPixels[x];

    //                // G成分反転
    //                v = XMVectorSetY(v, 1.0f - XMVectorGetY(v));

    //                outPixels[x] = v;
    //            }
    //        };

    //    HRESULT hr = TransformImage(
    //        scratch.GetImages(),
    //        scratch.GetImageCount(),
    //        scratch.GetMetadata(),
    //        invertY,
    //        flipped);

    //    scratch = std::move(flipped);
    //}

    // ----------------------------------
    // 2. ミップ生成
    // ----------------------------------
    ScratchImage mipChain;

    hr = GenerateMipMaps(
        scratch.GetImages(),
        scratch.GetImageCount(),
        scratch.GetMetadata(),
        TEX_FILTER_FANT,
        0,
        mipChain);

    if (FAILED(hr))
        return hr;

    // ----------------------------------
    // 3. GPU圧縮
    // ----------------------------------
    ScratchImage compressed;

    DXGI_FORMAT format = GetFormat(type);
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
    // 4. メモリ内にDDS生成
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
    // 5. vector にコピー
    // ----------------------------------
    outDDS.resize(ddsBlob.GetBufferSize());
    memcpy(outDDS.data(),
        ddsBlob.GetBufferPointer(),
        ddsBlob.GetBufferSize());

    return S_OK;
}

// テクスチャをDDSに変換して登録する関数
static int RegisterTexture(
    ID3D11Device* device,
    const std::filesystem::path& path,
    TextureType type,
    std::vector<TextureEntry>& textures,
    std::map<std::pair<std::wstring, TextureType>, int>& textureIndexMap)
{
    auto key = std::make_pair(path.c_str(), type);

    // 既に登録済み？
    auto it = textureIndexMap.find(key);
    if (it != textureIndexMap.end())
    {
        return it->second;
    }

    // ----- テクスチャの読み込み→DDSの変換→登録 ----- //
    ScratchImage image;
    TexMetadata metadata;

    // PNG読み込み（WIC使用）
    HRESULT hr = LoadFromWICFile(path.c_str(), WIC_FLAGS_NONE, &metadata, image);

    // 読み込み失敗
    if (FAILED(hr))
        return -1;

    // DDSへ変換
    std::vector<uint8_t> dds;
    hr = ConvertToDDSMemory(device, std::move(image), metadata.width, metadata.height, type, dds);

    // 変換失敗
    if (FAILED(hr))
        return -1;

    int newIndex = (int)textures.size();

    textures.push_back({ type, dds });
    textureIndexMap[key] = newIndex;

    return newIndex;
}

// テクスチャファイル名の取得関数（オプションなどは除去）
static std::string ExtractTextureFilename(std::istringstream& iss)
{
    std::string rest;
    std::getline(iss >> std::ws, rest);

    // オプション除去
    std::istringstream optStream(rest);
    std::string token;
    std::string filename;

    while (optStream >> token)
    {
        if (token[0] == '-')
        {
            optStream >> token; // オプション値をスキップ
        }
        else
        {
            filename = token;
            std::string remain;
            std::getline(optStream, remain);
            filename += remain;
            break;
        }
    }

    // 先頭のタブを除去
    filename.erase(0, filename.find_first_not_of(" \t"));

    return filename;
}

// mtlファイルの情報取得関数
static int AnalyzeMtl( ID3D11Device* device,
                       const std::filesystem::path& path,
                       std::vector<MaterialInfo>& materials,
                       std::unordered_map<std::string, uint32_t>& materialIndexMap,
                       std::vector<TextureEntry>& textures )
{
    // mtlファイルのオープン
    std::ifstream ifs(path.c_str());

    if (!ifs)
    {
        // ファイルのオープン失敗
        std::wcout << "Could not open " << path.c_str() << std::endl;
        return 1;
    }

    // テクスチャ登録位置を保存するコンテナ
    std::map<std::pair<std::wstring, TextureType>, int> textureIndexMap;

    uint32_t m_index = 0;
    int32_t t_index = 0;

    std::string line;
    while (std::getline(ifs, line))
    {
        // 空行やコメントをスキップ
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);

        // 先頭のトークン
        std::string type;
        iss >> type;

        // マテリアルファイル名
        if (type == "newmtl")
        {
            std::string name;
            iss >> name;
            materials.resize(materials.size() + 1);
            materialIndexMap[name] = m_index;
            m_index++;
        }

        // ディフューズ色
        else if (type == "Kd")
        {
            XMFLOAT3 color = ReadFloat3(iss);
            materials.back().diffuseColor = { color.x, color.y, color.z, 1.0f};
        }

        // スペキュラ色
        else if (type == "Ks")
        {
            // メタリックにIORレベルを入れる（プログラム側ではスペキュラ色の要素として使用）
            // （blenderではメタリックはNsで出力されるのでroughnessへ反映）
            XMFLOAT3 specularColor = ReadFloat3(iss);
            materials.back().metallicFactor = specularColor.x;
        }

        // スペキュラパワー
        else if (type == "Ns")
        {
            float Ns;
            iss >> Ns;
            // ※blenderの粗さ【roughness】はobj形式には反映されない
            // blenderではNsの値はメタリックが反映される
            // Ns = 1000 x (1 - metallic)^2
            // Nsを使ってroughnessを算出する
            // シェダー側では【spcularPower = 1000 x (1 - roughness)^2】で計算
            float roughness = 1.0f - (sqrtf(Ns / 1000.0f));
            materials.back().roughnessFactor = roughness;
        }

        // エミッシブ色
        else if (type == "Ke")
        {
            materials.back().emissiveColor = ReadFloat3(iss);
        }

        // テクスチャ（ベースカラー）
        else if (type == "map_Kd")
        {
            if (!materials.empty())
            {
                // 最後のトークンをファイル名として取得
                std::string name = ExtractTextureFilename(iss);

                // エラー
                if (name.empty()) return -1;

                std::filesystem::path p(name);

                // pngファイルが存在？
                if (!std::filesystem::exists(p))
                {
                    // 存在しない場合はobjと一緒のフォルダに変更
                    std::filesystem::path baseDir = path.parent_path();
                    p = baseDir / p.filename();
                    // そこにもpngがない
                    if (!std::filesystem::exists(p))
                    {
                        std::wcerr << L"Texture not found: " << p.wstring() << std::endl;
                        return -1;
                    }
                }

                // テクスチャ登録
                materials.back().baseColorTexIndex = RegisterTexture(
                    device, p, TextureType::BaseColor, textures, textureIndexMap);
            }
        }

        // テクスチャ（法線マップ）
        else if (type == "map_Bump")
        {
            if (!materials.empty())
            {
                // 最後のトークンをファイル名として取得
                std::string name = ExtractTextureFilename(iss);
                
                // エラー
                if (name.empty()) return -1;

                std::filesystem::path p(name);

                // pngファイルが存在？
                if (!std::filesystem::exists(p))
                {
                    // 存在しない場合はobjと一緒のフォルダに変更
                    std::filesystem::path baseDir = path.parent_path();
                    p = baseDir / p.filename();
                    // そこにもpngがない
                    if (!std::filesystem::exists(p))
                    {
                        std::wcerr << L"Texture not found: " << p.wstring() << std::endl;
                        return -1;
                    }
                }
                // テクスチャ登録
                materials.back().normalTexIndex = RegisterTexture(
                    device, p, TextureType::Normal, textures, textureIndexMap);
            }
        }
    }

    return 0;
}

// 頂点データ作成関数
static VertexPositionNormalTextureTangent MakeVertex(Object& object, const FaceIndex& face)
{
    VertexPositionNormalTextureTangent v = {};

    v.position = object.positions[face.v];

    if (face.vn >= 0)
    {
        // 法線を正規化
        XMVECTOR n = XMLoadFloat3(&object.normals[face.vn]);
        n = XMVector3Normalize(n);
        XMStoreFloat3(&v.normal, n);
    }
    else
    {
        // ダミー
        v.normal = XMFLOAT3(0.0f, 0.0f, 1.0f);
    }

    v.texcoord = (face.vt >= 0) ? object.texcoords[face.vt] : XMFLOAT2(0.0f, 0.0f);

    return v;
}

// Object情報からImdlに必要な情報を取得する関数
static void ConvertImdl( Object& object,
                         std::unordered_map<std::string, uint32_t>& materialIndexMap,
                         std::vector<SubMeshInfo>& subMeshInfo,
                         std::vector<MeshGroupInfo>& meshGroupInfo,
                         std::vector<NodeInfo>& nodeInfo,
                         std::vector<VertexPositionNormalTextureTangent>& vertexBuffer,
                         std::vector<uint32_t>& indexBuffer )
{
    uint32_t meshStart = 0;

    for (auto& mesh : object.meshes)
    {
        // メッシュ単位で頂点共有する
        std::unordered_map<FaceIndex, uint32_t> indexMap;

        NodeInfo node = {};
        node.meshGroupIndex = static_cast<int32_t>(meshGroupInfo.size());
        node.parentIndex = -1;
        node.defaultTranslation = { 0.0f, 0.0f, 0.0f };
        node.defaultRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        node.defaultScale = { 1.0f, 1.0f, 1.0f };

        uint32_t meshCount = 0;

        for (auto& subMesh : mesh.subMeshs)
        {
            // サブメッシュ数加算
            meshCount++;

            // サブメッシュ情報
            SubMeshInfo data = {};
            auto it = materialIndexMap.find(subMesh.material);
            if (it == materialIndexMap.end()) throw std::runtime_error("Material not found: " + subMesh.material);
            data.materialIndex = it->second;                                    // マテリアルインデックス
            data.startIndex = static_cast<uint32_t>(indexBuffer.size());        // スタートインデックス
            data.indexCount = static_cast<uint32_t>(subMesh.faces.size() * 3);  // インデックス数
            subMeshInfo.push_back(data);

            for (auto& face : subMesh.faces)
            {
                for (int i = 0; i < 3; i++)
                {
                    auto it = indexMap.find(face.faceIndices[i]);

                    if (it == indexMap.end())
                    {
                        // 新規頂点
                        uint32_t newIndex = static_cast<uint32_t>(vertexBuffer.size());
                        indexMap[face.faceIndices[i]] = newIndex;

                        vertexBuffer.push_back(MakeVertex(object, face.faceIndices[i]));
                        indexBuffer.push_back(newIndex);
                    }
                    else
                    {
                        // 既存頂点
                        indexBuffer.push_back(it->second);
                    }
                }
            }
        }

        // メッシュグループを追加
        MeshGroupInfo group = {};
        group.subMeshStart = meshStart;
        group.subMeshCount = meshCount;
        meshStart += meshCount;
        meshGroupInfo.push_back(group);

        // ノードを追加
        nodeInfo.push_back(node);
    }
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

// ファイルへの出力関数
static int OutputImdl( const std::filesystem::path& path,
                       std::vector<MaterialInfo>& materials,
                       std::vector<SubMeshInfo>& subMeshes,
                       std::vector<MeshGroupInfo>& meshGroups,
                       std::vector<NodeInfo>& nodes,
                       std::vector<TextureEntry>& textures,
                       std::vector<VertexPositionNormalTextureTangent>& vertexBuffer,
                       std::vector<uint32_t>& indexBuffer )
{
    // 出力ファイルオープン
    std::ofstream ofs(path.c_str(), std::ios::binary);

    if (!ofs.is_open())
    {
        std::wcout << "Could not open " << path.c_str() << std::endl;
        return 1;
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

    // チャンク数を追加したヘッダを書き戻す
    fileHeader.chunkCount = chunkCount;
    ofs.seekp(0);
    ofs.write((char*)&fileHeader, sizeof(fileHeader));

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

// パス付きマテリアルファイル名を取得
static bool GetMaterialPath(std::filesystem::path input, std::filesystem::path& mtlPath)
{
    if (mtlPath.empty())
    {
        std::wcerr << L"No mtllib specified in obj file." << std::endl;
        return false;
    }

    // 相対パスなら
    if (mtlPath.is_relative())
    {
        // objの入っているフォルダのパスを付ける
        mtlPath = input.parent_path() / mtlPath;
    }

    mtlPath = mtlPath.lexically_normal();  // 文字列レベルで正規化

    // mtlファイルが存在？
    if (!std::filesystem::exists(mtlPath))
    {
        std::wcerr << L"Material not found: " << mtlPath << std::endl;
        return false;
    }

    return true;
}

// メイン
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

    Object object;
    std::string path;

    // objファイルの情報取得
    if (AnalyzeObj(input, object)) return 1;

    // パス付きマテリアルファイル名を取得
    if (!GetMaterialPath(input, object.mtllib))
    {
        return -1;
    }

    // マテリアルを取得
    std::vector<MaterialInfo> materials;
    std::unordered_map<std::string, uint32_t> materialIndexMap;
    std::vector<TextureEntry> textures;
    if (AnalyzeMtl(device.Get(), object.mtllib, materials, materialIndexMap, textures)) return 1;

    std::vector<SubMeshInfo> subMeshInfo;
    std::vector<MeshGroupInfo> meshGroupInfo;
    std::vector<NodeInfo> nodeInfo;
    std::vector<VertexPositionNormalTextureTangent> vertexBuffer;
    std::vector<uint32_t> indexBuffer;
    // Imdl形式に必要なデータを抽出する 
    ConvertImdl(object, materialIndexMap, subMeshInfo, meshGroupInfo, nodeInfo, vertexBuffer, indexBuffer);

    // 頂点データに接線を追加
    GenerateTangentsMikkTSpace(vertexBuffer, indexBuffer, 0, static_cast<uint32_t>(indexBuffer.size()));

    // ----- 書き出し ----- //

    if (OutputImdl(output, materials, subMeshInfo, meshGroupInfo, nodeInfo, textures, vertexBuffer, indexBuffer)) return 1;

    CoUninitialize();

    return 0;
}
