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

struct StringPool;


//==============================================================================
struct PooledString
{
    PooledString() = default;

    operator std::string_view() const                   { return text != nullptr ? *text : std::string_view(); }
    std::string_view get() const                        { return text != nullptr ? *text : std::string_view(); }

    bool empty() const                                  { return text == nullptr; }

    bool operator== (PooledString other) const          { return text == other.text; }
    bool operator!= (PooledString other) const          { return text != other.text; }

    bool operator== (std::string_view other) const      { return text != nullptr ? (*text == other) :   other.empty(); }
    bool operator!= (std::string_view other) const      { return text != nullptr ? (*text != other) : ! other.empty(); }

    size_t hash() const                                 { return reinterpret_cast<size_t> (text); }

private:
    friend struct StringPool;
    PooledString (const std::string_view* s) : text (s) {}

    const std::string_view* text = nullptr;
};


//==============================================================================
struct StringPool
{
    StringPool (choc::memory::Pool& p) : pool (p)
    {
        strings.reserve (256);
    }

    PooledString get (const std::string& s)
    {
        if (s.empty())
            return {};

        if (auto i = strings.find (s); i != strings.end())
            return i->second;

        auto length = s.length();
        auto data = pool.allocateData (sizeof (std::string_view) + length);
        auto sv = reinterpret_cast<std::string_view*> (data);
        auto text = static_cast<char*> (data) + sizeof (std::string_view);
        new (sv) std::string_view (text, length);
        std::memcpy (text, s.data(), length);

        auto ps = PooledString (sv);
        strings[s] = ps;
        return ps;
    }

    PooledString get (std::string_view s)   { return get (std::string (s)); }
    PooledString get (const char* s)        { return get (std::string (s)); }

private:
    choc::memory::Pool& pool;
    std::unordered_map<std::string, PooledString> strings;
};


//==============================================================================
struct Strings
{
    Strings (choc::memory::Pool& p) : stringPool (p) {}

    StringPool stringPool;

    const PooledString real                          { stringPool.get ("real") },
                       imag                          { stringPool.get ("imag") },
                       mainFunctionName              { stringPool.get ("main") },
                       userInitFunctionName          { stringPool.get ("init") },
                       resetFunctionName             { stringPool.get ("reset") },
                       systemInitFunctionName        { stringPool.get ("_initialise") },
                       systemAdvanceFunctionName     { stringPool.get ("_advance") },
                       rootNamespaceName             { stringPool.get ("_root") },
                       initFnProcessorIDParamName    { stringPool.get ("processorID") },
                       initFnSessionIDParamName      { stringPool.get ("sessionID") },
                       initFnFrequencyParamName      { stringPool.get ("frequency") },
                       consoleEndpointName           { stringPool.get ("console") },
                       stateStructName               { stringPool.get ("_State") },
                       ioStructName                  { stringPool.get ("_IO") },
                       stdLibraryNamespaceName       { stringPool.get ("std") },
                       intrinsicsNamespaceName       { stringPool.get ("intrinsics") },
                       voidTypeName                  { stringPool.get ("void") },
                       int32TypeName                 { stringPool.get ("int32") },
                       int64TypeName                 { stringPool.get ("int64") },
                       float32TypeName               { stringPool.get ("float32") },
                       float64TypeName               { stringPool.get ("float64") },
                       complex32TypeName             { stringPool.get ("complex32") },
                       complex64TypeName             { stringPool.get ("complex64") },
                       boolTypeName                  { stringPool.get ("bool") },
                       stringTypeName                { stringPool.get ("string") },
                       processor                     { stringPool.get ("processor") },
                       wrap                          { stringPool.get ("wrap" )},
                       clamp                         { stringPool.get ("clamp" )},
                       _state                        { stringPool.get ("_state") },
                       _io                           { stringPool.get ("_io") },
                       in                            { stringPool.get ("in") },
                       out                           { stringPool.get ("out") },
                       value                         { stringPool.get ("value") },
                       values                        { stringPool.get ("values") },
                       index                         { stringPool.get ("index") },
                       start                         { stringPool.get ("start") },
                       end                           { stringPool.get ("end") },
                       array                         { stringPool.get ("array") },
                       run                           { stringPool.get ("run") },
                       increment                     { stringPool.get ("increment") },
                       frames                        { stringPool.get ("frames") },
                       _frames                       { stringPool.get ("_frames") },
                       _activeRamps                  { stringPool.get ("_activeRamps") },
                       _updateRamps                  { stringPool.get ("_updateRamps") };
};
