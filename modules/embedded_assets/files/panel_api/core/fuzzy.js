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
    Fuzzy matching for the command palette: the query's characters must all appear in the text,
    in order, ignoring case. A match scores higher when its characters start words (after a
    space, punctuation, or a lower-to-upper case change), when they run on consecutively, and
    when they start early; gaps between them cost a little.

    fuzzyMatch ("rst", "Reset") -> { score, indices: [0, 2, 4] }, or null if there's no match.
*/

// (a consecutive character is worth as much as one starting a word, so that a run isn't
// beaten by skipping ahead to a word start)
const bonusWordStart   = 8;
const bonusFirstChar   = 4;
const bonusConsecutive = 8;
const bonusExactCase   = 1;
const penaltyGap       = 1;
const penaltyMaxGap    = 3;
const penaltyLeading   = 0.5;
const penaltyLeadingWord = 1;

export function fuzzyMatch (query, text)
{
    query = String (query ?? "").trim();
    text = String (text ?? "");

    if (query === "")
        return { score: 0, indices: [] };

    const lowerQuery = query.toLowerCase().replace (/\s+/g, " ");
    const lowerText = text.toLowerCase();

    if (lowerQuery.length > lowerText.length)
        return null;

    // The best-scoring alignment, by dynamic programming: best[i][j] is the best score of
    // matching the first i+1 query characters with query char i at text position j.
    const n = lowerQuery.length, m = lowerText.length;
    const best = new Array (n), from = new Array (n);

    for (let i = 0; i < n; ++i)
    {
        best[i] = new Float64Array (m).fill (-Infinity);
        from[i] = new Int32Array (m).fill (-1);
        const q = lowerQuery[i];

        for (let j = i; j < m; ++j)
        {
            if (lowerText[j] !== q)
                continue;

            const charScore = 1 + (isWordStart (text, j) ? bonusWordStart : 0) + (text[j] === query[i] ? bonusExactCase : 0);

            if (i === 0)
            {
                // (a match that starts a word pays for the words it skips, not their lengths, so
                // "graph" scores the same in "Show the graph" as in "Fit the graph to the view")
                const leading = isWordStart (text, j) ? countWordStartsBefore (text, j) * penaltyLeadingWord
                                                      : Math.min (j, 10) * penaltyLeading;
                best[0][j] = charScore + (j === 0 ? bonusFirstChar : 0) - leading;
                continue;
            }

            for (let k = i - 1; k < j; ++k)
            {
                const previous = best[i - 1][k];

                if (previous === -Infinity)
                    continue;

                const gap = j - k - 1;
                const score = previous + charScore + (gap === 0 ? bonusConsecutive : - Math.min (gap * penaltyGap, penaltyMaxGap));

                if (score > best[i][j])
                {
                    best[i][j] = score;
                    from[i][j] = k;
                }
            }
        }
    }

    let end = -1, score = -Infinity;

    for (let j = 0; j < m; ++j)
        if (best[n - 1][j] > score)
            score = best[n - 1][j], end = j;

    if (end < 0)
        return null;

    const indices = new Array (n);

    for (let i = n - 1, j = end; i >= 0; --i)
    {
        indices[i] = j;
        j = from[i][j];
    }

    // (a slight preference for shorter texts, so "Reset" beats "Reset the graph view")
    return { score: score - m * 0.01, indices };
}

function isWordStart (text, index)
{
    if (index === 0)
        return true;

    const previous = text[index - 1], c = text[index];

    if (/[\s_\-.:/()[\]]/.test (previous))
        return true;

    // camelCase, and a digit after a letter
    return (previous === previous.toLowerCase() && c !== c.toLowerCase())
             || (/\d/.test (c) && ! /\d/.test (previous));
}

function countWordStartsBefore (text, index)
{
    let count = 0;

    for (let i = 0; i < index; ++i)
        if (isWordStart (text, i) && /\S/.test (text[i]))
            ++count;

    return count;
}

/// Splits a text into runs of matched and unmatched characters, for highlighting:
/// [{ text, isMatch }]
export function splitByMatches (text, indices)
{
    const runs = [];
    const matched = new Set (indices ?? []);

    for (let i = 0; i < text.length; ++i)
    {
        const isMatch = matched.has (i);
        const last = runs[runs.length - 1];

        if (last && last.isMatch === isMatch)
            last.text += text[i];
        else
            runs.push ({ text: text[i], isMatch });
    }

    return runs;
}
