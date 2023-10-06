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

#include "juce/cmaj_JUCEHeaders.h"

#include "../../../modules/compiler/include/cmaj_ErrorHandling.h"
#include "choc/text/choc_Files.h"
#include "choc/containers/choc_COM.h"
#include "../../../modules/compiler/src/codegen/cmaj_HTMLDocGenerator.h"
#include "../../../modules/compiler/src/codegen/cmaj_GraphGenerator.h"

static void writeToOutput (const std::string& outputFile, std::string_view s)
{
    if (outputFile.empty())
        std::cout << s << std::flush;
    else
        choc::file::replaceFileWithContent (outputFile, s);
}

static void addSourceToProgram (cmaj::AST::Program& program, const std::set<std::filesystem::path>& files)
{
    for (auto& sourceFile : files)
    {
        auto sourceContent = choc::file::loadFileAsString (sourceFile.string());

        auto errors = std::string (choc::com::StringPtr (program.parse (sourceFile.string().c_str(),
                                                                        sourceContent.data(),
                                                                        sourceContent.length())));

        if (! errors.empty())
            throw std::runtime_error (errors);
    }

    cmaj::transformations::runBasicResolutionPasses (program);
}

static void addStandardLibraryToProgram (cmaj::AST::Program& program,
                                         const std::set<std::filesystem::path>& files)
{
    for (auto& path : files)
    {
        auto& file = program.allocator.sourceFileList.add (path.filename().string(),
                                                           choc::file::loadFileAsString (path.string()), true);
        program.parse (file, true);
    }
}

static void buildProgram (cmaj::AST::Program& program, const std::vector<std::string>& sourceFiles, bool runResolutionPasses)
{
    std::set<std::filesystem::path> sortedLibraryFiles;
    bool isStandardLibrary = false;

    for (auto& name : sourceFiles)
    {
        auto path = std::filesystem::path (name);

        if (is_directory (path))
        {
            if (choc::text::contains (path.generic_string(), "/standard_library/"))
                isStandardLibrary = true;

            for (auto& f : std::filesystem::directory_iterator { path })
                if (f.path().extension() == ".cmajor")
                    sortedLibraryFiles.insert (f.path());
        }
        else
        {
            sortedLibraryFiles.insert (path);
        }
    }

    if (isStandardLibrary)
        addStandardLibraryToProgram (program, sortedLibraryFiles);
    else
        addSourceToProgram (program, sortedLibraryFiles);

    if (runResolutionPasses)
        cmaj::transformations::runBasicResolutionPasses (program);
}

//==============================================================================
void generateJSONSyntaxTree (const std::string& outputFile, const std::vector<std::string>& sourceFiles)
{
    cmaj::AST::Program program (true);
    buildProgram (program, sourceFiles, false);

    cmaj::SyntaxTreeOptions options;
    options.includeSourceLocations    = true;
    options.includeComments           = true;
    options.includeFunctionContents   = false;
    std::string json = choc::com::StringPtr (program.getSyntaxTree (options));
    writeToOutput (outputFile, json);
}

//==============================================================================
void generateBinaryModule (const std::string& outputFile, const std::vector<std::string>& sourceFiles, bool shouldObfuscate)
{
    cmaj::AST::Program program;
    buildProgram (program, sourceFiles, true);

    if (shouldObfuscate)
        cmaj::transformations::obfuscateNames (program);

    cmaj::AST::ObjectRefVector<cmaj::AST::ModuleBase> modules;

    for (auto& m : program.rootNamespace.getSubModules())
        if (! m->isSystemModule())
            modules.push_back (m);

    auto binaryData = cmaj::transformations::createBinaryModule (modules);
    writeToOutput (outputFile, std::string_view (reinterpret_cast<const char*> (binaryData.data()), binaryData.size()));
}

//==============================================================================
void generateBinaryStdLib (const std::string& repoFolder)
{
    auto contentTemplate = choc::text::trimStart (R"(
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

/*
   This file is generated using the command-line tool's special generator tool:

   cmaj generate --target=module --output={location of repo}  std_library
*/

//==============================================================================
namespace cmaj
{
static constexpr uint8_t standardLibraryData[] =
{
LIBRARY_DATA
};
}
)");

    cmaj::AST::Program program;
    buildProgram (program, { repoFolder + "/cmajor/standard_library",
                             repoFolder + "/cmajor/standard_library/internal" }, true);

    auto modules = program.rootNamespace.getSubModules();
    CMAJ_ASSERT (modules.size() == 1);
    auto binaryData = cmaj::transformations::createBinaryModule (modules);

    std::string cppData, line;

    for (auto b : binaryData)
    {
        if (line.length() > 200)
        {
            cppData += line + ",\n";
            line = {};
        }

        if (line.empty())
            line = "    ";
        else
            line += ", ";

        line += std::to_string (b);
    }

    cppData += line;

    auto outputFile = repoFolder + "/modules/compiler/src/standard_library/cmaj_StandardLibraryBinary.h";

    auto newContents      = choc::text::replace (contentTemplate, "LIBRARY_DATA", cppData);
    auto previousContents = choc::file::loadFileAsString (outputFile);

    if (previousContents == newContents)
    {
        throw std::runtime_error ("Library matches - nothing to do");
    }

    std::cerr << "Updating binary library file" << std::endl;
    choc::file::replaceFileWithContent (outputFile, newContents);
}

void writeIndexFiles (const std::filesystem::path& parentPath,
                      const cmaj::HTMLDocGenerator::Index& index,
                      size_t navOrder = 1)
{
    auto indexPath = parentPath;
    indexPath.append (index.anchor);

    auto* parentIndex = index.parent;

    std::ostringstream contents;

    contents << "---" << std::endl;
    contents << "title: " << index.description << std::endl;
    contents << "permalink: " << "/docs/StandardLibrary#" << index.anchor << std::endl;
    contents << "parent: " << parentIndex->description << std::endl;

    if (parentIndex->parent != nullptr)
        contents << "grand_parent: " << parentIndex->parent->description << std::endl;

    contents << "has_children: " << (index.childIndexes.empty() ? "False" : "True") << std::endl;
    contents << "nav_order: " << navOrder << std::endl;
    contents << "---" << std::endl;

    choc::file::replaceFileWithContent (indexPath.string() + "/" + index.anchor + ".md", contents.str());

    size_t n = 0;

    for (auto& child : index.childIndexes)
        writeIndexFiles (indexPath, *child, n++);
}

//==============================================================================
void generateHTMLDocs (const std::string& outputFile, const std::vector<std::string>& sourceFiles)
{
    cmaj::AST::Program program (true);
    buildProgram (program, sourceFiles, true);

    cmaj::HTMLDocGenerator generator;

    auto html = generator.generate (program, "Cmajor", "../assets/css/cmaj_doc_style.css");
    choc::file::replaceFileWithContent (outputFile + "/main.html", html);

    generator.rootIndex.description = "Standard Library";

    size_t n = 0;

    for (auto& indexFile : generator.rootIndex.childIndexes)
        writeIndexFiles (outputFile, *indexFile, n++);
}
