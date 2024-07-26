# FaustFM

This patch uses a source transformer to compile faust to Cmajor at patch compile time. Using this capability, this patch has both Cmajor and Faust source code for the DSP, and shows how these langauges can be compiled and inter-operate within the Cmajor runtime.

This example patch includes a faust FM oscillator, and a faust implementation of Freeverb. These components are integrated into a Cmajor graph to support polyphony, and midi transformation.

## Source Transformer

The source transformer is a javascript file, very similar to a patch worker, which is specified within the patch definition using the `sourceTransformer` key. The javascript registers itself, and communicates with the cmajor patch loader asynchronously. Requests to transform source code are received using the `transformRequest` message, and responses are sent using either `transformResponse` or `transformError`

In this example, the source transformer loads a wasm build of the faust library, and converts faust to cmajor using this library.

For more information about the source transformer, please refer to the patch format documentation.

For information about faust, and the wasm build of the faust library, see https://faust.grame.fr/

## Limitations

This is an experimental feature, and the API is likely to evolve over time. If you develop your own source transformer, expect to require updates to keep compatible with this patch feature.

The source transformer is only available in asynchronous builds, which limits the situations where these patches can be compiled. For now, the VSCode extension, and the command line tool `play` and `generate` commands support this, and support will grow over time.

The transformed source code is not readily visible, which means it can be difficult to integrate the components together.