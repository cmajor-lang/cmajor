## GuitarLSTM

This patch is a machine leaning inference implemented in Cmajor.

The code to generate this patch can be found in https://github.com/cmajor-lang/GuitarLSTM, which is a fork of the github project provided by https://guitarml.com


### Recreating the patch

To recreate this patch yourself, check out the above repo like this:

    git clone git@github.com:cmajor-lang/GuitarLSTM.git
    cd GuitarLSTM
    git submodule init
    git submodule update

The model can then be trained with:

    ./train.py data/ts9_test1_in_FP32.wav data/ts9_test1_out_FP32.wav test

This will generate the output in models/test, and the generated Cmajor will be in `models/test/patch`


### How this works

The model and inference is unchanged from the GuitarLSTM script, so this uses TensorFlow to build the model and run the training.

After training, we export the TF model to an RTNeural `.json` file, using the scripts included in the RTNeural package.

The script then executes the python script in `cmajor/tools/rtneural` to generate a Cmajor file based on the generated RTNeural json file.
