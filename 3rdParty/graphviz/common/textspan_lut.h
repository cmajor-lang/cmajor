/**
 * @file
 * @brief lookup table for textspan
 */

#pragma once

// LUT is short for lookup table.

/// @param text a single line of ASCII text which should contain no control characters.
/// @returns the estimated width of `text` in 1 point. A value is always returned, falling back to Times-Roman metrics
/// if there is no hard-coded lookup table for the given `font_name`.
double estimate_text_width_1pt(const char* font_name, const char* text, bool bold, bool italic);
