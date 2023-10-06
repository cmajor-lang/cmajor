/**
 * @file
 * @brief @ref xml_escape
 */

#include "../cgraph/exit.h"
#include "../cgraph/unreachable.h"
#include "types.h"
#include "utils.h"

/* return true if *s points to &[A-Za-z]+;      (e.g. &Ccedil; )
 *                          or &#[0-9]*;        (e.g. &#38; )
 *                          or &#x[0-9a-fA-F]*; (e.g. &#x6C34; )
 */
static bool xml_isentity(const char *s)
{
    s++;			/* already known to be '&' */
    if (*s == ';') { // '&;' is not a valid entity
	return false;
    }
    if (*s == '#') {
	s++;
	if (*s == 'x' || *s == 'X') {
	    s++;
	    while (isxdigit((int)*s))
		s++;
	} else {
	    while (isdigit((int)*s))
		s++;
	}
    } else {
	while (isalpha_no_locale(*s))
	    s++;
    }
    if (*s == ';')
	return true;
    return false;
}

/** XML-escape a character
 *
 * \param previous The source character preceding the current one or '\0' if
 *   there was no prior character.
 * \param[in, out] current Pointer to the current position in a source string
 *   being escaped. The pointer is updated based on how many characters are
 *   consumed.
 * \param flags Options for configuring behavior.
 * \param cb User function for emitting escaped data. This is expected to take a
 *   caller-defined state type as the first parameter and the string to emit as
 *   the second, and then return an opaque value that is passed back to the
 *   caller.
 * \param state Data to pass as the first parameter when calling `cb`.
 * \return The return value of a call to `cb`.
 */
static int xml_core(char previous, const char **current, xml_flags_t flags,
                    int (*cb)(void *state, const char *s), void *state) {

  const char *s = *current;
  char c = *s;

  // we will consume at least one character, so note that now
  ++*current;

  // escape '&' only if not part of a legal entity sequence
  if (c == '&' && (flags.raw || !xml_isentity(s)))
    return cb(state, "&amp;");

  // '<' '>' are safe to substitute even if string is already UTF-8 coded since
  // UTF-8 strings won't contain '<' or '>'
  if (c == '<')
    return cb(state, "&lt;");

  if (c == '>')
    return cb(state, "&gt;");

  // '-' cannot be used in XML comment strings
  if (c == '-' && flags.dash)
    return cb(state, "&#45;");

  if (c == ' ' && previous == ' ' && flags.nbsp)
    // substitute 2nd and subsequent spaces with required_spaces
    return cb(state, "&#160;"); // Inkscape does not recognize &nbsp;

  if (c == '"')
    return cb(state, "&quot;");

  if (c == '\'')
    return cb(state, "&#39;");

  if (c == '\n' && flags.raw)
    return cb(state, "&#10;");

  if (c == '\r' && flags.raw)
    return cb(state, "&#13;");

  unsigned char uc = (unsigned char)c;
  if (uc > 0x7f && flags.utf8) {

    // replicating a table from https://en.wikipedia.org/wiki/UTF-8:
    //
    //   ┌────────────────┬───────────────┬────────┬────────┬────────┬────────┐
    //   │First code point│Last code point│Byte 1  │Byte 2  │Byte 3  │Byte 4  │
    //   ├────────────────┼───────────────┼────────┼────────┼────────┼────────┤
    //   │          U+0000│         U+007F│0xxxxxxx│        │        │        │
    //   │          U+0080│         U+07FF│110xxxxx│10xxxxxx│        │        │
    //   │          U+0800│         U+FFFF│1110xxxx│10xxxxxx│10xxxxxx│        │
    //   │         U+10000│       U+10FFFF│11110xxx│10xxxxxx│10xxxxxx│10xxxxxx│
    //   └────────────────┴───────────────┴────────┴────────┴────────┴────────┘
    //
    // from which we can calculate the byte length of the current character
    size_t length =
        (uc >> 5) == 6 ? 2 : (uc >> 4) == 14 ? 3 : (uc >> 3) == 30 ? 4 : 0;

    // was the length malformed or is the follow on sequence truncated?
    bool is_invalid = length == 0;
    for (size_t l = 1; !is_invalid && length > l; ++l)
      is_invalid |= s[l] == '\0';

    // TODO: a better strategy than aborting on malformed data
    if (is_invalid) {
      fprintf(stderr, "Error during conversion to \"UTF-8\". Quiting.\n");
      graphviz_exit(EXIT_FAILURE);
    }

    // Decode the character. Refer again to the above table to understand this
    // algorithm.
    uint32_t utf8_char = 0;
    switch (length) {
    case 2: {
      uint32_t low = ((uint32_t)s[1]) & ((1 << 6) - 1);
      uint32_t high = ((uint32_t)s[0]) & ((1 << 5) - 1);
      utf8_char = low | (high << 6);
      break;
    }
    case 3: {
      uint32_t low = ((uint32_t)s[2]) & ((1 << 6) - 1);
      uint32_t mid = ((uint32_t)s[1]) & ((1 << 6) - 1);
      uint32_t high = ((uint32_t)s[0]) & ((1 << 4) - 1);
      utf8_char = low | (mid << 6) | (high << 12);
      break;
    }
    case 4: {
      uint32_t low = ((uint32_t)s[3]) & ((1 << 6) - 1);
      uint32_t mid1 = ((uint32_t)s[2]) & ((1 << 6) - 1);
      uint32_t mid2 = ((uint32_t)s[1]) & ((1 << 6) - 1);
      uint32_t high = ((uint32_t)s[0]) & ((1 << 3) - 1);
      utf8_char = low | (mid1 << 6) | (mid2 << 12) | (high << 18);
      break;
    }
    default:
      UNREACHABLE();
    }

    // setup a buffer that will fit the largest escape we need to print
    char buffer[sizeof("&#xFFFFFFFF;")];

    // emit the escape sequence itself
    snprintf(buffer, sizeof(buffer), "&#x%" PRIx32 ";", utf8_char);

    // note how many extra characters we consumed
    *current += length - 1;

    return cb(state, buffer);
  }

  // otherwise, output the character as-is
  char buffer[2] = {c, '\0'};
  return cb(state, buffer);
}

int xml_escape(const char *s, xml_flags_t flags,
               int (*cb)(void *state, const char *s), void *state) {
  char previous = '\0';
  int rc = 0;
  while (*s != '\0') {
    char p = *s;
    rc = xml_core(previous, &s, flags, cb, state);
    if (rc < 0)
      return rc;
    previous = p;
  }
  return rc;
}

#ifdef TEST_XML
// compile the below test stub with:
//
//   ${CC} -std=c99 -DTEST_XML -Ilib -Ilib/gvc -Ilib/pathplan -Ilib/cgraph
//     -Ilib/cdt lib/common/xml.c

static int put(void *stream, const char *s) { return fputs(s, stream); }

// stub for testing above functionality
int main(int argc, char **argv) {

  xml_flags_t flags = {0};

  while (true) {
    static const struct option opts[] = {
        {"dash", no_argument, 0, 'd'},
        {"nbsp", no_argument, 0, 'n'},
        {"raw", no_argument, 0, 'r'},
        {"utf8", no_argument, 0, 'u'},
        {0, 0, 0, 0},
    };

    int index;
    int c = getopt_long(argc, argv, "dnru", opts, &index);

    if (c == -1)
      break;

    switch (c) {

    case 'd':
      flags.dash = 1;
      break;

    case 'n':
      flags.nbsp = 1;
      break;

    case 'r':
      flags.raw = 1;
      break;

    case 'u':
      flags.utf8 = 1;
      break;

    default:
      fprintf(stderr, "unexpected error\n");
      graphviz_exit(EXIT_FAILURE);
    }
  }

  // escape all input we received
  for (int i = optind; i < argc; ++i) {
    int r = xml_escape(argv[i], flags, put, stdout);
    if (r < 0)
      graphviz_exit(EXIT_FAILURE);
  }

  graphviz_exit(EXIT_SUCCESS);
}
#endif
