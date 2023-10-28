#!/usr/bin/env python3

import sys, shutil, os, getopt, onnx
from onnx import numpy_helper, shape_inference

patchTemplateDir = os.path.dirname(os.path.abspath(sys.argv[0])) + "/patchTemplate"

dimParamFrames = 1
endpointSuffix = "_Transformer"

ioBlock = []
nodes = []
connections = []
initialisers = []
endpoints = {}
connectionDataTypes = {}
outputNames = []

nodeEndpointNames = {
    "Tanh" :        [["in"],                                                                                    ["out"]],
    "Reshape" :     [["in", ""],                                                                                ["out"]],
    "MatMul" :      [["in", "mul"],                                                                             ["out"]],
    "Gemm" :        [["in", "mul", "add"],                                                                      ["out"]],
    "Add" :         [["inA", "inB"],                                                                            ["out"]],
    "Unsqueeze" :   [["in", ""],                                                                                ["out"]],
    "Squeeze" :     [["in", ""],                                                                                ["out"]],
    "Conv" :        [["in", "inWeights"],                                                                       ["out"]],
    "GRU" :         [["in", "inWVals", "inRVals", "inBVals", "seq", "inInitial"],                               ["out"]],
    "Pad" :         [["in", "pads"],                                                                            ["out"]],
    "Transpose" :   [["in"],                                                                                    ["out"]],
    "Constant":     [[],                                                                                        ["out"]],
    "LSTM":         [["inX", "inW", "inR", "inB", "inSequenceLens", "initialH", "initialC", "inP"],             ["outY", "outY_h", "outY_c"]],
    "Slice" :       [["data", "starts", "ends", "axes", "steps"],                                               ["out"]],
    "Gather" :      [["in", "indices"],                                                                         ["out"]],
    "Concat" :      [["in1", "in2", "in3"],                                                                     ["out"]],
}

def getConnectionDataType (name):

    if name in connectionDataTypes:
        return connectionDataTypes[name][0]

    assert (False)

def getConnectionDataTypeDimensions (name):
    if name in connectionDataTypes:
        return connectionDataTypes[name][1]

    assert (False)

def getConnectionDataTypeTotalElements (name):
    if name in connectionDataTypes:
        return connectionDataTypes[name][2]

    assert (False)

def getDataType (dataType: int):

    dataTypes = {
        onnx.TensorProto.FLOAT:     ["float32", "f"],
        onnx.TensorProto.DOUBLE:    ["float64", ""],
        onnx.TensorProto.INT64:     ["int64",   "i64"],
        onnx.TensorProto.INT32:     ["int32",   ""],
        onnx.TensorProto.BOOL:      ["bool",    ""]
    }

    if dataType in dataTypes:
        return dataTypes[dataType]

    assert (False)

def createCmajArray (dimensions, array, elementSuffix):

    if len(dimensions) == 0:
        return str (array) + elementSuffix

    if len(dimensions) == 1:
        items = []

        for i in array.tolist():
            items.append (str (i) + elementSuffix)

        return "(" + ", ".join (items) + ")"

    items = []
    innerDims = dimensions[1:]

    for i in range (dimensions[0]):
        items.append (createCmajArray (innerDims, array[i], elementSuffix))

    return "(" + ", ".join (items) + ")"

def getEndpointInputName (opType, id):
    if opType in nodeEndpointNames:
        nodeEndpointInputNameList = nodeEndpointNames[opType][0]
        if id < len (nodeEndpointInputNameList):
            return nodeEndpointInputNameList[id]

    return "in" + str (id)

def getEndpointOutputName (opType, id):
    if opType in nodeEndpointNames:
        nodeEndpointInputNameList = nodeEndpointNames[opType][1]
        if id < len (nodeEndpointInputNameList):
            return nodeEndpointInputNameList[id]

    return "out" + str (id)

def sanitiseName (s):
    return s.replace('/','_').replace(":", "_").replace(".", "_").lstrip ("_")

def populateNodeOutputs (model: onnx.ModelProto):
    for op in model.graph.node:
        nodeName = sanitiseName (op.name)
        for index, outputName in enumerate (op.output):
            endpoints[sanitiseName (outputName)] = nodeName + "." + getEndpointOutputName (op.op_type, index)

    initialisers = []

    for initialiser in model.graph.initializer:
        initialisers.append (initialiser.name)
        endpoints[sanitiseName (initialiser.name)] = sanitiseName (initialiser.name) + ".out"
        addInitialiser (initialiser)

    for input in model.graph.input:
        if input.name not in initialisers:
            endpoints[sanitiseName (input.name)] = sanitiseName (input.name) + endpointSuffix

    for output in model.graph.output:
        endpoints[sanitiseName (output.name)] = sanitiseName (output.name)

def addInitialiser (initialiser):
    nodeName = sanitiseName (initialiser.name)
    nodes.append ("        " + nodeName + " = onnx::operation::Constant (" + nodeName + "_.type, " + nodeName + "_);")

    dimensions = ','.join(map(str, initialiser.dims))
    dataType, elementSuffix = getDataType (initialiser.data_type)
    array = numpy_helper.to_array(initialiser)
    dataArray = str(createCmajArray (initialiser.dims, array, elementSuffix)).replace ('[','(').replace(']',')')
    initialisers.append ("    let " + nodeName + "_ = " + dataType + "[" + dimensions + "] " + dataArray + ";")

def createConstantValue (constantName, attribute):

    dataType, elementSuffix = getDataType (attribute.t.data_type)
    dimensions = ','.join(map(str, attribute.t.dims))
    array = numpy_helper.to_array(attribute.t)
    dataArray = str(createCmajArray (attribute.t.dims, array, elementSuffix)).replace ('[','(').replace(']',')')

    if len (dimensions) == 0:
        initialisers.append ("    let " + constantName + "_ = " + dataType + " (" + dataArray + ");")
    else:
        initialisers.append ("    let " + constantName + "_ = " + dataType + "[" + dimensions + "] " + dataArray + ";")

    return constantName + "_"

def getNodeAttribute (op: onnx.NodeProto, name):
    for attribute in op.attribute:
        if attribute.name == name:
            return attribute

def getNodeAttributeValue (op: onnx.NodeProto, name):

    for attribute in op.attribute:
        if attribute.name == name:
            if attribute.type == onnx.AttributeProto.INT:
                return str (attribute.i)

            if attribute.type == onnx.AttributeProto.INTS:

                intList = []
                for i in attribute.ints:
                    intList.append (i)

                return "int[" + str (len (intList)) + "] (" + ", ".join(map(str, intList)) + ")"

def getOperatorParameters (op: onnx.NodeProto):
    outputDataType = getConnectionDataType (op.output[0])

    if op.op_type == "Constant":
        constantName = createConstantValue (sanitiseName (op.name), getNodeAttribute (op, "value"))
        return " (" + outputDataType + ", " + constantName + ")"

    inputDataType = getConnectionDataType (op.input[0])

    if op.op_type == "Add":
        return " (" + inputDataType + ")"

    if op.op_type == "Tanh":
        return " (" + inputDataType + ")"

    if op.op_type == "Conv":
        dilations = getNodeAttributeValue (op, "dilations")
        kernelShape = getNodeAttributeValue (op, "kernel_shape")
        return " (" + inputDataType + ", " + outputDataType + ", " + str (kernelShape) + ", " + str (dilations) + ")"

    if op.op_type == "LSTM":
        hiddenSize = getNodeAttributeValue (op, "hidden_size")
        return " (" + inputDataType + ", " + outputDataType + ", " + str (hiddenSize) + ")"

    if op.op_type == "Transpose":
        perm = getNodeAttributeValue (op, "perm")
        totalElements = getConnectionDataTypeTotalElements (op.input[0])
        return " (" + inputDataType + ", " + outputDataType + ", " + str (totalElements) + ", " + perm + ")"

    if op.op_type == "MatMul":
        mulType = getConnectionDataType (op.input[1])
        return " (" + inputDataType + ", " + outputDataType + ", " + mulType + ")"

    if op.op_type == "Gemm":
        mulType = getConnectionDataType (op.input[1])
        return " (" + inputDataType + ", " + outputDataType + ", " + mulType + ")"

    if op.op_type == "Slice":
        axesType = "int[" + str (getConnectionDataTypeDimensions (op.input[0])) + "]"

        if (op.input[3]):
            axesType = getConnectionDataType (op.input[3])

        return " (" + inputDataType + ", " + outputDataType + ", " + axesType + ")"

    if op.op_type == "Gather":
        indicesType = getConnectionDataType (op.input[1])
        return " (" + inputDataType + ", " + outputDataType + ", " + indicesType + ")"

    if op.op_type == "Concat":
        axisValue = getNodeAttributeValue (op, "axis")
        return " (" + inputDataType + ", " + outputDataType + ", " + "3" + ", " + axisValue + ")"

    return " (" + inputDataType + ", " + outputDataType + ")"

def generateNode (op: onnx.NodeProto):
    global nodes
    global connections

    nodeName = sanitiseName (op.name)
    opType   = op.op_type
    nodes.append ("        " + nodeName + " = onnx::operation::" + opType + getOperatorParameters (op) + ";")

    for index, input in enumerate (op.input):
        if input != "":
            inputEndpoint = getEndpointInputName (opType, index)
            if inputEndpoint != "":
                connections.append ("        " + endpoints[sanitiseName (input)] + " -> " + nodeName + "." + inputEndpoint + ";")

    for index, output in enumerate (op.output):
        if output in outputNames:
            outputEndpoint = getEndpointOutputName (opType, index)
            connections.append ("        " + nodeName + "." + outputEndpoint + " -> " + output + endpointSuffix + ";")

def parseInitialiserType (initialiser):
        dataType, elementSuffix = getDataType (initialiser.data_type)
        dimensions = []
        totalElements = 1
        for dim in initialiser.dims:
            dimensions.append (dim)
            totalElements *= dim

        if len (dimensions) == 0:
            return dataType, 0, 1

        dimensionList = ','.join(map(str, dimensions))

        return dataType + "[" + dimensionList + "]", len (dimensions), totalElements

def parseTensorType (tensorType):
        dataType, elementSuffix = getDataType (tensorType.elem_type)
        dimensions = []
        totalElements = 1

        for dim in tensorType.shape.dim:
            if dim.dim_value:
                dimensions.append (dim.dim_value)
                totalElements *= dim.dim_value
            elif dim.dim_param:
                dimensions.append (dimParamFrames)
                totalElements *= dimParamFrames

        if len (dimensions) == 0:
            return dataType, 0, 1

        dimensionList = ','.join(map(str, dimensions))

        return dataType + "[" + dimensionList + "]", len (dimensions), totalElements

def parseTensorTypeToStreamType (tensorType):
        dataType, elementSuffix = getDataType (tensorType.elem_type)
        elements = 1

        for dim in tensorType.shape.dim:
            if dim.dim_value:
                elements *= dim.dim_value
            elif dim.dim_param:
                elements *= dimParamFrames

        return dataType + "<" + str (elements) + ">"

def populateDataTypes (model: onnx.ModelProto):

    for input in model.graph.input:
        connectionDataTypes[input.name] = parseTensorType (input.type.tensor_type)

    for output in model.graph.output:
        connectionDataTypes[output.name] = parseTensorType (output.type.tensor_type)

    for valueInfo in model.graph.value_info:
        name = valueInfo.name
        connectionDataTypes[name] = parseTensorType (valueInfo.type.tensor_type)

    for initialiser in model.graph.initializer:
        name = initialiser.name
        connectionDataTypes[name] = parseInitialiserType (initialiser)


def populateIO (model: onnx.ModelProto):
    global ioBlock
    global nodes
    global connections
    global outputNames

    initialisers = []

    for initialiser in model.graph.initializer:
        initialisers.append (initialiser.name)

    for input in model.graph.input:
        if input.name not in initialisers:
            streamType = parseTensorTypeToStreamType (input.type.tensor_type)
            tensorType = getConnectionDataType (input.name)

            ioBlock.append ("    input stream " + streamType + " " + input.name + ";")
            nodes.append ("        " + input.name + endpointSuffix + " = onnx::graphIn (" + streamType + ", " + tensorType + ");")
            connections.append ("        " + input.name + " -> " + input.name + endpointSuffix + ";")

    for output in model.graph.output:
        outputNames.append (output.name)
        streamType = parseTensorTypeToStreamType (output.type.tensor_type)
        tensorType = getConnectionDataType (output.name)

        ioBlock.append ("    output stream " + streamType + " " + output.name + ";")
        nodes.append ("        " + output.name + endpointSuffix + " = onnx::graphOut (" + tensorType + ", " + streamType + ");")
        connections.append ("        " + output.name + endpointSuffix + " -> " + output.name + ";")

    connections.append ("")

def populateMissingNodeNames (model: onnx.ModelProto):
    nextNode = 1

    for node in model.graph.node:
        if node.name == "":
            node.name = "node_" + str (nextNode)
            nextNode = nextNode + 1

def generateNodesAndConnections (model: onnx.ModelProto):
    populateMissingNodeNames (model)
    populateDataTypes (model)
    populateIO (model)
    populateNodeOutputs (model)

    for op in enumerate (model.graph.node):
        generateNode (op[1])


def printCmajor (patchDir):

    with open (patchTemplateDir + "/model.cmajor", "r") as f:
        mainGraphBody = f.read()

    cmaj = mainGraphBody.replace("NODES", "\n".join (nodes)) \
                        .replace("IOBLOCK", "\n".join (ioBlock)) \
                        .replace("CONNECTIONS", "\n".join (connections)) \
                        .replace("INITIALISERS", "\n".join (initialisers))

    if patchDir == None:
        print (cmaj)
    else:

        if not os.path.exists (patchDir):
            shutil.copytree (patchTemplateDir, patchDir)
        else:
            with open (patchTemplateDir + "/onnx.cmajor", "r") as f:
                f2 = open (patchDir + "/onnx.cmajor", "w")
                f2.write (f.read())
            with open (patchTemplateDir + "/model.cmajorpatch", "r") as f:
                f2 = open (patchDir + "/model.cmajorpatch", "w")
                f2.write (f.read())

        f = open (patchDir + "/model.cmajor", "w")
        f.write (cmaj)
        print ("Cmajor patch created: " + patchDir + "/model.cmajor")
        f.close()

def usage():
    print (
"""

    onnxToCmajor.py

    This script generates cmajor for the supplied ONNX model
    Options:

    -h                  Show the help
    --model [s]         Specifies the ONNX model to convert
    --patchDir [s]      Writes the output as a patch to the given directory

    If no patchDir is specified, a single cmajor file is output which depends on
    the Cmajor ml operators

""")

def main(argv):
    if len(argv) == 0:
        usage()
        exit(1)

    try:
        opts, args = getopt.getopt (argv, "h", ["patchDir=", "model="])
    except getopt.GetoptError as e:
        print (f"{e}")
        usage()
        exit (1)

    patchDir = None
    model = None

    for opt, arg in opts:
        if opt in ("-h"):
            usage()
            exit (1)

        if opt in ("--patchDir"):
            patchDir = arg

        if opt in ("--model"):
            model = arg

    if model == None:
        model = args[0]

    onnxModel = onnx.load (model)
    inferredModel = shape_inference.infer_shapes(onnxModel)

    try:
        onnx.checker.check_model (onnxModel)
    except onnx.checker.ValidationError as e:
        print (f"The model is invalid: {e}")
    else:
        generateNodesAndConnections (inferredModel)

    printCmajor (patchDir)

if __name__== "__main__" :
    main (sys.argv[1:])
    exit()
