import React from "react";
import styled from "styled-components";
import { bindActionCreators } from "redux";
import { setPreset } from "../actions/actions.js";
import { connect } from "react-redux";

const colors = [
    "#FFFFFF",
    "#E89A0A",
    "#EB420A",
    "#D0227E",
    "#7829D6",
    "#235ED6",
    "#23AC9E",
    "#63BF17",
];

const Base = styled.div`
    cursor: pointer;
    background: ${(props) => props.color};
    box-shadow: ${(props) =>
        props.active
            ? " 0 0 0 1px rgba(0,0,0,0.50), 0 1px 2px 0 rgba(0,0,0,0.19), 0 2px 5px 0 rgba(0,0,0,0.50), inset 0 -2px 3px 0 rgba(0,0,0,0.50), inset 0 3px 4px 0 rgba(255,255,255,0.09)"
            : " 0 0 0 1px rgba(0,0,0,0.50), 0 5px 5px 0 rgba(0,0,0,0.19), 0 12px 13px 0 rgba(0,0,0,0.50), inset 0 -2px 3px 0 rgba(0,0,0,0.50), inset 0 3px 4px 0 rgba(255,255,255,0.09)"};
    border-radius: 3px;
    transition: all 0.1s ease-out;
    height: 44px;
    flex: 1;
    margin-right: 10px;
    position: relative;
    z-index: ${(props) => (props.active ? 1 : 5)};
    &:last-child {
        margin-right: 0;
    }
    &::before {
        transition: all 0.1s ease-out;
        content: "";
        position: absolute;
        left: 0;
        top: 0;
        bottom: 0;
        right: 0;
        background-image: linear-gradient(
            180deg,
            rgba(0, 0, 0, 0.16) 0%,
            rgba(0, 0, 0, 0.37) 98%
        );
        opacity: ${(props) => (props.active ? 1 : 0)};
    }

    &::after {
        transition: all 0.1s ease-out;
        display: block;

        content: "";
        position: absolute;
        left: 0;
        top: 0;
        bottom: 0;
        right: 0;
        background-image: linear-gradient(
            180deg,
            rgba(255, 255, 255, 0.18) 0%,
            rgba(255, 255, 255, 0) 100%
        );
        opacity: ${(props) => (props.active ? 0 : 1)};
    }
`;

class PowerSwitch extends React.Component {
    constructor(props) {
        super(props);
        this.state = {};
    }

    handleClick() {
        if (this.props.active) return;
        const audio = new Audio("./sounds/button.mp3");
        audio.play();
        this.props.onClick();
    }

    render() {
        const { active } = this.props;
        return (
            <Base
                color={colors[this.props.preset]}
                onClick={this.handleClick.bind(this)}
                active={active}
            ></Base>
        );
    }
}

PowerSwitch.defaultProps = {
    value: true,
};

const mapDispatchToProps = (dispatch) => {
    return bindActionCreators({ setPreset }, dispatch);
};
export default connect(null, mapDispatchToProps)(PowerSwitch);
