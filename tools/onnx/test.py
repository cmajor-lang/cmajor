#!/usr/bin/env python3

#
#     ,ad888ba,                              88
#    d8"'    "8b
#   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
#   Y8,           88    88    88  88     88  88
#    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
#     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
#                                           ,88
#                                        888P"
#
#  The Cmajor project is subject to commercial or open-source licensing.
#  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
#  visit https://cmajor.dev to learn about our commercial licence options.
#
#  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
#  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
#  DISCLAIMED.

from typing import Any, Sequence, Union, List, Optional
import onnx
from onnx import (AttributeProto,    FunctionProto,    GraphProto,    ModelProto,    NodeProto,    TensorProto, TypeProto)
import onnxruntime
import numpy as np

def _extract_value_info(
    input: Union[List[Any], np.ndarray, None],
    name: str,
    type_proto: Optional[TypeProto] = None,
) -> onnx.ValueInfoProto:
    if type_proto is None:
        if input is None:
            raise NotImplementedError(
                "_extract_value_info: both input and type_proto arguments cannot be None."
            )
        elif isinstance(input, list):
            elem_type = onnx.helper.np_dtype_to_tensor_dtype(input[0].dtype)
            shape = None
            tensor_type_proto = onnx.helper.make_tensor_type_proto(elem_type, shape)
            type_proto = onnx.helper.make_sequence_type_proto(tensor_type_proto)
        elif isinstance(input, TensorProto):
            elem_type = input.data_type
            shape = tuple(input.dims)
            type_proto = onnx.helper.make_tensor_type_proto(elem_type, shape)
        else:
            elem_type = onnx.helper.np_dtype_to_tensor_dtype(input.dtype)
            shape = input.shape
            type_proto = onnx.helper.make_tensor_type_proto(elem_type, shape)

    return onnx.helper.make_value_info(name, type_proto)


def expect(
    node: onnx.NodeProto,
    inputs: Sequence[np.ndarray],
    outputs: Sequence[np.ndarray],
    name: str,
    **kwargs: Any,
) -> None:
    # Builds the model
    present_inputs = [x for x in node.input if (x != "")]
    present_outputs = [x for x in node.output if (x != "")]
    input_type_protos = [None] * len(inputs)
    if "input_type_protos" in kwargs:
        input_type_protos = kwargs["input_type_protos"]
        del kwargs["input_type_protos"]
    output_type_protos = [None] * len(outputs)
    if "output_type_protos" in kwargs:
        output_type_protos = kwargs["output_type_protos"]
        del kwargs["output_type_protos"]
    inputs_vi = [
        _extract_value_info(arr, arr_name, input_type)
        for arr, arr_name, input_type in zip(inputs, present_inputs, input_type_protos)
    ]
    outputs_vi = [
        _extract_value_info(arr, arr_name, output_type)
        for arr, arr_name, output_type in zip(
            outputs, present_outputs, output_type_protos
        )
    ]
    graph = onnx.helper.make_graph(
        nodes=[node], name=name, inputs=inputs_vi, outputs=outputs_vi
    )
    kwargs["producer_name"] = "backend-test"

    if "opset_imports" not in kwargs:
        # To make sure the model will be produced with the same opset_version after opset changes
        # By default, it uses since_version as opset_version for produced models
        produce_opset_version = onnx.defs.get_schema(
            node.op_type, domain=node.domain
        ).since_version
        kwargs["opset_imports"] = [
            onnx.helper.make_operatorsetid(node.domain, produce_opset_version)
        ]

    model = onnx.helper.make_model_gen_version(graph, **kwargs)

    # Checking the produces are the expected ones.
    sess = onnxruntime.InferenceSession(model.SerializeToString(),
                                        providers=["CPUExecutionProvider"])
    feeds = {name: value for name, value in zip(node.input, inputs)}
    results = sess.run(None, feeds)
    for expected, output in zip(outputs, results):
        print (output.shape)
        print (output)
        # np.testing.assert_allclose(expected, output)



# node = onnx.helper.make_node(
#     "Add",
#     inputs=["x", "y"],
#     outputs=["sum"],
# )

# x = np.random.randn(3, 4, 5).astype(np.float32)
# y = np.random.randn(3, 4, 5).astype(np.float32)
# expect(node, inputs=[x, y], outputs=[x - y], name="test_add")



x = np.array(
    [
        [
            [
                [1.0, 0.0, 0.0, 0.0, 0.0]
            ],
            [
                [0.0, 0.0, 0.0, 0.0, 0.0]
            ],
            [
                [0.0, 0.0, 0.0, 0.0, 0.0]
            ],
            [
                [0.0, 0.0, 0.0, 0.0, 0.0]
            ],
            [
                [0.0, 0.0, 0.0, 0.0, 0.0]
            ],
            [
                [0.0, 0.0, 0.0, 0.0, 0.0]
            ],
            [
                [0.0, 0.0, 0.0, 0.0, 0.0]
            ],
            [
                [0.0, 0.0, 0.0, 0.0, 0.0]
            ],
        ]
    ]
).astype(np.float32)


W = np.array(
    [
        [
            [[-0.24126437306404114, 0.3814312219619751, -0.12044018507003784]],
            [[-0.04274851083755493, -0.2839662432670593, -0.22701495885849]],
            [[-0.03819775581359863, -0.383661150932312, 0.2926709055900574]],
            [[-0.08654946088790894, -0.14390897750854492, -0.08310288190841675]],
            [[0.1248442530632019, 0.32284533977508545, 0.08825048804283142]],
            [[-0.24214640259742737, 0.11585944890975952, 0.2948625683784485]],
            [[-0.015756994485855103, -0.24117492139339447, 0.38654762506484985]],
            [[0.176719069480896, 0.2188374400138855, -0.37407004833221436]]
        ],
        [
            [[-0.031374335289001465, -0.035794854164123535, 0.12210100889205933]],
            [[0.26001787185668945, 0.3963600993156433, -0.005117237567901611]],
            [[0.17751890420913696, -0.16308872401714325, -0.208327516913414]],
            [[0.03579816222190857, -0.39630940556526184, 0.26233619451522827]],
            [[-0.004462867975234985, 0.2417028546333313, -0.02489122748374939]],
            [[-0.3775651454925537, 0.07815927267074585, -0.21973885595798492]],
            [[0.048407793045043945, -0.13970395922660828, 0.22788327932357788]],
            [[0.19025921821594238, 0.22861170768737793, -0.16930827498435974]]],
        [
            [[0.22665190696716309, 0.3551509380340576, 0.10703051090240479]],
            [[0.3507322072982788, 0.29436373710632324, -0.0871293842792511]],
            [[0.045223504304885864, -0.15356624126434326, 0.05609959363937378]],
            [[0.01850026845932007, -0.3690764307975769, 0.38323861360549927]],
            [[-0.04213705658912659, -0.40032142400741577, 0.09417814016342163]],
            [[-0.1498490571975708, 0.3246315121650696, -0.22800017893314362]],
            [[-0.40136921405792236, -0.16548694670200348, -0.26746249198913574]],
            [[0.05820125341415405, 0.023428291082382202, -0.3699834942817688]]
        ],
        [
            [[-0.2485920637845993, 0.02315652370452881, 0.032374441623687744]],
            [[-0.2936265468597412, 0.11543101072311401, -0.09102371335029602]],
            [[-0.12715381383895874, -0.2550944685935974, 0.0560973584651947]],
            [[0.13347011804580688, 0.24840587377548218, 0.24507302045822144]],
            [[0.06088784337043762, -0.21260009706020355, 0.3677828907966614]],
            [[-0.3499971330165863, 0.13434505462646484, -0.25860723853111267]],
            [[0.1938493847846985, 0.2694610357284546, 0.14973199367523193]],
            [[-0.11654245853424072, 0.21739059686660767, 0.2852078080177307]]
        ]
    ]
).astype(np.float32)

print (x.shape)
print (W.shape)

# Convolution with padding
node_with_padding = onnx.helper.make_node(
    "Conv",
    inputs=["x", "W"],
    outputs=["y"],
    kernel_shape=[1, 3],
    dilations=[1,2],
    # Default values for other attributes: strides=[1, 1], dilations=[1, 1], groups=1
    pads=[0,0,0,0]
)

y_with_padding = np.array(
    [
        [
            [
                [12.0, 21.0, 27.0, 33.0, 24.0],  # (1, 1, 5, 5) output tensor
                [33.0, 54.0, 63.0, 72.0, 51.0],
                [63.0, 99.0, 108.0, 117.0, 81.0],
                [93.0, 144.0, 153.0, 162.0, 111.0],
                [72.0, 111.0, 117.0, 123.0, 84.0],
            ]
        ]
    ]
).astype(np.float32)

expect(
    node_with_padding,
    inputs=[x, W],
    outputs=[y_with_padding],
    name="test_basic_conv_with_padding",
)

