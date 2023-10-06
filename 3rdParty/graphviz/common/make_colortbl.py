#!/usr/bin/python3

"""
(brewer_lib, svgcolor_lib, color_names) → colortbl.h generator
"""

import argparse
import locale
import re
import sys
from typing import List, Tuple

def rgb_to_hsv(r: int, g: int, b: int) -> Tuple[int, int, int]:
  """transform an RGB color into HSV format"""

  r /= 255.0
  g /= 255.0
  b /= 255.0

  max_ = max((r, g, b))
  min_ = min((r, g, b))

  v = max_

  if max_ != 0:
    s = (max_ - min_) / max_
  else:
    s = 0

  if s == 0:
    h = 0
  else:
    delta = max_ - min_
    rc = (max_ - r) / delta
    gc = (max_ - g) / delta
    bc = (max_ - b) / delta
    if r == max_:
      h = bc - gc
    elif g == max_:
      h = 2.0 + (rc - bc)
    else:
      h = 4.0 + (gc - rc)
    h *= 60.0
    if h < 0.0:
      h += 360.0

  return int(h / 360.0 * 255.0), int(s * 255.0), int(v * 255.0)

def main(args: List[str]) -> int:
  """entry point"""

  # avoid sorting issues due to locale differences
  locale.setlocale(locale.LC_ALL, "C")

  # parse command line arguments
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("brewer_lib", type=argparse.FileType("rt"),
                      help="input Brewer color table entries")
  parser.add_argument("svgcolor_lib", type=argparse.FileType("rt"),
                      help="input SVG color table entries")
  parser.add_argument("color_names", type=argparse.FileType("rt"),
                      help="input other color table entries")
  parser.add_argument("output", type=argparse.FileType("wt"),
                      help="output color table C header")
  options = parser.parse_args(args[1:])

  options.output.write("static hsvrgbacolor_t color_lib[] = {\n")

  # collect all color entries
  entries = options.brewer_lib.readlines() + options.svgcolor_lib.readlines() \
    + options.color_names.readlines()

  # sort these so `bsearch` for a color will work correctly
  entries = sorted(entries, key=locale.strxfrm)

  for entry in entries:

    # ignore blank lines
    if entry.strip() == "":
      continue

    # extract fields of the entry
    m = re.match(r"(?P<name>\S+) (?P<r>\d+) (?P<g>\d+) (?P<b>\d+) (?P<a>\d+)", entry)
    assert m is not None, f'unexpected entry format "{entry}"'

    # transform an RGB color into HSV
    r = int(m.group("r"))
    g = int(m.group("g"))
    b = int(m.group("b"))
    h, s, v = rgb_to_hsv(r, g, b)

    # write this out as a C array entry
    name = m.group("name")
    a = int(m.group("a"))
    options.output.write(f'{{"{name}",{h},{s},{v},{r},{g},{b},{a}}},\n')

  options.output.write("};\n")

  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv))
