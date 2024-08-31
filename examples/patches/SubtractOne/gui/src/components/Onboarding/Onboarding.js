import React from "react";
import styled from "styled-components";
import ControlBar from "./ControlBar";
import AnimatedText from "./AnimatedText";
import { motion, AnimatePresence } from "framer-motion";
import { connect } from "react-redux";

const Container = styled(motion.div)`
    position: absolute;
    left: 0;
    top: 0%;
    bottom: 0;
    min-width: 600px;
    width: 45%;
    padding: 110px;
    justify-content: space-between;
    flex-direction: column;
    z-index: 99;
    display: flex;
    @media (max-height: 800px) {
        padding: 60px;
    }
`;

function Onboarding({ finished, currentStep }) {
    return (
        <AnimatePresence initial={false}>
            {!finished && (
                <Container
                    initial={{ x: `0%`, opacity: 1 }}
                    animate={{ x: `0%`, opacity: 1 }}
                    exit={{ x: `-100%`, opacity: 0 }}
                    transition={{
                        type: "spring",
                        stiffness: 100,
                        mass: 2,
                    }}
                >
                    <ControlBar></ControlBar>
                    <div>
                        <AnimatedText headline="SUBTRACT ONE" show={0 === currentStep}>
                            This is Subtract One, an analog inspired, subtractive, monophonic
                            synthesizer, powered by 3 oscillators, a filter, and two envelope
                            generators, controlling the amplitude and the filter. It’s
                            completely written in JavaScript, using the Web Audio API.
                        </AnimatedText>
                        <AnimatedText
                            headline="Playing a note"
                            show={1 === currentStep}
                            images={[
                                { image: "keyboard.png", text: "Use your keyboard" },
                                {
                                    image: "midi-keyboard.png",
                                    text: "Connect a MIDI Device",
                                    small: "Only in Chrome",
                                },
                            ]}
                        >
                            As you see, there is no piano roll attached to the synthesizer.
                            That’s a very common thing. These types of synthesizers are called
                            modules and are controlled by external devices like keyboards or
                            sequencers.
                        </AnimatedText>
                        <AnimatedText
                            headline="Changing the sound"
                            show={2 === currentStep}
                            images={[
                                { image: "knobs.png", text: "Turn some knobs" },
                                { image: "presets.png", text: "Try & modify a preset." },
                            ]}
                        >
                            Analog synthesizers offer an indefinite amount of different
                            sounds. You can create heavy bass sounds, bright bells,
                            atmospheric pads and so much more.
                        </AnimatedText>
                    </div>
                </Container>
            )}
        </AnimatePresence>
    );
}

const mapStateToProps = (state) => {
    return {
        ...state.state.onboarding,
    };
};

export default connect(mapStateToProps, null)(Onboarding);
