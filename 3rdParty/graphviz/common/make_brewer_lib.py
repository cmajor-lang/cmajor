#!/usr/bin/python3

"""
brewer_colors → brewer_lib generator
"""

import argparse
import sys
from typing import List

def main(args: List[str]) -> int:
  """entry point"""

  # parse command line arguments
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("input", type=argparse.FileType("rt"),
                      help="input Brewer CSV data")
  parser.add_argument("output", type=argparse.FileType("wt"),
                      help="output color table entries")
  options = parser.parse_args(args[1:])

  name = None

  for line in options.input:

    # skip comments and empty lines
    if line.startswith("#") or line.strip() == "":
      continue

    # split the line into columns
    items = line.split(",")
    assert len(items) == 10, f"unexpected line {line}"

    # do we have a new name on this line?
    if items[0] != "":
      # derive the name from the first two columns
      name = "".join(items[:2]).replace('"', "")

    assert name is not None, "first line contained no name"

    # write this as a color table entry
    options.output.write(f"/{name}/{items[4]} {items[6]} {items[7]} {items[8]} 255\n")

  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv))
