//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  The Cmajor project is subject to commercial or open-source licensing.
//  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
//  visit https://cmajor.dev to learn about our commercial licence options.
//
//  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
//  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
//  DISCLAIMED.

#pragma once

namespace cmaj
{

//==============================================================================
struct NativeTypeLayout
{
    NativeTypeLayout (const AST::TypeBase& t) : type (t.skipConstAndRefModifiers()) {}

    struct NativeChunkInfo
    {
        uint32_t offset, size;
        bool isPackedBits;
    };

    template <typename GetNativeLayout>
    void generate (const GetNativeLayout& getNativeLayout)
    {
        addChunks (type, getNativeLayout, 0, 0);
    }

    void generateWithoutPacking()
    {
        addChunk (0, 0, static_cast<uint32_t> (type.toChocType().getValueDataSize()), 0);
    }

    bool requiresPacking() const
    {
        if (chunks.empty())
            return false;

        return chunks.size() > 1 || chunks.front().numBits != 0;
    }

    uint32_t getNativeSize() const
    {
        return chunks.empty() ? 0 : chunks.back().getNativeEnd();
    }

    void copyNativeToPacked (void* packedDest, const void* nativeSource) const
    {
        for (auto& chunk : chunks)
            chunk.copyNativeToPacked (static_cast<uint8_t*> (packedDest),
                                      static_cast<const uint8_t*> (nativeSource));
    }

    void copyPackedToNative (void* nativeDest, const void* packedSource) const
    {
        for (auto& chunk : chunks)
            chunk.copyPackedToNative (static_cast<uint8_t*> (nativeDest),
                                      static_cast<const uint8_t*> (packedSource));
    }

    uint32_t convertPackedByteToNativeBit (uint32_t offset) const
    {
        for (auto& c : chunks)
        {
            if (offset >= c.packedOffset && offset < c.getPackedEnd())
            {
                auto startBit = 8 * c.nativeOffset;
                auto packedOffset = offset - c.packedOffset;

                return c.numBits != 0 ? startBit + packedOffset / sizeof (choc::value::BoolStorageType)
                                      : startBit + packedOffset * 8;
            }
        }

        CMAJ_ASSERT (offset == chunks.back().getPackedEnd());
        return 8 * getNativeSize();
    }

    uint32_t convertPackedByteToNativeByte (uint32_t offset) const
    {
        for (auto& c : chunks)
        {
            if (offset >= c.packedOffset && offset < c.getPackedEnd())
            {
                CMAJ_ASSERT (c.numBits == 0);
                return offset + c.nativeOffset - c.packedOffset;
            }
        }

        CMAJ_ASSERT (offset == chunks.back().getPackedEnd());
        return getNativeSize();
    }

    const AST::TypeBase& type;

private:
    struct ContiguousChunk
    {
        uint32_t packedOffset, nativeOffset, numBytes, numBits;

        uint32_t getPackedEnd() const   { return packedOffset + (numBits != 0 ? (numBits * 4) : numBytes); }
        uint32_t getNativeEnd() const   { return nativeOffset + (numBits != 0 ? (numBits + 7) / 8 : numBytes); }

        void copyNativeToPacked (uint8_t* dest, const uint8_t* source) const
        {
            dest += packedOffset;
            source += nativeOffset;

            if (numBits == 0)
                std::memcpy (dest, source, numBytes);
            else
                copyBitVectorToInts (reinterpret_cast<uint32_t*> (dest), source, numBits);
        }

        void copyPackedToNative (uint8_t* dest, const uint8_t* source) const
        {
            dest += nativeOffset;
            source += packedOffset;

            if (numBits == 0)
                std::memcpy (dest, source, numBytes);
            else
                copyIntsToBitVector (dest, reinterpret_cast<const uint32_t*> (source), numBits);
        }

        static void copyBitVectorToInts (uint32_t* dest, const uint8_t* source, uint32_t numBits)
        {
            uint32_t bitIndex = 0;
            auto sourceByte = *source;

            for (uint32_t i = 0; i < numBits; ++i)
            {
                *dest++ = (sourceByte & 1u);
                sourceByte = static_cast<uint8_t> (sourceByte >> 1u);

                if (++bitIndex == 8)
                {
                    bitIndex = 0;
                    sourceByte = *++source;
                }
            }
        }

        static void copyIntsToBitVector (uint8_t* dest, const uint32_t* source, uint32_t numBits)
        {
            uint8_t bitMask = 1;

            for (uint32_t i = 0; i < numBits; ++i)
            {
                if (bitMask == 1)
                    *dest = bitMask;
                else if (*source != 0)
                    *dest |= bitMask;

                bitMask <<= 1;
                ++source;
            }
        }
    };

    choc::SmallVector<ContiguousChunk, 2> chunks;

    template <typename GetNativeLayout>
    void addChunks (const AST::TypeBase& astType, const GetNativeLayout& getNativeLayout, uint32_t packedOffset, uint32_t nativeOffset)
    {
        if (auto a = astType.getAsArrayType())
        {
            auto& elementType = *a->getArrayOrVectorElementType();
            auto packedElementSize = static_cast<uint32_t> (elementType.toChocType().getValueDataSize());
            auto nativeElementInfo = getNativeLayout (*a, 0);
            auto numElements = a->resolveSize();

            for (uint32_t i = 0; i < numElements; ++i)
            {
                addChunks (elementType, getNativeLayout, packedOffset, nativeOffset);
                packedOffset += packedElementSize;
                nativeOffset += nativeElementInfo.size;
            }

            return;
        }

        if (auto s = astType.getAsStructType())
        {
            auto numMembers = s->memberTypes.size();

            for (uint32_t i = 0; i < numMembers; ++i)
            {
                auto& memberType = *s->getAggregateElementType (i);
                auto memberInfo = getNativeLayout (*s, i);
                addChunks (memberType, getNativeLayout, packedOffset, nativeOffset + memberInfo.offset);
                packedOffset += static_cast<uint32_t> (memberType.toChocType().getValueDataSize());
            }

            return;
        }

        if (auto v = astType.getAsVectorType())
        {
            auto& elementType = *v->getArrayOrVectorElementType();
            auto packedElementSize = static_cast<uint32_t> (elementType.toChocType().getValueDataSize());
            auto nativeElementInfo = getNativeLayout (*v, 0);
            auto numElements = v->resolveSize();

            if (nativeElementInfo.isPackedBits)
            {
                addChunk (packedOffset, nativeOffset, 0, numElements);
                return;
            }

            for (uint32_t i = 0; i < numElements; ++i)
            {
                addChunk (packedOffset, nativeOffset, std::min (packedElementSize, nativeElementInfo.size), 0);
                packedOffset += packedElementSize;
                nativeOffset += nativeElementInfo.size;
            }

            return;
        }

        auto packedSize = static_cast<uint32_t> (astType.toChocType().getValueDataSize());
        auto nativeSize = getNativeLayout (astType, 0);
        addChunk (packedOffset, nativeOffset, std::min (nativeSize.size, packedSize), 0);
    }

    void addChunk (uint32_t packedOffset, uint32_t nativeOffset, uint32_t numBytes, uint32_t numBits)
    {
        if (! chunks.empty())
        {
            auto& last = chunks.back();

            if (last.getNativeEnd() == nativeOffset
                 && last.getPackedEnd() == packedOffset
                 && (last.numBits != 0) == (numBits != 0))
            {
                last.numBytes += numBytes;
                return;
            }
        }

        chunks.push_back ({ packedOffset, nativeOffset, numBytes, numBits });
    }
};

//==============================================================================
struct NativeTypeLayoutCache
{
    ptr<const NativeTypeLayout> find (const AST::TypeBase& targetType)
    {
        for (auto& p : nativeTypeLayouts)
            if (p->type.isIdentical (targetType))
                return ptr<const NativeTypeLayout> (p.get());

        return {};
    }

    ptr<const NativeTypeLayout> get (const AST::TypeBase& type)
    {
        if (auto p = find (type))
            return p;

        auto layout = createLayout (type);
        auto result = layout.get();
        nativeTypeLayouts.push_back (std::move (layout));
        return ptr<const NativeTypeLayout> (result);
    }

    std::function<std::unique_ptr<NativeTypeLayout>(const AST::TypeBase&)> createLayout;
    std::vector<std::unique_ptr<NativeTypeLayout>> nativeTypeLayouts;
};

}
