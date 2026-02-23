//--------------------------------------------------------------------------------------
// File: ChunkIO.h
//
// チャンクデータの書き込みと読み込み
//
// Date: 2026.2.16
// Author: Hideyasu Imase
//--------------------------------------------------------------------------------------
#pragma once

#include <iostream>
#include <fstream>
#include <vector>

namespace Imase
{
    // チャンクヘッダの構造体
    struct ChunkHeader
    {
        uint32_t type;  // データタイプ
        uint32_t size;  // データサイズ
    };

    // チャンクデータ書き出し関数
    inline void WriteChunk(std::ofstream& ofs, uint32_t& chunkCounter, uint32_t type, const std::vector<uint8_t>& data)
    {
        ChunkHeader header{};
        header.type = type;
        header.size = (uint32_t)data.size();

        ofs.write((char*)&header, sizeof(header));
        ofs.write((char*)data.data(), data.size());

        chunkCounter++;
    }

    // チャンクデータを読み込む関数
    inline bool ReadChunk(std::ifstream& ifs, ChunkHeader& header, std::vector<uint8_t>& buffer)
    {
        if (!ifs.read(reinterpret_cast<char*>(&header), sizeof(header)))
        {
            return false;
        }

        buffer.resize(header.size);

        if (!ifs.read(reinterpret_cast<char*>(buffer.data()), header.size))
        {
            return false;
        }

        return true;
    }
}
