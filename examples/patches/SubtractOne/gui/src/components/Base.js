import React from "react";
import styled from "styled-components";

import Controls from "./Controls";

const Container = styled.div`
    display: flex;
    width: 1200px;
    height: 820px;
    min-height: 800px;
    margin-top: -15px;
    @media (max-width: 1240px), (max-height: 870px) {
        transform: scale(0.9);
    }
    @media (max-width: 1140px), (max-height: 780px) {
        transform: scale(0.8);
    }
    @media (max-width: 1100px), (max-height: 700px) {
        transform: scale(0.75);
    }
`;
const Wood = styled.div`
    display: flex;
    background: #9e5934;
    background-image: linear-gradient(
        180deg,
        rgba(0, 0, 0, 0.04) 3%,
        rgba(0, 0, 0, 0.18) 100%
    );
    box-shadow: 0 12px 25px 0 rgba(0, 0, 0, 0.25),
        0 52px 54px 0 rgba(0, 0, 0, 0.2), inset -1px -2px 0 0 rgba(0, 0, 0, 0.5),
        inset 0 2px 1px 0 rgba(255, 255, 255, 0.13);
    border-radius: 4px 4px 4px 4px 0;
    width: 50px;
    z-index: 9;
    position: relative;
    background-size: cover;
    border-radius: 4px;
`;
const Header = styled.div`
    margin-bottom: 5px;
    padding-left: 8px;
`;

const Panel = styled.div`
    display: flex;
    align-items: space-between;
    justify-content: stretch;
    flex-direction: column;

    background: #313131;
    box-shadow: 0 12px 25px 0 rgba(0, 0, 0, 0.21),
        0 52px 54px 0 rgba(0, 0, 0, 0.12), inset 0 -1px 0 0 rgba(0, 0, 0, 0.5),
        inset 0 1px 0 0 rgba(255, 255, 255, 0.13);
    border-radius: 3px;
    padding: 30px;
    padding-top: 25px;
    position: relative;
    z-index: 1;
    margin: 10px 0;
`;

function Base() {
    return (
        <Container>
            <Wood style={{ backgroundImage: "url(./images/wood-1.jpg)" }} />
            <Panel>
                <Header>
                    <img alt="Subtract one" src="images/logo.svg" />
                </Header>

                <Controls></Controls>
            </Panel>
            <Wood style={{ backgroundImage: "url(./images/wood-2.jpg)" }} />
        </Container>
    );
}

export default Base;
