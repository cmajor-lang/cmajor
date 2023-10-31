#!/usr/bin/env python3

import sys, shutil, os, getopt, json, math

patchTemplateDir = os.path.dirname(os.path.abspath(sys.argv[0])) + "/patchTemplate"

elementType = "float"
elementSuffix = "f"

nodes = []
ioBlock = []
connections = []
initialisers = []
layerIds = []

nextLayerId = 0

nodeNamesMap = {
    "activation"             : "rt::layer::Activation",
    "dense"                  : "rt::layer::Dense",
    "conv1d"                 : "rt::layer::Conv1d",
    "gru"                    : "rt::layer::Gru",
    "prelu"                  : "rt::layer::Prelu",
    "batchnorm"              : "rt::layer::BatchNorm1d",
    "time-distributed-dense" : "rt::layer::Dense",
    "lstm"                   : "rt::layer::Lstm",

    "conv2d"                 : "rt::layer::Conv2d",
    "batchnorm2d"            : "rt::layer::BatchNorm2d",
}

def getNextLayerId():
    global nextLayerId
    global layerIds

    nextLayerId = nextLayerId + 1

    layerId = f"l{nextLayerId}"
    layerIds.append (layerId)
    return layerId


def parseShapeSize (shape):

    if len (shape) == 4:
        return shape[2] * shape[3]

    return shape[-1]

def addOptionalActivationLayer (size, layer):
    activationFn = layer["activation"]

    if activationFn == "":
        return

    nodeName = nodeNamesMap["activation"]

    nodes.append (f"        {getNextLayerId()} = {nodeName} ({size}, rt::ActivationFunction::{activationFn});")

def createCmajArrayType (data, elementType):
    dimensions = []
    nonUnitDimension = False

    while isinstance (data, list):
        nonUnitDimension = nonUnitDimension or len (data) > 1
        dimensions.append (str (len (data)))
        data = data[0]

    if len (dimensions) == 0:
        return elementType

    if not nonUnitDimension:
        return elementType

    dimensionString = ",".join (dimensions)

    return f"{elementType}[{dimensionString}]"


def createCmajArray (data, elementSuffix):

    if not isinstance (data, list):
        return str (float (data)) + elementSuffix

    items = []

    for i in data:
        items.append (createCmajArray (i, elementSuffix))

    return "(" + ", ".join (items) + ")"

def parseActivationFn (layer):
    if layer["activation"] == "":
        return "none"

    return layer["activation"]

def parseInitialiser (name, data):
    cmajType = createCmajArrayType (data, elementType)
    dataString = createCmajArray (data, elementSuffix)
    initialisers.append (f"    let {name} = {cmajType} {dataString};")

def computeNumFeaturesOut (numFeaturesIn, kernelSize, stride, validPad):
    if validPad == "valid":
        return int (math.ceil (numFeaturesIn - kernelSize + 1) / stride)

    return int (math.ceil (numFeaturesIn / stride))

def computePadLeft (numFeaturesIn, kernelSize, stride, validPad):
    if validPad == "valid":
        return 0

    if numFeaturesIn % stride == 0:
        return int (max (kernelSize - stride, 0) / 2)

    return int (max (kernelSize - (numFeaturesIn % stride), 0) / 2)

def computePadRight (numFeaturesIn, kernelSize, stride, validPad):
    if validPad == "valid":
        return 0

    totalPad = 0
    if numFeaturesIn % stride == 0:
        totalPad = max (kernelSize - stride, 0)
    else:
        totalPad = max (kernelSize - numFeaturesIn % stride, 0)

    return int (totalPad - totalPad / 2)


def parseLayer (inputSize, layer):
    global nextLayerId

    outputSize = parseShapeSize (layer["shape"])
    layerType = layer["type"]

    nodeName = nodeNamesMap[layerType]

    match layerType:
        case "dense":
            layerName = getNextLayerId()
            parseInitialiser (f"{layerName}Weights", layer["weights"][0])
            parseInitialiser (f"{layerName}Biases", layer["weights"][1])

            nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {outputSize}, {layerName}Weights, {layerName}Biases);")
            addOptionalActivationLayer (outputSize, layer)

        case "time-distributed-dense":
            layerName = getNextLayerId()
            parseInitialiser (f"{layerName}Weights", layer["weights"][0])
            parseInitialiser (f"{layerName}Biases", layer["weights"][1])

            nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {outputSize}, {layerName}Weights, {layerName}Biases);")
            addOptionalActivationLayer (outputSize, layer)

        case "conv1d":
            layerName = getNextLayerId()
            kernelSize = layer["kernel_size"][0]
            dilation = layer["dilation"][0]
            parseInitialiser (f"{layerName}Weights", layer["weights"][0])
            parseInitialiser (f"{layerName}Biases", layer["weights"][1])

            nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {outputSize}, {kernelSize}, {dilation}, {layerName}Weights, {layerName}Biases);")
            addOptionalActivationLayer (outputSize, layer)

        case "gru":
            layerName = getNextLayerId()
            parseInitialiser (f"{layerName}W", layer["weights"][0])
            parseInitialiser (f"{layerName}U", layer["weights"][1])
            parseInitialiser (f"{layerName}B", layer["weights"][2])
            activationFn = parseActivationFn (layer)

            nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {outputSize}, rt::ActivationFunction::{activationFn}, {layerName}W, {layerName}U, {layerName}B);")

        case "prelu":
            layerName = getNextLayerId()
            parseInitialiser (f"{layerName}Weights", layer["weights"][0])

            nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {layerName}Weights);")
            addOptionalActivationLayer (outputSize, layer)

        case "batchnorm":
            layerName = getNextLayerId()
            epsilon = layer["epsilon"]

            if len (layer["weights"]) == 2:
                parseInitialiser (f"{layerName}RunningMean", layer["weights"][0])
                parseInitialiser (f"{layerName}RunningVariance", layer["weights"][1])
                nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {epsilon}f, {layerName}RunningMean, {layerName}RunningVariance);")
            else:
                parseInitialiser (f"{layerName}Gamma", layer["weights"][0])
                parseInitialiser (f"{layerName}Beta", layer["weights"][1])
                parseInitialiser (f"{layerName}RunningMean", layer["weights"][2])
                parseInitialiser (f"{layerName}RunningVariance", layer["weights"][3])
                nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {epsilon}f, {layerName}RunningMean, {layerName}RunningVariance, {layerName}Gamma, {layerName}Beta);")

            addOptionalActivationLayer (outputSize, layer)

        case "lstm":
            layerName = getNextLayerId()
            parseInitialiser (f"{layerName}W", layer["weights"][0])
            parseInitialiser (f"{layerName}U", layer["weights"][1])
            parseInitialiser (f"{layerName}B", layer["weights"][2])
            activationFn = parseActivationFn (layer)
            nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {outputSize}, rt::ActivationFunction::{activationFn}, {layerName}W, {layerName}U, {layerName}B);")

        case "conv2d":
            layerName = getNextLayerId()
            numFiltersIn = layer["num_filters_in"]
            numFiltersOut = layer["num_filters_out"]
            numFeaturesIn = layer["num_features_in"]
            kernelSizeTime = layer["kernel_size_time"]
            kernelSizeFeature = layer["kernel_size_feature"]
            dilation = layer["dilation"]
            strides = layer["strides"]
            padding = "true" if (layer["padding"] == "valid") else "false"
            numFeaturesOut = computeNumFeaturesOut (numFeaturesIn, kernelSizeFeature, strides, layer["padding"])
            padLeft = computePadLeft (numFeaturesIn, kernelSizeFeature, strides, layer["padding"])
            padRight = computePadRight (numFeaturesIn, kernelSizeFeature, strides, layer["padding"])

            parseInitialiser (f"{layerName}Weights", layer["weights"][0])
            parseInitialiser (f"{layerName}Biases", layer["weights"][1])

            nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {outputSize}, {numFiltersIn}, {numFiltersOut}, {numFeaturesIn}, {numFeaturesOut}, {kernelSizeTime}, {kernelSizeFeature}, {dilation}, {strides}, {padding}, {padLeft}, {padRight}, {layerName}Weights, {layerName}Biases);")
            addOptionalActivationLayer (outputSize, layer)

        case "batchnorm2d":
            layerName = getNextLayerId()
            epsilon = layer["epsilon"]
            numFiltersIn = layer["num_filters_in"]
            numFeaturesIn = layer["num_features_in"]

            if len (layer["weights"]) == 2:
                parseInitialiser (f"{layerName}RunningMean", layer["weights"][0])
                parseInitialiser (f"{layerName}RunningVariance", layer["weights"][1])
                nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {epsilon}f, {numFiltersIn}, {numFeaturesIn}, {layerName}RunningMean, {layerName}RunningVariance);")
            else:
                parseInitialiser (f"{layerName}Gamma", layer["weights"][0])
                parseInitialiser (f"{layerName}Beta", layer["weights"][1])
                parseInitialiser (f"{layerName}RunningMean", layer["weights"][2])
                parseInitialiser (f"{layerName}RunningVariance", layer["weights"][3])
                nodes.append (f"        {layerName} = {nodeName} ({inputSize}, {epsilon}f, {numFiltersIn}, {numFeaturesIn}, {layerName}RunningMean, {layerName}RunningVariance, {layerName}Gamma, {layerName}Beta);")

            addOptionalActivationLayer (outputSize, layer)

        case "activation":
            addOptionalActivationLayer (outputSize, layer)

        case _:
            print (layerType)
            assert (False)

    return outputSize


def generateNodesAndConnections (model):
    global ioBlock

    size = parseShapeSize (model["in_shape"])
    ioBlock.append (f"    input stream {elementType}<{size}> in;")

    for layer in model["layers"]:
        size = parseLayer (size, layer)

    ioBlock.append (f"    output stream {elementType}<{size}> out;")
    connections.append ("        in -> " + " -> ".join (layerIds) + " -> out;")

    return

def replacePlaceholders (str, name):
    return str.replace("{NODES}", "\n".join (nodes)) \
              .replace("{NAME}", name) \
              .replace("{ELEMENT_TYPE}", elementType) \
              .replace("{IOBLOCK}", "\n".join (ioBlock)) \
              .replace("{CONNECTIONS}", "\n".join (connections)) \
              .replace("{INITIALISERS}", "\n".join (initialisers))


def printCmajor (name, patchDir):

    with open (patchTemplateDir + "/model.cmajor", "r") as f:
        cmajBody = replacePlaceholders (f.read(), name)

    with open (patchTemplateDir + "/model.cmajorpatch", "r") as f:
        patchBody = replacePlaceholders (f.read(), name)

    if patchDir == None:
        print (cmajBody)
    else:

        if not os.path.exists (patchDir):
            shutil.copytree (patchTemplateDir, patchDir)
        else:
            with open (patchTemplateDir + "/rtneural.cmajor", "r") as f:
                f2 = open (patchDir + "/rtneural.cmajor", "w")
                f2.write (f.read())

        with open (patchDir + "/model.cmajor", "w") as f:
            f.write (cmajBody)
            f.close()

        with open (patchDir + "/model.cmajorpatch", "w") as f:
            f.write (patchBody)
            f.close()


def usage():
    print (
"""

  This script generates Cmajor code from an RTNeural model.

  Options:
    -h                   Display this help
    --model <file>       Specifies the RTNeural model file to convert
    --outputDir <folder> Specifies a folder into which the generated patch will be written
    --name <name>        Optional name for the model - defaults to "Model"
    --useFloat64         By default we use float32, but this changes the generated model to float64

  If no patchDir argument is supplied, the output will be a single Cmajor file.

""")


def main(argv):

    global elementType, elementSuffix

    if len(argv) == 0:
        usage()
        exit(1)

    try:
        opts, args = getopt.getopt (argv, "h", ["outputDir=", "model=", "name=", "useFloat64"])
    except getopt.GetoptError as e:
        print (f"{e}")
        usage()
        exit (1)

    patchDir = None
    model = None
    name = "Model"

    for opt, arg in opts:
        if opt in ("-h"):
            usage()
            exit (1)

        if opt in ("--outputDir"):
            patchDir = arg

        if opt in ("--model"):
            model = arg

        if opt in ("--name"):
            name = arg

        if opt in ("useFloat64"):
            print ("Switching to float64")
            elementType = "float64"
            elementSuffix = ""

    if model == None:
        model = args[0]

    rtneuralModel = json.load (open (model))

    generateNodesAndConnections (rtneuralModel)

    printCmajor (name, patchDir)

if __name__== "__main__" :
    main (sys.argv[1:])
    exit()