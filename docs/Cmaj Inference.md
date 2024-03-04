# Machine Learning Tools in Cmajor

Cmajor makes a great platform for high-performance execution (inference) of real-time machine learning tasks. ML inference involves a lot of matrix operators, and the Cmajor JIT engine can do a great job of vectorising that kind of code. In most cases it equals or outperforms equivalent C++ code by taking advantage of processor-specific SIMD extensions where available, and is real-time safe by design.

Two commonly used ML model formats for audio tasks are [ONNX](https://onnx.ai/) and [RTNeural](https://github.com/jatinchowdhury18/RTNeural), and we provide tools to convert these models directly into pure Cmajor code.

Other ML frameworks such as Pytorch or Tensorflow provide conversion tools to/from ONNX, so ONNX can be used as an intermediate format when converting other models to Cmajor.

----------------------------------------------------------

## ONNX

The [ONNX](https://onnx.ai/) Intermediate Representation (IR) is a common format used for exchanging ML models. An ONNX IR file includes not only the model's architecture but also its weights and other metadata.

### ONNX Operators

ONNX operators provide a standardised way to represent these operations, regardless of the deep learning framework used to define or train the model. This means that if you have a neural network model built in one framework (e.g. PyTorch or TensorFlow) and you want to run it on a different platform or framework, you can use ONNX to convert and execute the model seamlessly with Cmajor.

### Using the ONNX to Cmajor Python script

You can run `cmajor/tools/onnx/onnxToCmajor.py` to do the conversion. Command-line options are:

```
    --model <file>       Specifies the ONNX model file to convert
    --patchDir <folder>  Specifies a folder into which the generated patch will be written

  If no patchDir argument is supplied, the output will be a single Cmajor file.
```

To run this script, options are given as:

```shell
onnxToCmajor.py --model "path/to/your/model.onnx" --patchDir "path/to/output/patch"
```

This will generate the following files in the given directory:

- `model.cmajor` - Contains all the nodes, connections and weights to describe the model

- `onnx.cmajor` - Contains definitions of the ONNX operators required by the model

- `model.cmajorpatch` - As with all cmajor manifest files, this describes the patch's properties and contains links to the other files in the patch

### Currently supported ONNX Operators:

The ONNX format contains a huge number of operators. Cmajor supports a subset of the more commonly used ones, which will grow over time:

`Tanh`, `Reshape`, `MatMul`, `Gemm`, `Add`, `Unsqueeze`, `Squeeze`, `Conv`, `GRU`, `Pad`, `Transpose`, `Constant`, `LSTM`, `Slice`, `Gather`, `Concat`

-------------------------------------------------------------------

## RTNeural

[RTNeural](https://github.com/jatinchowdhury18/RTNeural) is a C++ ML framework aimed at more real-time tasks.

You can run `cmajor/tools/rtneural/rtneuralToCmajor.py` to perform the conversion, with arguments:

```
    --model <file>       Specifies the RTNeural model file to convert
    --patchDir <folder>  Specifies a folder into which the generated patch will be written
    --name <name>        Optional name for the model - defaults to "Model"
    --useFloat64         By default we use float32, but this changes the generated model to float64

  If no patchDir argument is supplied, the output will be a single Cmajor file.
```

e.g.

```shell
rtneuralToCmajor.py --model "path/to/your/model.json" --patchDir "path/to/output/patch"
```

### Supported Operators

We currently support the following RTNeural features:

- Layers: `Dense`, `conv1d`, `conv2d`, `GRU`, `PReLU`, `BatchNorm1D`, `BatchNorm2D`, `LSTM`

- Activations: `tanh`, `ReLU`, `Sigmoid`, `Softmax`, `ELu`, `PReLU`