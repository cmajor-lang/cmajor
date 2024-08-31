import React from "react";
import styled from "styled-components";
import { connect, useStore } from "react-redux";
import { bindActionCreators } from "redux";
import { onboardingReset, sharePatch } from "../actions/actions.js";
import { motion, AnimatePresence } from "framer-motion";

const Container = styled.div`
    text-transform: uppercase;
    font-size: 12px;

    letter-spacing: 2px;
    position: absolute;
    bottom: 20px;
    display: flex;
    justify-content: space-between;
    left: 20px;
    right: 20px;
    span {
        opacity: 0.5;
    }
    button,
    a {
        opacity: 0.5;
        color: white;
        text-decoration: none;
        display: inline-block;
        border: none;
        text-transform: uppercase;
        font-size: 12px;
        letter-spacing: 2px;

        border-bottom: 1px dashed;
        background: none;
        outline: none;
        display: inline;
        padding: 0;

        cursor: pointer;
        &:hover {
            opacity: 0.8;
        }
    }
`;

const Item = styled(motion.div)``;

function BottomBar({ onboardingReset, setPatchLink, show, sharePatch }) {
    const store = useStore();
    const animate = {
        animate: { opacity: 1 },
        exit: { opacity: 0 },
        transition: { duration: 0.3 },
    };

    async function share() {
        const url = "https://subtract.one";

        //const url = "http://localhost:3000";
        const state = store.getState();
        const {
            power,
            keyboard,
            sequencer,
            amp,
            meta,
            oscilloscope,
            onboarding,
            ...patch
        } = state.state;

        const string = JSON.stringify(patch);

        const whatsthis = `0fd40510df1fb0a5d106d8c02e61eb92145390ca`;

        //var queryString = Object.keys(state.state).map(key => key + '=' + state.state[key]).join('&');
        //  copyStringToClipboard(string)

        const long_url = url + "#" + encodeURIComponent(string);
        console.log(long_url);

        await fetch("https://api-ssl.bitly.com/v4/shorten", {
            method: "POST",
            headers: new Headers({
                Authorization: `Bearer ${whatsthis}`,
                "Content-Type": "application/json",
            }),
            body: JSON.stringify({
                long_url,
            }),
        })
            .then((res) => res.json())
            .then(
                (result) => {
                    console.log(result.link);
                    sharePatch(result.link);
                },
                // Note: it's important to handle errors here
                // instead of a catch() block so that we don't swallow
                // exceptions from actual bugs in components.
                (error) => {
                    alert("Something went wrong");
                }
            );
    }

    return (
        <Container>
            <AnimatePresence>
                {show && [
                    <Item key={0} {...animate}>
                        <button onClick={onboardingReset}>How to use </button>
                    </Item>,
                    <Item key={1} {...animate}>
                        <span>Made by </span>
                        <a
                            target="_blank"
                            rel="noopener noreferrer"
                            href="https://twitter.com/juliussohn"
                        >
                            Julius Sohn
                        </a>
                    </Item>,
                    <Item key={2} {...animate}>
                        <button onClick={share}>Share Patch </button>{" "}
                    </Item>,
                ]}
            </AnimatePresence>
        </Container>
    );
}

const mapStateToProps = (state) => {
    return {
        ...state.state.onboarding,
    };
};

const mapDispatchToProps = (dispatch) => {
    return bindActionCreators({ onboardingReset, sharePatch }, dispatch);
};

export default connect(mapStateToProps, mapDispatchToProps)(BottomBar);
