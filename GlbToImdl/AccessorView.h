#pragma once

#include "tiny_gltf.h"

// 汎用AccessorViewクラス
template<typename T>
class AccessorView
{
public:

    AccessorView(const tinygltf::Model& model, int accessorIndex)
    {
        const auto& accessor = model.accessors[accessorIndex];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];

        m_count = accessor.count;
        m_stride = accessor.ByteStride(bufferView);
        if (m_stride == 0)
            m_stride = sizeof(T);

        m_data = buffer.data.data()
            + bufferView.byteOffset
            + accessor.byteOffset;
    }

    const T& operator[](size_t index) const
    {
        return *reinterpret_cast<const T*>(m_data + m_stride * index);
    }

    size_t size() const { return m_count; }

private:

    const uint8_t* m_data = nullptr;
    size_t m_stride = 0;
    size_t m_count = 0;
};

// インデックス用AccessorViewクラス（インデックスの型により可変する）
class IndexAccessorView
{
public:

    IndexAccessorView(const tinygltf::Model& model, int accessorIndex)
    {
        const auto& accessor = model.accessors[accessorIndex];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];

        m_count = accessor.count;
        m_componentType = accessor.componentType;

        m_stride = accessor.ByteStride(bufferView);
        if (m_stride == 0)
        {
            switch (m_componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  m_stride = 1; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: m_stride = 2; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   m_stride = 4; break;
            default: assert(false);
            }
        }

        m_data = buffer.data.data()
            + bufferView.byteOffset
            + accessor.byteOffset;
    }

    uint32_t operator[](size_t index) const
    {
        const uint8_t* p = m_data + m_stride * index;

        switch (m_componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return *reinterpret_cast<const uint8_t*>(p);

        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return *reinterpret_cast<const uint16_t*>(p);

        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return *reinterpret_cast<const uint32_t*>(p);

        default:
            assert(false);
            return 0;
        }
    }

    size_t size() const { return m_count; }

private:

    const uint8_t* m_data = nullptr;
    size_t m_stride = 0;
    size_t m_count = 0;
    int m_componentType = 0;
};
