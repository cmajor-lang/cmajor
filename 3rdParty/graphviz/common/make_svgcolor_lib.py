#!/usr/bin/python3

"""
svgcolor_names → svgcolor_lib generator
"""

import argparse
import re
import sys
from typing import List

def main(args: List[str]) -> int:
  """entry point"""

  # parse command line arguments
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("input", type=argparse.FileType("rt"),
                      help="input SVG data")
  parser.add_argument("output", type=argparse.FileType("wt"),
                      help="output color table entries")
  options = parser.parse_args(args[1:])

  for line in options.input:

    # skip comments and empty lines
    if line.startswith("#") or line.strip() == "":
      continue

    # split the line into columns
    items = re.split(r"[ \t,()]+", line)
    assert len(items) == 8, f"unexpected line {line}"

    # write this as a color table entry
    options.output.write(f"/svg/{items[0]} {items[4]} {items[5]} {items[6]} 255\n")

  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv))
