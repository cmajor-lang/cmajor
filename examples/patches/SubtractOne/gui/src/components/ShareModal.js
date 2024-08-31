import React, { useState } from "react";
import styled from "styled-components";
import { connect } from "react-redux";
import { bindActionCreators } from "redux";
import { closeShareModal } from "../actions/actions.js";
import { motion, AnimatePresence } from "framer-motion";

const Overlay = styled(motion.div)`
    background-color: rgba(0, 0, 0, 0.3);
    position: fixed;
    top: 0;
    bottom: 0;
    left: 0%;
    right: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 99;
`;

const Modal = styled(motion.div)`
    background-color: #141414;
    padding: 60px;
    border-radius: 10px;
    box-shadow: 0px 24px 44px rgba(0, 0, 0, 0.15);
    position: relative;
    width: 640px;
    position: fixed;
    top: 50%;
    left: 50%;
    transform: translateX(-50%) translateY(-50%);
    z-index: 999;
`;
const CopyButton = styled.button`
    padding: 16px;
    border-top-right-radius: 4px;
    border-bottom-right-radius: 4px;
    background: #333333;
    font-size: 14px;
    text-transform: uppercase;
    font-weight: 600;
    color: white;
    border: none;
    outline: none;
    width: 120px;
    cursor: pointer;
    &:hover {
        background-color: #444444;
    }
`;

const Input = styled.input`
    border: 1px solid #333333;
    box-sizing: border-box;
    border-top-left-radius: 4px;
    border-bottom-left-radius: 4px;
    background: none;
    padding: 12px;
    color: white;
    font-size: 20px;
    outline: none;
    margin-right: 0;
    flex: 1;
    width: auto;
    display: block;
    min-width: 0;
`;
const Close = styled.a`
    position: absolute;
    top: 20px;
    right: 20px;
    opacity: 0.5;
    cursor: pointer;
    &:hover {
        opacity: 0.8;
    }
`;
const Title = styled.div`
    font-family: "Syne Bold";
    font-size: 50px;
    margin: 0;
    font-weight: bold;
`;

const SocialShare = styled.a`
    cursor: pointer;
    flex-shrink: 0;
    height: 58px;
    width: 58px;
    border-radius: 4px;
    background-color: #333333;
    margin-left: 12px;
    display: flex;
    align-items: center;
    justify-content: center;
    img {
        width: 30px;
    }
    &:hover {
        background-color: #444444;
    }
`;

const Link = styled.div`
    display: flex;
    flex: 1;
`;
const Description = styled.p`
    margin-left: 70px;
    font-size: 16px;
    line-height: 1.5;
    margin-top: 0;
    margin-bottom: 5px;
    color: #a8a8a8;
`;

const Header = styled.div`
    display: flex;
    margin-bottom: 30px;
    align-items: flex-end;
`;

const ShareOptions = styled.div`
    display: flex;
`;

function BottomBar({ shareModal, patchLink, closeShareModal }) {
    const [copyLabel, setCopyLabel] = useState("Copy Link");

    function copyToClipBoard() {
        var el = document.createElement("textarea");
        el.value = patchLink;
        el.setAttribute("readonly", "");
        el.style = { position: "absolute", left: "-9999px" };
        document.body.appendChild(el);
        el.select();
        document.execCommand("copy");
        document.body.removeChild(el);
        setCopyLabel("Copied!");
    }

    function closeModal() {
        closeShareModal();
    }

    return (
        <AnimatePresence>
            {shareModal && [
                <Overlay
                    key="overlay"
                    initial={{ opacity: 0 }}
                    animate={{ opacity: 1 }}
                    exit={{ opacity: 0 }}
                ></Overlay>,
                <Modal
                    key="modal"
                    initial={{ y: `-35%`, x: `-50%`, opacity: 0 }}
                    animate={{ y: `-50%`, x: `-50%`, opacity: 1 }}
                    exit={{
                        y: `-35%`,
                        x: `-50%`,
                        opacity: 0,
                    }}
                    onAnimationComplete={() => {
                        setCopyLabel("Copy Link");
                    }}
                    transition={{ type: "spring", stiffness: 100, damping: 300 }}
                >
                    <Close onClick={closeModal}>
                        <img alt="close" src="./images/x.svg" />
                    </Close>
                    <Header>
                        <div>
                            <Title>Share Patch</Title>
                        </div>
                        <Description>
                            When opening the link below, all settings will be preserved and
                            the synth will create the exact same sound.
                        </Description>
                    </Header>

                    <ShareOptions>
                        <Link>
                            <Input type="text" readonly value={patchLink} />
                            <CopyButton onClick={copyToClipBoard}>{copyLabel}</CopyButton>
                        </Link>
                        <SocialShare
                            target="_blank"
                            rel="noopener noreferrer"
                            href={`https://twitter.com/intent/tweet?url=${patchLink}`}
                        >
                            <img alt="Twitter" src="./images/twitter.svg" />
                        </SocialShare>
                        <SocialShare
                            target="_blank"
                            rel="noopener noreferrer"
                            href={`https://www.facebook.com/sharer/sharer.php?u=${patchLink}`}
                        >
                            <img alt="Facebook" src="./images/facebook.svg" />
                        </SocialShare>
                    </ShareOptions>
                </Modal>,
            ]}
        </AnimatePresence>
    );
}

const mapStateToProps = (state) => {
    return {
        ...state.state.onboarding,
        ...state.state.meta,
    };
};

const mapDispatchToProps = (dispatch) => {
    return bindActionCreators({ closeShareModal }, dispatch);
};

export default connect(mapStateToProps, mapDispatchToProps)(BottomBar);
