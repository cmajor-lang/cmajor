import React from "react";
import styled from "styled-components";
import Base from "./components/Base";
import BottomBar from "./components/BottomBar";
import ShareModal from "./components/ShareModal";
import Onboarding from "./components/Onboarding/Onboarding";
import { motion } from "framer-motion";
import { connect } from "react-redux";
import { useState, useEffect } from "react";
import AnimatedText from "./components/Onboarding/AnimatedText";

const AppContainer = styled.div`
    align-items: center;
    /*justify-content:center;*/
    height: ${(props) => props.height}px;
    width: 100vw;
    align-items: stretch;
    overflow: hidden;
`;

const SynthContainer = styled(motion.div)`
    width: 100vw;
    height: 100%;
    overflow: hidden;
    padding: 50px;
    align-items: center;
    justify-content: space-around;
    display: flex;
    flex-direction: column;
    pointer-events: ${(props) => (props.clickable ? "auto" : "none")};
`;

const PhoneContainer = styled.div`
    width: 80%;
    max-width: 500px;
    padding: 20px;
    position: fixed;
    top: 20px;
    left: 20px;
`;
const variants = {
    onboarding: { x: `45%` },
    onboardingSmall: { x: `100%` },
    default: { x: `0%` },
};

function App({ finished }) {
    const size = useWindowSize();

    if (size.height < 600 || size.width < 900)
        return (
            <PhoneContainer>
                <AnimatedText headline="Oh, Heck!" show={true}>
                    This screen is to small to properly shred. Those knobs would be tiny.
                    How did you imagine this would work? Please open this site on a tablet
                    or desktop device.
                </AnimatedText>
            </PhoneContainer>
        );
    return (
        <AppContainer height={size.height}>
            <ShareModal></ShareModal>
            <Onboarding></Onboarding>
            <SynthContainer
                clickable={finished}
                variants={variants}
                initial={
                    finished
                        ? `default`
                        : size.width <= 1024
                        ? `onboardingSmall`
                        : `onboarding`
                }
                animate={
                    finished
                        ? `default`
                        : size.width <= 1024
                        ? `onboardingSmall`
                        : `onboarding`
                }
                transition={{
                    type: "spring",
                    stiffness: 60,
                    damping: 22,
                }}
            >
                <Base></Base>
                <BottomBar show={finished} />
            </SynthContainer>
        </AppContainer>
    );
}

function useWindowSize() {
    const isClient = typeof window === "object";

    function getSize() {
        return {
            width: isClient ? window.innerWidth : undefined,
            height: isClient ? window.innerHeight : undefined,
        };
    }

    const [windowSize, setWindowSize] = useState(getSize);

    useEffect(() => {
        if (!isClient) {
            return false;
        }

        function handleResize() {
            setWindowSize(getSize());
        }

        window.addEventListener("resize", handleResize);
        return () => window.removeEventListener("resize", handleResize);
    }); // Empty array ensures that effect is only run on mount and unmount

    return windowSize;
}

const mapStateToProps = (state) => {
    return {
        ...state.state.onboarding,
    };
};

export default connect(mapStateToProps, null)(App);
