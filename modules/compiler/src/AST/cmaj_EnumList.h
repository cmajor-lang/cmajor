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

namespace cmaj
{

//==============================================================================
struct EnumList
{
    /// Must be a valid, non-empty, comma-separated string literal with values
    EnumList (const char* nameList)
    {
        auto tokenStart = nameList;
        auto pos = tokenStart;

        for (;;)
        {
            while (*pos != '=')
                ++pos;

            auto name = choc::text::trim (std::string_view (tokenStart, static_cast<size_t> (pos - tokenStart)));
            auto valueStart = ++pos;

            while (*pos != ',' && *pos != 0)
                ++pos;

            auto valueString = choc::text::trim (std::string_view (valueStart, static_cast<size_t> (pos - valueStart)));
            auto value = std::atoi (std::string (valueString).c_str());
            items.push_back ({ name, value });

            if (*pos == 0)
                break;

            tokenStart = ++pos;
        }
    }

    int getID (std::string_view name) const
    {
        for (auto& i : items)
            if (i.name == name)
                return i.ID;

        return -1;
    }

    bool isValidID (int idToCheck) const
    {
        for (auto& i : items)
            if (i.ID == idToCheck)
                return true;

        return false;
    }

    std::string_view getNameForID (int itemID) const
    {
        for (auto& i : items)
            if (i.ID == itemID)
                return i.name;

        CMAJ_ASSERT_FALSE;
        return {};
    }

    struct Item
    {
        std::string_view name;
        int ID;

        bool operator== (const Item& other) const       { return ID == other.ID; }
    };

    std::vector<Item> items;
};

}
