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

#include "../../../modules/compiler/include/cmaj_ErrorHandling.h"
#include "choc/platform/choc_UnitTest.h"
#include "choc/containers/choc_ArgumentList.h"

#include "../../../modules/server/include/cmaj_PatchPlayerServer.h"
#include "../../../include/cmajor/COM/cmaj_Library.h"
#include "unit_tests/cmaj_APIUnitTests.h"
#include "unit_tests/cmaj_PatchHelperUnitTests.h"
#include "unit_tests/cmaj_GraphvizUnitTests.h"
#include "unit_tests/cmaj_CLAPPluginUnitTests.h"

//==============================================================================
static void runAllTests (choc::test::TestProgress& progress)
{
    /// Add your tests here!

    cmaj::api_tests::runUnitTests (progress);
    cmaj::patch_helper_tests::runUnitTests (progress);
    cmaj::graphviz_tests::runUnitTests (progress);
    cmaj::plugin::clap::test::runUnitTests (progress);
    cmaj::runServerUnitTests (progress);
}


//==============================================================================
void runUnitTests (choc::ArgumentList& args, const choc::value::Value&, cmaj::BuildSettings&)
{
    int iterations = 1;

    if (auto it = args.removeIntValue<int> ("--iterations"))
        iterations = *it;

    while (iterations > 0)
    {
        choc::test::TestProgress progress;
        runAllTests (progress);
        progress.printReport();

        if (progress.numFails > 0)
            throw std::runtime_error ("");

        --iterations;
    }
}
