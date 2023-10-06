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

#include "choc/javascript/choc_javascript.h"


namespace cmaj::javascript
{

#define CMAJ_JAVASCRIPT_BINDING_METHOD(name) \
    context.registerFunction ("_" #name, [&] (choc::javascript::ArgumentList args) { return name (args); });

//==============================================================================
template <typename ObjectType, typename StorageType>
struct ObjectHandleList
{
    std::vector<StorageType> objects;

    choc::value::Value createNewObject (StorageType newObject)
    {
        for (size_t i = 0; i < objects.size(); ++i)
        {
            if (objects[i] == nullptr)
            {
                objects[i] = std::move (newObject);
                return choc::value::createInt32 (static_cast<int32_t> (i));
            }
        }

        auto handle = objects.size();
        objects.push_back (std::move (newObject));
        return choc::value::createInt32 (static_cast<int32_t> (handle));
    }

    void deleteObject (choc::javascript::ArgumentList args, size_t argIndex)
    {
        auto index = getIndex (args, argIndex);

        if (index < objects.size())
            objects[index].reset();
    }

    ObjectType* getObject (choc::javascript::ArgumentList args, size_t argIndex) const
    {
        auto index = getIndex (args, argIndex);

        if (index < objects.size())
            return objects[index].get();

        return {};
    }

    ObjectType& getObjectExpectingSuccess (choc::javascript::ArgumentList args, size_t argIndex) const
    {
        auto o = getObject (args, argIndex);
        CMAJ_ASSERT (o != nullptr);
        return *o;
    }

    void clear()
    {
        objects.clear();
    }

private:
    size_t getIndex (choc::javascript::ArgumentList args, size_t argIndex) const
    {
        auto handle = args.get<int64_t> (argIndex, -1);

        if (handle >= 0 && handle < static_cast<int64_t> (objects.size()))
            return static_cast<size_t> (handle);

        return objects.size();
    }
};

}
