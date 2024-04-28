#!/usr/bin/env python3

import os
import sys

def pull (folder):
    print (folder)
    os.chdir (folder)
    os.system ("git checkout master")
    os.system ("git pull")

boostFolder = os.path.dirname (os.path.realpath (__file__))

pull (os.path.join (boostFolder, "align"))
pull (os.path.join (boostFolder, "asio"))
pull (os.path.join (boostFolder, "assert"))
pull (os.path.join (boostFolder, "beast"))
pull (os.path.join (boostFolder, "bind"))
pull (os.path.join (boostFolder, "config"))
pull (os.path.join (boostFolder, "core"))
pull (os.path.join (boostFolder, "date_time"))
pull (os.path.join (boostFolder, "endian"))
pull (os.path.join (boostFolder, "intrusive"))
pull (os.path.join (boostFolder, "io"))
pull (os.path.join (boostFolder, "logic"))
pull (os.path.join (boostFolder, "move"))
pull (os.path.join (boostFolder, "mp11"))
pull (os.path.join (boostFolder, "mpl"))
pull (os.path.join (boostFolder, "numeric_conversion"))
pull (os.path.join (boostFolder, "optional"))
pull (os.path.join (boostFolder, "predef"))
pull (os.path.join (boostFolder, "preprocessor"))
pull (os.path.join (boostFolder, "regex"))
pull (os.path.join (boostFolder, "smart_ptr"))
pull (os.path.join (boostFolder, "static_assert"))
pull (os.path.join (boostFolder, "static_string"))
pull (os.path.join (boostFolder, "system"))
pull (os.path.join (boostFolder, "throw_exception"))
pull (os.path.join (boostFolder, "type_traits"))
pull (os.path.join (boostFolder, "utility"))
pull (os.path.join (boostFolder, "winapi"))
