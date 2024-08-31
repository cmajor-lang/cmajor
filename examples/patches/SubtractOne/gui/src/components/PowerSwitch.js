import React from "react";
import styled from "styled-components";
import { bindActionCreators } from "redux";
import { setPower } from "../actions/actions.js";

import { connect } from "react-redux";

const Lamp = styled.div`
    height: 8px;
    width: 8px;
    border-radius: 100%;

    background: ${(props) => (props.active ? "#40EC00" : "#434343")};
    background-image: linear-gradient(
        180deg,
        rgba(255, 255, 255, 0.5) 0%,
        rgba(255, 255, 255, 0.02) 100%
    );
    box-shadow: ${(props) =>
        props.active
            ? "0 -1px 0 0 rgba(0,0,0,0.08), 0 1px 0 0 rgba(255,255,255,0.08), 0 0 10px 0 #40EC00, inset 0 -2px 4px 0 rgba(21,76,9,0.50), inset 0 2px 4px 0 rgba(212,255,170,0.50)"
            : " 0 -1px 0 0 rgba(0,0,0,0.08), 0 1px 0 0 rgba(255,255,255,0.08), 0 0 10px 0 rgba(64,236,0,0.00), inset 0 -2px 4px 0 rgba(0,0,0,0.42), inset 0 2px 4px 0 rgba(51,51,51,0.50)"};

    transition: all 0.1s;
`;

const Container = styled.div`
    display: flex;
    flex-direction: column;
    align-items: center;
    flex: 1;
    justify-content: space-around;
`;
const Track = styled.div`
    box-shadow: inset 0 1px 0 0 rgba(0, 0, 0, 0.5),
        inset 0 -1px 0 0 rgba(255, 255, 255, 0.16);
    border-radius: 15px;
    height: 24px;
    width: 48px;
    overflow: auto;
    cursor: pointer;
    transition: all 0.2s;
`;

const Knob = styled.div`
    height: 20px;
    width: 20px;
    border-radius: 100%;
    margin: 2px;
    transition: all 0.3s;

    background-image: linear-gradient(
        180deg,
        #4d4d4d 0%,
        #393939 42%,
        #1c1c1c 99%
    );
    box-shadow: 0 5px 3px 0 rgba(0, 0, 0, 0.19), 0 8px 7px 0 rgba(0, 0, 0, 0.5),
        inset 0 -1px 0 0 rgba(0, 0, 0, 0.5),
        inset 0 1px 0 0 rgba(255, 255, 255, 0.16);
`;

class PowerSwitch extends React.Component {
    constructor(props) {
        super(props);
        this.state = {};
    }
    toggle() {
        const { value } = this.props;

        this.props.setPower(!value);
    }
    componentDidUpdate(prevProps) {
        const { value } = this.props;
        //when prev animation finished
        if (prevProps.value !== value) {
            const audio = new Audio("./sounds/switch.mp3");
            audio.play();
        }
    }

    render() {
        const { value } = this.props;
        return (
            <Container>
                <Lamp active={value}></Lamp>
                <Track
                    style={{ background: value ? "#aaa" : "#111" }}
                    onClick={this.toggle.bind(this)}
                    active={value}
                >
                    <Knob style={{ marginLeft: value ? 26 : 2 }}></Knob>
                </Track>
            </Container>
        );
    }
}

PowerSwitch.defaultProps = {
    value: true,
};

const mapDispatchToProps = (dispatch) => {
    return bindActionCreators({ setPower }, dispatch);
};
export default connect(null, mapDispatchToProps)(PowerSwitch);
