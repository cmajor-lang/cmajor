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


namespace cmaj::transformations
{

/*
    Binary module format:
        - 8 bytes header "Cmaj0001"
        - 8 bytes xxHash64 of the rest of the file
        - compressed int: number of main top-level objects
        - Series of objects (first ones being the top-level objects), where an object is:
            - 1 byte: object class
            - compressed int: parent ID  (must be 0 for a top-level object)
            - 1 byte: number of properties
            - ..list of stored properties

        Object IDs start from 1 and are sequential in the file
*/

static constexpr uint32_t hashOffset = 8;
static constexpr uint32_t objectDataStart = 16;
static constexpr size_t numObjectsToReserve = 16384;

//==============================================================================
struct BinaryModuleWriter
{
    BinaryModuleWriter()
    {
        objectIDs.reserve (numObjectsToReserve);
        objectsToStore.reserve (numObjectsToReserve);
        data.reserve (8192);
    }

    void store (const AST::ObjectRefVector<AST::ModuleBase>& mainObjects)
    {
        write (AST::getBinaryProgramHeader().data(), AST::getBinaryProgramHeader().length());
        writeZeros (8);  // space for the hash
        writeCompressedInt (static_cast<int64_t> (mainObjects.size()));

        for (auto& o : mainObjects)
            addObjectToStore (o.getPointer());

        // NB: this is deliberately not a range-based-for because
        // the vector will grow during the loop
        for (size_t i = 0; i < objectsToStore.size(); ++i)
            writeObject (*objectsToStore[i], i < mainObjects.size());

        writeHash();
    }

    uint32_t addObjectToStore (AST::Object* o)
    {
        if (o == nullptr)
            return 0;

        if (auto found = objectIDs.find (o); found != objectIDs.end())
            return found->second;

        objectsToStore.push_back (o);
        auto ID = static_cast<uint32_t> (objectsToStore.size());
        objectIDs[o] = ID;
        return ID;
    }

    template <typename Type>
    void writeByte (Type n)       { data.push_back (static_cast<uint8_t> (n)); }

    void write (const void* buffer, size_t size)
    {
        for (size_t i = 0; i < size; ++i)
            writeByte (static_cast<const uint8_t*> (buffer)[i]);
    }

    void writeZeros (uint32_t size)
    {
        for (uint32_t i = 0; i < size; ++i)
            writeByte (0);
    }

    void writeCompressedInt (int64_t n)
    {
        char i[16];
        auto len = choc::integer_encoding::encodeVariableLengthInt (i, n);
        write (i, len);
    }

    void writeObject (AST::Object& o, bool isMainObject)
    {
        auto ID = objectIDs.find (std::addressof (o));
        CMAJ_ASSERT (ID != objectIDs.end());
        writeByte (o.getObjectClassID());
        writeCompressedInt (isMainObject ? 0 : addObjectToStore (o.context.parentScope.get()));

        auto props = o.getPropertyList();
        uint32_t numActiveProps = 0;

        for (auto& p : props)
            if (! p->hasDefaultValue())
                ++numActiveProps;

        writeByte (numActiveProps);

        for (uint32_t i = 0; i < props.size(); ++i)
        {
            if (! props[i].hasDefaultValue())
            {
                auto propID = o.getPropertyID(i);
                CMAJ_ASSERT (propID != 0);
                writeByte (propID);
                writeProperty (props[i]);
            }
        }
    }

    void writeProperty (const AST::Property& prop)
    {
        if (auto p = prop.getAsIntegerProperty())
        {
            writeCompressedInt (choc::integer_encoding::zigzagEncode (static_cast<int64_t> (*p)));
            return;
        }

        if (auto p = prop.getAsFloatProperty())
        {
            char i[8];
            choc::memory::writeLittleEndian (i, choc::memory::bit_cast<uint64_t> (static_cast<double> (*p)));
            write (i, 8);
            return;
        }

        if (auto p = prop.getAsBoolProperty())
        {
            writeByte (p->get() ? 1 : 0);
            return;
        }

        if (auto p = prop.getAsStringProperty())
        {
            auto s = p->get().get();
            write (s.data(), s.length());
            writeByte (0);
            return;
        }

        if (auto p = prop.getAsEnumProperty())
        {
            writeByte (p->getID());
            return;
        }

        if (auto p = prop.getAsObjectProperty())
        {
            writeCompressedInt (addObjectToStore (p->getRawPointer()));
            return;
        }

        if (auto p = prop.getAsListProperty())
        {
            writeCompressedInt (static_cast<int64_t> (p->size()));

            for (auto& item : *p)
            {
                writeByte (item->getPropertyTypeID());
                writeProperty (item);
            }

            return;
        }

        CMAJ_ASSERT_FALSE;
    }

    void writeHash()
    {
        choc::hash::xxHash64 hash;
        hash.addInput (data.data() + objectDataStart, data.size() - objectDataStart);
        choc::memory::writeLittleEndian (data.data() + hashOffset, hash.getHash());
    }

    std::vector<uint8_t> data;
    std::unordered_map<AST::Object*, uint32_t> objectIDs;
    std::vector<AST::Object*> objectsToStore;
};

std::vector<uint8_t> createBinaryModule (const AST::ObjectRefVector<AST::ModuleBase>& objects)
{
    BinaryModuleWriter data;
    data.store (objects);
    return std::move (data.data);
}

//==============================================================================
struct BinaryModuleReader
{
    BinaryModuleReader (const uint8_t* d, size_t s) : data (d), size (s)
    {
        objectProperiesToResolve.reserve (numObjectsToReserve);
    }

    void read (AST::Allocator& allocator,
               AST::ObjectRefVector<AST::ModuleBase>& results,
               bool checkHashValidity)
    {
        objectsRead.reserve (numObjectsToReserve);

        std::vector<ParentToResolve> parentsToResolve;
        parentsToResolve.reserve (numObjectsToReserve);

        if (! readHeaderAndHash (checkHashValidity))
            throwError();

        auto numMainObjects = readCompressedUInt32();

        while (size != 0)
        {
            auto classID  = readByte();
            auto parentID = readCompressedUInt32();

            AST::ObjectContext context { allocator, {}, nullptr };

            if (auto p = getObjectFromID (parentID))
                context.parentScope = *p;

            auto newObject = AST::createObjectOfClassType (context, classID);

            if (numMainObjects != 0)
            {
                auto m = AST::castTo<AST::ModuleBase> (newObject);

                if (m == nullptr || parentID != 0)
                    throwError();

                results.push_back (*m);
                --numMainObjects;
            }

            if (newObject == nullptr)
            {
                if (auto upgraded = importOldObjectType (context, classID))
                {
                    objectsRead.push_back (upgraded.get());

                    if (parentID != 0 && context.parentScope == nullptr)
                        parentsToResolve.push_back ({ *upgraded, parentID });

                    continue;
                }

                throwError();
            }

            objectsRead.push_back (newObject.get());

            if (parentID != 0 && context.parentScope == nullptr)
                parentsToResolve.push_back ({ *newObject, parentID });

            auto numProperties = readByte();

            for (size_t i = 0; i < numProperties; ++i)
                readProperty (*newObject);
        }

        for (auto& o : objectProperiesToResolve)
        {
            auto found = getObjectFromID (o.objectID);
            CMAJ_ASSERT (found != nullptr);
            o.property.referToUnchecked (*found);
        }

        for (auto& p : parentsToResolve)
        {
            auto found = getObjectFromID (p.parentID);
            CMAJ_ASSERT (found != nullptr);
            p.object.setParentScope (*found);
        }
    }

    bool readHeaderAndHash (bool checkHashValidity)
    {
        if (size <= objectDataStart || std::string_view (reinterpret_cast<const char*> (data), hashOffset) != AST::getBinaryProgramHeader())
            return false;

        skip (8);
        auto hashFromFile = readInt64();

        if (checkHashValidity)
        {
            choc::hash::xxHash64 hash;
            hash.addInput (data, size);
            auto calculatedHash = hash.getHash();

            if (hashFromFile != calculatedHash)
                return false;
        }

        return true;
    }

    void skip (size_t num)
    {
        data += num;
        size -= num;
    }

    uint8_t readByte()
    {
        if (size == 0)
            throwError();

        auto n = *data;
        skip (1);
        return n;
    }

    uint64_t readInt64()
    {
        if (size < 8)
            throwError();

        auto n = choc::memory::readLittleEndian<uint64_t> (data);
        skip (8);
        return n;
    }

    int64_t readCompressedInt()
    {
        size_t used = 0;
        auto n = choc::integer_encoding::decodeVariableLengthInt<int64_t> (data, size, used);

        if (used == 0)
            throwError();

        skip (used);
        return n;
    }

    uint32_t readCompressedUInt32()
    {
        auto n = readCompressedInt();

        if (n < 0 || n > 0xffffffff)
            throwError();

        return static_cast<uint32_t> (n);
    }

    std::string_view readZeroTerminatedString()
    {
        for (auto start = data;;)
        {
            if (size == 0)
                throwError();

            auto c = *data++;
            --size;

            if (c == 0)
                return std::string_view (reinterpret_cast<const char*> (start),
                                         static_cast<size_t> (data - start - 1));
        }
    }

    void readProperty (AST::Object& targetObject)
    {
        if (auto p = targetObject.findPropertyForID (readByte()))
            return readProperty (*p);

        throwError();
    }

    void readProperty (AST::Property& prop)
    {
        if (auto p = prop.getAsIntegerProperty())
        {
            p->set (choc::integer_encoding::zigzagDecode (readCompressedInt()));
            return;
        }

        if (auto p = prop.getAsFloatProperty())
        {
            p->set (choc::memory::bit_cast<double> (readInt64()));
            return;
        }

        if (auto p = prop.getAsBoolProperty())
        {
            p->set (readByte() != 0);
            return;
        }

        if (auto p = prop.getAsStringProperty())
        {
            p->set (prop.getStringPool().get (readZeroTerminatedString()));
            return;
        }

        if (auto p = prop.getAsEnumProperty())
        {
            p->setID (readByte());
            return;
        }

        if (auto p = prop.getAsObjectProperty())
        {
            if (auto objectID = readCompressedUInt32())
            {
                if (auto found = getObjectFromID (objectID))
                    p->referToUnchecked (*found);
                else
                    objectProperiesToResolve.push_back ({ *p, objectID });
            }

            return;
        }

        if (auto p = prop.getAsListProperty())
        {
            auto numItems = readCompressedUInt32();
            p->reserve (numItems);

            for (uint32_t i = 0; i < numItems; ++i)
            {
                auto propertyTypeID = readByte();
                auto& property = createPropertyOfType (prop.owner, propertyTypeID);
                p->add (property);
                readProperty (property);
            }

            return;
        }

        CMAJ_ASSERT_FALSE;
    }

    void readPropertyIntoList (AST::ListProperty& list)
    {
        list.addNullObject();
        readProperty (list.back());
    }

    ptr<AST::Object> importOldObjectType (AST::ObjectContext& context, uint8_t classID)
    {
        if (classID == 19) // old 1-dimensional ArrayType
        {
            auto numProperties = readByte();
            auto& o = AST::castToRef<AST::ArrayType> (AST::createObjectOfClassType (context, AST::ArrayType::classID));

            for (uint32_t i = 0; i < numProperties; ++i)
            {
                switch (readByte())
                {
                    case 1:   readProperty (o.elementType); break;
                    case 2:   readPropertyIntoList (o.dimensionList); break;
                    default:  throwError(); break;
                }
            }

            return o;
        }

        if (classID == 3) // old BracketedSuffix
        {
            auto numProperties = readByte();
            auto& o = AST::castToRef<AST::BracketedSuffix> (AST::createObjectOfClassType (context, AST::BracketedSuffix::classID));

            auto getTerm = [&] () -> AST::BracketedSuffixTerm&
            {
                if (o.terms.empty())
                {
                    auto& term = o.allocateChild<AST::BracketedSuffixTerm>();
                    o.terms.addChildObject (term);
                }

                return AST::castToRef<AST::BracketedSuffixTerm> (o.terms[0]);
            };

            for (uint32_t i = 0; i < numProperties; ++i)
            {
                switch (readByte())
                {
                    case 1:   readProperty (o.parent); break;
                    case 2:   readProperty (getTerm().startIndex); break;
                    case 3:   readProperty (getTerm().endIndex); break;
                    case 4:   readProperty (getTerm().isRange); break;
                    default:  throwError(); break;
                }
            }

            return o;
        }

        if (classID == 4) // old ChevronedSuffix
        {
            auto numProperties = readByte();
            auto& o = AST::castToRef<AST::ChevronedSuffix> (AST::createObjectOfClassType (context, AST::ChevronedSuffix::classID));

            for (uint32_t i = 0; i < numProperties; ++i)
            {
                switch (readByte())
                {
                    case 1:   readProperty (o.parent); break;
                    case 2:   readPropertyIntoList (o.terms); break;
                    default:  throwError(); break;
                }
            }

            return o;
        }

        if (classID == 33)  // old GetElement
        {
            auto numProperties = readByte();
            auto& o = AST::castToRef<AST::GetElement> (AST::createObjectOfClassType (context, AST::GetElement::classID));

            for (uint32_t i = 0; i < numProperties; ++i)
            {
                switch (readByte())
                {
                    case 1:  readProperty (o.parent); break;
                    case 2:  readPropertyIntoList (o.indexes); break;
                    case 3:  readProperty (o.isAtFunction); break;
                    default: throwError(); break;
                }
            }

            return o;
        }

        if (classID == 39) // old ValueMetaFunction
        {
            auto numProperties = readByte();
            auto& o = AST::castToRef<AST::ValueMetaFunction> (AST::createObjectOfClassType (context, AST::ValueMetaFunction::classID));

            for (uint32_t i = 0; i < numProperties; ++i)
            {
                switch (readByte())
                {
                    case 1:   readPropertyIntoList (o.arguments); break;
                    case 2:   readProperty (o.op); break;
                    default:  throwError(); break;
                }
            }

            return o;
        }

        return {};
    }

    static AST::Property& createPropertyOfType (AST::Object& parentObject, uint8_t propertyTypeID)
    {
        switch (propertyTypeID)
        {
            case AST::IntegerProperty::typeID:    return parentObject.context.allocator.allocate<AST::IntegerProperty> (parentObject);
            case AST::FloatProperty::typeID:      return parentObject.context.allocator.allocate<AST::FloatProperty> (parentObject);
            case AST::BoolProperty::typeID:       return parentObject.context.allocator.allocate<AST::BoolProperty> (parentObject);
            case AST::StringProperty::typeID:     return parentObject.context.allocator.allocate<AST::StringProperty> (parentObject);
            case AST::ChildObject::typeID:        return parentObject.context.allocator.allocate<AST::ChildObject> (parentObject);
            case AST::ObjectReference::typeID:    return parentObject.context.allocator.allocate<AST::ObjectReference> (parentObject);
            case AST::ListProperty::typeID:       return parentObject.context.allocator.allocate<AST::ListProperty> (parentObject);
            case AST::EnumProperty::typeID:       // there are multiple enum classes, so won't create the base class
            default:                              CMAJ_ASSERT_FALSE;
        }
    }

    AST::Object* getObjectFromID (uint32_t objectID) const
    {
        if (--objectID < static_cast<uint32_t> (objectsRead.size()))
            return objectsRead[objectID];

        return {};
    }

    [[noreturn]] static void throwError()   { throw std::runtime_error ("Error reading binary program data"); }

    const uint8_t* data;
    size_t size;

    struct ParentToResolve
    {
        AST::Object& object;
        uint32_t parentID;
    };

    struct ObjectPropertyToResolve
    {
        AST::ObjectProperty& property;
        uint32_t objectID;
    };

    std::vector<AST::Object*> objectsRead;
    std::vector<ObjectPropertyToResolve> objectProperiesToResolve;
};

//==============================================================================
AST::ObjectRefVector<AST::ModuleBase> parseBinaryModule (AST::Allocator& allocator, const void* data, size_t size,
                                                         bool checkHashValidity)
{
    try
    {
        AST::ObjectRefVector<AST::ModuleBase> results;
        BinaryModuleReader reader (static_cast<const uint8_t*> (data), size);
        reader.read (allocator, results, checkHashValidity);
        return results;
    }
    catch (...)
    {}

    return {};
}

bool isValidBinaryModuleData (const void* data, size_t size)
{
    BinaryModuleReader reader (static_cast<const uint8_t*> (data), size);
    return reader.readHeaderAndHash (true);
}


} // namespace cmaj::transformations
