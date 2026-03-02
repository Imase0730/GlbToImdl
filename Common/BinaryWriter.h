//--------------------------------------------------------------------------------------
// File: BinaryWriter.h
//
// 指定サイズのデータをバッファに書き込んでいって、バッファを作成する関数
//
// ※内部で指定サイズのデータを書き込むと書き込み位置を移動しています
//
// Date: 2026.2.16
// Author: Hideyasu Imase
//--------------------------------------------------------------------------------------
#pragma once
#include <vector>
#include <string>

namespace Imase
{
    class BinaryWriter
    {
    public:

        // 【int16_t】を書き込む関数
        void WriteInt16(int16_t v)
        {
            WriteRaw(&v, sizeof(v));
        }

        // 【uint32_t】を書き込む関数
        void WriteUInt32(uint32_t v)
        {
            WriteRaw(&v, sizeof(v));
        }

        // 【int32_t】を書き込む関数
        void WriteInt32(int32_t v)
        {
            WriteRaw(&v, sizeof(v));
        }

        // 【float】を書き込む関数
        void WriteFloat(float v)
        {
            WriteRaw(&v, sizeof(v));
        }

        // 【1byte】を書き込む関数
        void WriteBytes(const void* data, size_t size)
        {
            WriteRaw(data, size);
        }

        // 【T】を指定個数を書き込む関数
        // 【uint32_t】(count) + 【T】 * count
        template<typename T>
        void WriteCountedArray(const std::vector<T>& vec)
        {
            WriteUInt32(static_cast<uint32_t>(vec.size()));
            if (!vec.empty())
            {
                WriteRaw(vec.data(), sizeof(T) * vec.size());
            }
        }

        // std::stringを書き込む関数
        //【uint32_t】(count) + 【1byte】 * count
        void WriteString(const std::string& str)
        {
            uint32_t length = static_cast<uint32_t>(str.size());
            WriteUInt32(length);
            WriteBytes(str.data(), length);
        }

        // バッファを取得する関数
        const std::vector<uint8_t>& GetBuffer() const
        {
            return m_buffer;
        }

    private:

        // 書き込み用バッファ
        std::vector<uint8_t> m_buffer;

        // バッファに指定サイズのデータを書き込む関数
        void WriteRaw(const void* data, size_t size)
        {
            size_t oldSize = m_buffer.size();
            m_buffer.resize(oldSize + size);
            std::memcpy(m_buffer.data() + oldSize, data, size);
        }
    };
}
