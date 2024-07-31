#!/usr/bin/env python3

import sys
sys.dont_write_bytecode = True

import os
import argparse
import shutil

# ==============================================================================
def checkSysCall(call):
    result = os.system(call)

    if result != 0:
        sys.exit(result)

def readFile (file):
    with open(file, 'r') as f:
        return f.read()

def writeFile (file, content):
    with open(file, 'w') as f:
        f.write (content)


# ==============================================================================
parser = argparse.ArgumentParser (description = "This script builds the wasm compiler and puts everything needed into a target folder")
parser.add_argument ("--target", type = str, help = "The path to the target folder to put the results", required = True)
args = parser.parse_args()

scriptFolder = os.path.dirname (os.path.realpath(__file__))
buildFolder = os.path.join (scriptFolder, "build")

checkSysCall ("rm -rf " + buildFolder)
checkSysCall (f'emcmake cmake -DCMAKE_BUILD_TYPE=Release -S' + scriptFolder + " -B " + buildFolder + " -GNinja")
checkSysCall ("cd " + buildFolder + " && ninja")

from rjsmin import jsmin
emscriptenWrapper = os.path.join (buildFolder, "cmaj-compiler-wasm.js")
writeFile (emscriptenWrapper, jsmin (readFile (emscriptenWrapper)))

targetFolder = args.target
checkSysCall ("rm -rf " + targetFolder)

shutil.copytree (os.path.join (scriptFolder, "../../javascript/cmaj_api"), targetFolder, symlinks=False, ignore=None, dirs_exist_ok=True)
shutil.copy (os.path.join (scriptFolder, "cmaj-embedded-compiler.js"), targetFolder)
shutil.copy (os.path.join (scriptFolder, "embedded-compiler-demo.html"), targetFolder)
shutil.copy (os.path.join (buildFolder, "cmaj-compiler-wasm.js"), targetFolder)
shutil.copy (os.path.join (buildFolder, "cmaj-compiler-wasm.wasm"), targetFolder)
