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

#include "cmajor/helpers/cmaj_PatchHelpers.h"

namespace cmaj::patch_parameter_tests
{

//==============================================================================
/// Builds a PatchParameterProperties from an annotation, without needing to
/// compile or load a patch, so that the parameter logic can be tested in isolation.
struct TestParameter
{
    choc::value::Value annotation { choc::value::createObject ("") };
    bool isIntegerType = false;

    template <typename Type>
    TestParameter& with (std::string_view name, Type value)
    {
        annotation.setMember (name, value);
        return *this;
    }

    /// Makes the endpoint take an int32, which sets the `shouldRound` property
    TestParameter& withIntegerType()
    {
        isIntegerType = true;
        return *this;
    }

    PatchParameterProperties create() const
    {
        EndpointDetails details;
        details.endpointID = EndpointID::create (std::string_view ("testParam"));
        details.endpointType = EndpointType::event;
        details.isInput = true;
        details.dataTypes.push_back (isIntegerType ? choc::value::Type::createInt32()
                                                   : choc::value::Type::createFloat32());
        details.annotation = annotation;

        return PatchParameterProperties (details);
    }
};

static PatchParameterProperties createParameter (float minValue, float maxValue, float step)
{
    return TestParameter().with ("min", minValue)
                          .with ("max", maxValue)
                          .with ("step", step)
                          .create();
}

static PatchParameterProperties createParameter (float minValue, float maxValue)
{
    return TestParameter().with ("min", minValue)
                          .with ("max", maxValue)
                          .create();
}

//==============================================================================
/// Returns the canonical legal value for the given step index, i.e. minValue plus a
/// whole number of steps. Note that because a step such as 0.1 isn't exactly
/// representable as a float, this isn't always the same as the float nearest to the
/// decimal value you'd write down.
static float getLegalValueForStepIndex (const PatchParameterProperties& p, int64_t index)
{
    return static_cast<float> (static_cast<double> (p.minValue)
                                + static_cast<double> (index) * static_cast<double> (p.step));
}

//==============================================================================
/// A range to sweep over, used by the tests which check every legal value of a parameter.
/// The number of steps is stated explicitly so that these tests don't just repeat the
/// implementation's own arithmetic: every range here is chosen so that minValue plus
/// numSteps steps lands exactly on maxValue.
struct TestRange
{
    float minValue, maxValue, step;
    int64_t numSteps;

    PatchParameterProperties createParameter() const
    {
        return cmaj::patch_parameter_tests::createParameter (minValue, maxValue, step);
    }

    /// The value that snapping should return for anything within half a step of this index
    float getLegalValue (int64_t index) const
    {
        if (index >= numSteps)
            return maxValue;

        auto p = createParameter();
        return getLegalValueForStepIndex (p, index);
    }

    std::string getDescription() const
    {
        return "min=" + std::to_string (minValue)
             + " max=" + std::to_string (maxValue)
             + " step=" + std::to_string (step);
    }
};

/// The ranges used by the sweep tests, each a whole number of steps wide
static std::vector<TestRange> getSteppedTestRanges()
{
    return { TestRange { 0.0f,   10.0f,  0.1f,   100 },
             TestRange { 0.0f,   1.0f,   0.01f,  100 },
             TestRange { 0.0f,   127.0f, 1.0f,   127 },
             TestRange { -1.5f,  1.5f,   0.5f,   6 },
             TestRange { 1.0f,   10.0f,  3.0f,   3 },
             TestRange { 20.0f,  100.0f, 0.001f, 80000 },
             TestRange { -50.0f, 50.0f,  0.01f,  10000 },
             TestRange { 0.05f,  1.05f,  0.1f,   10 },
             TestRange { 0.0f,   5.0f,   0.25f,  20 } };
}

//==============================================================================
static void runParameterPropertyTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (PatchParameterProperties);

    {
        CHOC_TEST (DefaultProperties)

        auto p = TestParameter().create();

        // with an empty annotation, a parameter should default to a 0 to 1 range
        CHOC_EXPECT_EQ (p.minValue, 0.0f);
        CHOC_EXPECT_EQ (p.maxValue, 1.0f);
        CHOC_EXPECT_EQ (p.step, 0.0f);
        CHOC_EXPECT_EQ (p.defaultValue, 0.0f);
        CHOC_EXPECT_TRUE (p.automatable);
        CHOC_EXPECT_FALSE (p.boolean);
        CHOC_EXPECT_FALSE (p.hidden);
        CHOC_EXPECT_FALSE (p.discrete);
        CHOC_EXPECT_EQ (p.rampFrames, 0u);
        CHOC_EXPECT_EQ (p.getNumDiscreteOptions(), 0u);
        CHOC_EXPECT_TRUE (p.valueStrings.empty());
    }

    {
        CHOC_TEST (AnnotationParsing)

        auto p = TestParameter().with ("name", "Cutoff")
                                .with ("unit", "Hz")
                                .with ("group", "Filter")
                                .with ("min", 20.0f)
                                .with ("max", 20000.0f)
                                .with ("step", 0.5f)
                                .with ("init", 1000.0f)
                                .with ("automatable", false)
                                .with ("boolean", false)
                                .with ("hidden", true)
                                .with ("discrete", false)
                                .with ("rampFrames", 128)
                                .create();

        CHOC_EXPECT_EQ (p.endpointID, "testParam");
        CHOC_EXPECT_EQ (p.name, "Cutoff");
        CHOC_EXPECT_EQ (p.unit, "Hz");
        CHOC_EXPECT_EQ (p.group, "Filter");
        CHOC_EXPECT_EQ (p.minValue, 20.0f);
        CHOC_EXPECT_EQ (p.maxValue, 20000.0f);
        CHOC_EXPECT_EQ (p.step, 0.5f);
        CHOC_EXPECT_EQ (p.defaultValue, 1000.0f);
        CHOC_EXPECT_FALSE (p.automatable);
        CHOC_EXPECT_TRUE (p.hidden);
        CHOC_EXPECT_EQ (p.rampFrames, 128u);

        // if no name is given, the endpoint ID should be used
        CHOC_EXPECT_EQ (TestParameter().create().name, "testParam");

        // if no init is given, the default should be the minimum
        CHOC_EXPECT_EQ (createParameter (3.0f, 9.0f).defaultValue, 3.0f);
    }

    {
        CHOC_TEST (ConstrainWithNoStep)

        auto p = createParameter (-2.0f, 6.0f);

        // with no step, any in-range value must be returned completely untouched
        for (auto v : { -2.0f, -1.999f, -0.5f, 0.0f, 0.1f, 1.0f, 3.14159f, 5.9999f, 6.0f })
            CHOC_EXPECT_EQ (p.snapAndConstrainValue (v), v);

        // out-of-range values should be clamped to the ends of the range
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (-2.5f), -2.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (-1000.0f), -2.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (6.5f), 6.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1000.0f), 6.0f);
    }

    {
        CHOC_TEST (StepGridIsAnchoredAtMinValue)

        // With min=1, max=10 and step=3, the legal values are 1, 4, 7 and 10. Snapping to
        // absolute multiples of the step instead would push each of these *down* to the
        // next multiple of 3 (i.e. 4 -> 3, 7 -> 6, 10 -> 9).
        auto p = createParameter (1.0f, 10.0f, 3.0f);

        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1.0f), 1.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (4.0f), 4.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (7.0f), 7.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (10.0f), 10.0f);

        // and values in between should go to the nearest of those
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1.9f), 1.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (2.6f), 4.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (5.4f), 4.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (5.6f), 7.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (9.9f), 10.0f);

        // the step grid must never produce a value outside the range
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (-100.0f), 1.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (100.0f), 10.0f);

        // a non-integer offset min should behave the same way
        auto q = createParameter (0.05f, 1.05f, 0.5f);
        CHOC_EXPECT_EQ (q.snapAndConstrainValue (0.05f), 0.05f);
        CHOC_EXPECT_EQ (q.snapAndConstrainValue (0.55f), 0.55f);
        CHOC_EXPECT_EQ (q.snapAndConstrainValue (1.05f), 1.05f);
    }

    {
        CHOC_TEST (StepSnapsToNearestLegalValue)

        auto p = createParameter (0.0f, 1.0f, 0.25f);

        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.0f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.12f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.13f), 0.25f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.25f), 0.25f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.37f), 0.25f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.38f), 0.5f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.5f), 0.5f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.99f), 1.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1.0f), 1.0f);
    }

    {
        CHOC_TEST (StepWithNegativeRange)

        auto p = createParameter (-1.5f, 1.5f, 0.5f);

        for (auto v : { -1.5f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 1.5f })
            CHOC_EXPECT_EQ (p.snapAndConstrainValue (v), v);

        // rounding must be symmetrical around zero rather than biased in one direction
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (-0.3f), -0.5f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (-0.2f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.2f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.3f), 0.5f);
    }

    {
        CHOC_TEST (StepWhenRangeIsNotAWholeNumberOfSteps)

        // 0 to 1 in steps of 0.3 gives legal values of 0, 0.3, 0.6 and 0.9 - maxValue
        // itself isn't on the grid, so the top legal value is the last whole step
        auto p = createParameter (0.0f, 1.0f, 0.3f);

        // NB: 0.3 isn't exactly representable as a float, so the legal value for step 3
        // is minValue + 3 * step, which is an ulp away from the float nearest to 0.9.
        // Snapping returns that canonical grid value rather than the decimal literal.
        auto topValue = getLegalValueForStepIndex (p, 3);

        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.0f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.3f), getLegalValueForStepIndex (p, 1));
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.6f), getLegalValueForStepIndex (p, 2));
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.9f), topValue);
        CHOC_EXPECT_NEAR (topValue, 0.9f, 0.0001f);

        // the result must stay on the grid rather than being clamped to an illegal maxValue
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1.0f), topValue);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (5.0f), topValue);
    }

    {
        CHOC_TEST (SmallStepValue)

        auto p = createParameter (0.0f, 1.0f, 0.0001f);

        CHOC_EXPECT_NEAR (p.snapAndConstrainValue (0.2f), 0.2f, 0.000001f);
    }

    {
        CHOC_TEST (StepAlwaysAllowsMinAndMaxValues)

        // The ends of the range are the values most likely to be round-tripped by a host,
        // so when maxValue is a whole number of steps above minValue it must come back
        // exactly, rather than being truncated down to the step below it.
        for (auto range : getSteppedTestRanges())
        {
            auto p = range.createParameter();

            if (p.snapAndConstrainValue (range.minValue) != range.minValue
                 || p.snapAndConstrainValue (range.maxValue) != range.maxValue)
                progress.print ("  failing range: " + range.getDescription());

            CHOC_EXPECT_EQ (p.snapAndConstrainValue (range.minValue), range.minValue);
            CHOC_EXPECT_EQ (p.snapAndConstrainValue (range.maxValue), range.maxValue);
        }
    }

    {
        CHOC_TEST (StepReturnsLegalValuesUnchanged)

        // Every legal value of a stepped parameter must survive snapping completely
        // unchanged, otherwise hosts which compare the snapped value against the one
        // they sent will see spurious parameter changes. A float divide-then-multiply
        // doesn't round-trip, so this caught a lot of values before it was fixed.
        for (auto range : getSteppedTestRanges())
        {
            auto p = range.createParameter();
            int numChanged = 0, numNotIdempotent = 0;

            for (int64_t i = 0; i <= range.numSteps; ++i)
            {
                auto legalValue = range.getLegalValue (i);
                auto snapped = p.snapAndConstrainValue (legalValue);

                if (snapped != legalValue)
                    ++numChanged;

                // snapping an already-snapped value must be a no-op
                if (p.snapAndConstrainValue (snapped) != snapped)
                    ++numNotIdempotent;
            }

            if (numChanged != 0 || numNotIdempotent != 0)
                progress.print ("  failing range: " + range.getDescription());

            CHOC_EXPECT_EQ (numChanged, 0);
            CHOC_EXPECT_EQ (numNotIdempotent, 0);
        }
    }

    {
        CHOC_TEST (StepSnapsNudgedValuesToTheNearestLegalValue)

        // A value which is a third of a step away from a legal value must snap back to it.
        for (auto range : getSteppedTestRanges())
        {
            auto p = range.createParameter();
            int numWrong = 0;

            for (int64_t i = 0; i <= range.numSteps; ++i)
            {
                auto legalValue = range.getLegalValue (i);

                for (auto offset : { -0.33f, 0.33f })
                {
                    auto nudged = static_cast<float> (legalValue + offset * p.step);

                    if (nudged < p.minValue || nudged > p.maxValue)
                        continue;

                    if (std::abs (p.snapAndConstrainValue (nudged) - legalValue) > p.step * 0.01f)
                        ++numWrong;
                }
            }

            if (numWrong != 0)
                progress.print ("  failing range: " + range.getDescription());

            CHOC_EXPECT_EQ (numWrong, 0);
        }
    }

    {
        CHOC_TEST (TextOptionsDefineTheirOwnRange)

        auto p = TestParameter().with ("text", "Low|Mid|High").create();

        // with no explicit range, the options should map onto 0 to numOptions - 1
        CHOC_EXPECT_EQ (p.minValue, 0.0f);
        CHOC_EXPECT_EQ (p.maxValue, 2.0f);
        CHOC_EXPECT_EQ (p.getNumDiscreteOptions(), 3u);
        CHOC_EXPECT_EQ (p.valueStrings.size(), 3u);

        // an explicit min and max should be honoured instead
        auto q = TestParameter().with ("text", "Low|Mid|High")
                                .with ("min", 10.0f)
                                .with ("max", 20.0f)
                                .create();

        CHOC_EXPECT_EQ (q.minValue, 10.0f);
        CHOC_EXPECT_EQ (q.maxValue, 20.0f);
        CHOC_EXPECT_EQ (q.getNumDiscreteOptions(), 3u);

        // note that both a min *and* a max are needed to override the range: supplying
        // only one of them leaves the option-index range in place
        auto r = TestParameter().with ("text", "Low|Mid|High").with ("min", 10.0f).create();
        CHOC_EXPECT_EQ (r.minValue, 0.0f);
        CHOC_EXPECT_EQ (r.maxValue, 2.0f);
    }

    {
        CHOC_TEST (TextOptionsSnapToNearestOption)

        auto p = TestParameter().with ("text", "Low|Mid|High").create();

        // the option values themselves must be preserved exactly
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.0f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1.0f), 1.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (2.0f), 2.0f);

        // and the switch-over points should sit half way between the options, so that
        // each value goes to the option nearest to it
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.49f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.51f), 1.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1.49f), 1.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1.51f), 2.0f);

        // out of range values should land on the end options
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (-10.0f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (10.0f), 2.0f);
    }

    {
        CHOC_TEST (TextOptionsSnapToNearestOptionOverCustomRange)

        // four options spread over 0 to 3 gives option values of 0, 1, 2 and 3
        auto p = TestParameter().with ("text", "A|B|C|D").create();

        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1.4f), 1.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1.6f), 2.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (2.4f), 2.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (2.6f), 3.0f);

        // the same options spread over -1 to 1 gives values of -1, -1/3, 1/3 and 1
        auto q = TestParameter().with ("text", "A|B|C|D")
                                .with ("min", -1.0f)
                                .with ("max", 1.0f)
                                .create();

        CHOC_EXPECT_EQ (q.snapAndConstrainValue (-1.0f), -1.0f);
        CHOC_EXPECT_EQ (q.snapAndConstrainValue (1.0f), 1.0f);
        CHOC_EXPECT_NEAR (q.snapAndConstrainValue (0.3f), 1.0f / 3.0f, 0.0001f);
        CHOC_EXPECT_NEAR (q.snapAndConstrainValue (-0.4f), -1.0f / 3.0f, 0.0001f);
    }

    {
        CHOC_TEST (EveryOptionValueSurvivesSnapping)

        // Each option's own value must snap back to itself - the CLAP and VST wrappers
        // rely on this to convert between option indexes and parameter values.
        struct OptionTest { const char* text; float minValue, maxValue; bool hasRange; };

        for (auto test : { OptionTest { "A|B", 0, 1, false },
                           OptionTest { "A|B|C", 0, 2, false },
                           OptionTest { "A|B|C|D|E", 0, 4, false },
                           OptionTest { "A|B|C", 10, 20, true },
                           OptionTest { "A|B|C|D", -1, 1, true },
                           OptionTest { "A|B|C|D|E|F|G", 0.5f, 100.0f, true } })
        {
            auto builder = TestParameter().with ("text", test.text);

            if (test.hasRange)
                builder.with ("min", test.minValue).with ("max", test.maxValue);

            auto p = builder.create();
            auto numOptions = p.getNumDiscreteOptions();
            int numChanged = 0;

            for (uint64_t i = 0; i < numOptions; ++i)
            {
                auto optionValue = p.convertFrom0to1 (static_cast<float> (i)
                                                        / static_cast<float> (numOptions - 1u));
                auto snapped = p.snapAndConstrainValue (optionValue);

                // snapping must be stable, even if float division means the option value
                // itself isn't exactly reproducible
                if (p.snapAndConstrainValue (snapped) != snapped)
                    ++numChanged;

                if (std::abs (snapped - optionValue) > std::abs (p.maxValue - p.minValue) * 1.0e-5f)
                    ++numChanged;
            }

            if (numChanged != 0)
                progress.print (std::string ("  failing options: ") + test.text);

            CHOC_EXPECT_EQ (numChanged, 0);
        }
    }

    {
        CHOC_TEST (BooleanParameters)

        auto p = TestParameter().with ("boolean", true).create();

        CHOC_EXPECT_EQ (p.getNumDiscreteOptions(), 2u);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.0f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.1f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.49f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.51f), 1.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (1.0f), 1.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (-5.0f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (5.0f), 1.0f);
    }

    {
        CHOC_TEST (DiscreteParametersWithAStep)

        auto p = TestParameter().with ("min", 0.0f)
                                .with ("max", 10.0f)
                                .with ("step", 0.1f)
                                .with ("discrete", true)
                                .create();

        CHOC_EXPECT_EQ (p.getNumDiscreteOptions(), 101u);

        for (auto v : { 0.0f, 0.1f, 0.5f, 1.0f, 5.0f, 9.9f, 10.0f })
            CHOC_EXPECT_NEAR (p.snapAndConstrainValue (v), v, 0.0001f);

        // values in between should snap to the nearest option, not be truncated downwards
        CHOC_EXPECT_NEAR (p.snapAndConstrainValue (5.04f), 5.0f, 0.0001f);
        CHOC_EXPECT_NEAR (p.snapAndConstrainValue (5.06f), 5.1f, 0.0001f);

        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.0f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (10.0f), 10.0f);
    }

    {
        CHOC_TEST (DiscreteIntegerParameters)

        auto p = TestParameter().with ("min", 1.0f)
                                .with ("max", 5.0f)
                                .with ("step", 1.0f)
                                .with ("discrete", true)
                                .withIntegerType()
                                .create();

        CHOC_EXPECT_TRUE (p.shouldRound);
        CHOC_EXPECT_EQ (p.getNumDiscreteOptions(), 5u);

        for (auto v : { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f })
            CHOC_EXPECT_EQ (p.snapAndConstrainValue (v), v);

        // integer parameters should pick the nearest whole value
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (2.4f), 2.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (2.6f), 3.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (3.4f), 3.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (3.6f), 4.0f);
    }

    {
        CHOC_TEST (IntegerTextOptionsAreRounded)

        // seven options spread over 0 to 10 would land on fractional values, but an
        // integer endpoint should round them to whole numbers
        auto p = TestParameter().with ("text", "A|B|C|D")
                                .with ("min", 0.0f)
                                .with ("max", 10.0f)
                                .withIntegerType()
                                .create();

        CHOC_EXPECT_TRUE (p.shouldRound);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.0f), 0.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (3.0f), 3.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (7.0f), 7.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (10.0f), 10.0f);
    }

    {
        CHOC_TEST (DiscreteStepThatDoesNotDivideTheRangeExactly)

        // NB: this documents a known wart rather than ideal behaviour. When `discrete`
        // is set, the option count is trunc((max - min) / step) + 1 and the options are
        // then spread evenly across the range, so if the step doesn't divide the range
        // exactly then the effective step is larger than the one that was asked for.
        // Here, 0 to 0.9 in steps of 0.3 gives 3 options spaced 0.45 apart.
        auto p = TestParameter().with ("min", 0.0f)
                                .with ("max", 0.9f)
                                .with ("step", 0.3f)
                                .with ("discrete", true)
                                .create();

        CHOC_EXPECT_EQ (p.getNumDiscreteOptions(), 3u);
        CHOC_EXPECT_NEAR (p.snapAndConstrainValue (0.45f), 0.45f, 0.0001f);

        // whereas a non-discrete parameter with the same range honours the step exactly
        // (0.45 is deliberately avoided here, as it's an exact tie between two steps)
        auto q = createParameter (0.0f, 0.9f, 0.3f);
        CHOC_EXPECT_NEAR (q.snapAndConstrainValue (0.5f), 0.6f, 0.0001f);
        CHOC_EXPECT_NEAR (q.snapAndConstrainValue (0.4f), 0.3f, 0.0001f);
        CHOC_EXPECT_NEAR (q.snapAndConstrainValue (0.3f), 0.3f, 0.0001f);
    }

    {
        CHOC_TEST (NumDiscreteOptions)

        CHOC_EXPECT_EQ (createParameter (0.0f, 1.0f).getNumDiscreteOptions(), 0u);
        CHOC_EXPECT_EQ (createParameter (0.0f, 1.0f, 0.1f).getNumDiscreteOptions(), 0u);
        CHOC_EXPECT_EQ (TestParameter().with ("text", "A|B|C").create().getNumDiscreteOptions(), 3u);
        CHOC_EXPECT_EQ (TestParameter().with ("boolean", true).create().getNumDiscreteOptions(), 2u);

        // a single text entry is a format string rather than a list of options
        CHOC_EXPECT_EQ (TestParameter().with ("text", "%d dB").create().getNumDiscreteOptions(), 0u);

        CHOC_EXPECT_EQ (TestParameter().with ("min", 0.0f).with ("max", 1.0f)
                                       .with ("step", 0.1f).with ("discrete", true)
                                       .create().getNumDiscreteOptions(), 11u);
    }

    {
        CHOC_TEST (ZeroSizedRange)

        // a degenerate range shouldn't produce NaNs or infinities
        auto p = createParameter (5.0f, 5.0f, 1.0f);

        CHOC_EXPECT_EQ (p.snapAndConstrainValue (5.0f), 5.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (0.0f), 5.0f);
        CHOC_EXPECT_EQ (p.snapAndConstrainValue (100.0f), 5.0f);
    }

    {
        CHOC_TEST (NormalisedConversions)

        auto p = createParameter (20.0f, 20.0f + 80.0f);

        CHOC_EXPECT_EQ (p.convertTo0to1 (20.0f), 0.0f);
        CHOC_EXPECT_EQ (p.convertTo0to1 (60.0f), 0.5f);
        CHOC_EXPECT_EQ (p.convertTo0to1 (100.0f), 1.0f);

        // out of range values should be clamped
        CHOC_EXPECT_EQ (p.convertTo0to1 (-100.0f), 0.0f);
        CHOC_EXPECT_EQ (p.convertTo0to1 (1000.0f), 1.0f);

        CHOC_EXPECT_EQ (p.convertFrom0to1 (0.0f), 20.0f);
        CHOC_EXPECT_EQ (p.convertFrom0to1 (0.5f), 60.0f);
        CHOC_EXPECT_EQ (p.convertFrom0to1 (1.0f), 100.0f);

        for (auto v : { 20.0f, 25.0f, 50.0f, 99.0f, 100.0f })
            CHOC_EXPECT_NEAR (p.convertFrom0to1 (p.convertTo0to1 (v)), v, 0.0001f);
    }

    {
        CHOC_TEST (ValueToString)

        // a list of options should use the option names
        auto options = TestParameter().with ("text", "Low|Mid|High").create();
        CHOC_EXPECT_EQ (options.getValueAsString (0.0f), "Low");
        CHOC_EXPECT_EQ (options.getValueAsString (1.0f), "Mid");
        CHOC_EXPECT_EQ (options.getValueAsString (2.0f), "High");

        // and should pick the nearest option for in-between values
        CHOC_EXPECT_EQ (options.getValueAsString (0.51f), "Mid");
        CHOC_EXPECT_EQ (options.getValueAsString (1.51f), "High");
        CHOC_EXPECT_EQ (options.getValueAsString (-3.0f), "Low");
        CHOC_EXPECT_EQ (options.getValueAsString (3.0f), "High");

        // a single entry is used as a format string
        auto formatted = TestParameter().with ("text", "%d dB")
                                        .with ("min", 0.0f)
                                        .with ("max", 10.0f)
                                        .create();

        CHOC_EXPECT_EQ (formatted.getValueAsString (3.0f), "3 dB");
    }

    {
        CHOC_TEST (StringToValue)

        auto options = TestParameter().with ("text", "Low|Mid|High").create();

        // option names should be recognised, ignoring case and surrounding whitespace
        CHOC_EXPECT_TRUE (options.getStringAsValue ("Low").has_value());
        CHOC_EXPECT_EQ (*options.getStringAsValue ("Low"), 0.0f);
        CHOC_EXPECT_EQ (*options.getStringAsValue ("mid"), 1.0f);
        CHOC_EXPECT_EQ (*options.getStringAsValue ("  HIGH  "), 2.0f);

        auto p = createParameter (0.0f, 10.0f);

        CHOC_EXPECT_EQ (*p.getStringAsValue ("2.5"), 2.5f);
        CHOC_EXPECT_EQ (*p.getStringAsValue (" -3 "), -3.0f);

        // out of range values are returned as-is, for the caller to deal with
        CHOC_EXPECT_EQ (*p.getStringAsValue ("500"), 500.0f);

        // and something non-numeric gives nothing at all
        CHOC_EXPECT_FALSE (p.getStringAsValue ("not a number").has_value());
        CHOC_EXPECT_FALSE (p.getStringAsValue ("").has_value());
    }

    {
        CHOC_TEST (ParseValue)

        auto p = TestParameter().with ("text", "Low|Mid|High").with ("init", 1.0f).create();

        CHOC_EXPECT_EQ (p.parseValue (choc::value::createFloat32 (2.0f)), 2.0f);
        CHOC_EXPECT_EQ (p.parseValue (choc::value::createInt32 (1)), 1.0f);
        CHOC_EXPECT_EQ (p.parseValue (choc::value::createString ("High")), 2.0f);

        // anything unusable should fall back to the default value
        CHOC_EXPECT_EQ (p.parseValue (choc::value::createString ("nonsense")), p.defaultValue);
        CHOC_EXPECT_EQ (p.parseValue (choc::value::Value()), p.defaultValue);
    }
}

//==============================================================================
inline void runUnitTests (choc::test::TestProgress& progress)
{
    runParameterPropertyTests (progress);
}

} // namespace cmaj::patch_parameter_tests
