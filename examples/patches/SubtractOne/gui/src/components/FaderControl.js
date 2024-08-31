import React from "react";
import styled from "styled-components";

import { bindActionCreators } from "redux";
import { beginSetParam, setParam, endSetParam } from "../actions/actions.js";
import { connect } from "react-redux";
import { transform } from "framer-motion";

import { attachGlobalMoveAndEndListenersToControl } from "./gesture-helper.js";

const knobWidth = 34;
const knobHeight = 48;
const Container = styled.div`
    display: flex;
    flex-direction: column;
    align-items: center;
    margin: 20px 0 30px;
`;

const Track = styled.div`
    background: #181818;
    box-shadow: inset 0 1px 0 0 rgba(0, 0, 0, 0.5),
        inset 0 -1px 0 0 rgba(255, 255, 255, 0.16);
    border-radius: 7px;
    width: 4px;
    height: 100%;
    margin: 0 5px;
`;

const Scale = styled.div`
    display: flex;
    align-items: ${(props) => `flex-${props.align}`};
    flex-direction: column;
    height: 100%;
    justify-content: space-between;
`;

const Background = styled.div`
    position: absolute;
    top: ${knobHeight / 2}px;
    bottom: -${knobHeight / 2}px;
    display: flex;
    width: 100%;
    align-items: center;
    justify-content: center;
`;

const Tick = styled.div`
    background-color: white;
    height: 1px;
    width: ${(props) => (props.size === "big" ? 18 : 12)}px;
    opacity: 0.3;
`;
const Inner = styled.div`
    position: relative;
    height: 100%;
    display: flex;
    justify-content: center;
`;

const Outer = styled.div`
    width: ${knobWidth}px;
    width: 100%;

    position: relative;
    height: ${(props) => props.size}px;
    margin-bottom: ${knobHeight}px;
`;

const Grip = styled.div`
    background-image: linear-gradient(180deg, #1c1c1c 0%, #4d4d4d 99%);
    width: 100%;
    height: 2px;
    opacity: 0.5;
`;

const Knob = styled.div`
    position: relative;
    padding: 5px 0;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: space-around;
    height: ${knobHeight}px;
    width: ${knobWidth}px;
    background-image: linear-gradient(180deg, #1c1c1c 0%, #4d4d4d 99%);
    box-shadow: 0 5px 3px 0 rgba(0, 0, 0, 0.19), 0 8px 7px 0 rgba(0, 0, 0, 0.5),
        inset 0 -1px 0 0 rgba(0, 0, 0, 0.5),
        inset 0 1px 0 0 rgba(255, 255, 255, 0.16);
    border-radius: 5px;
    z-index: 5;
    cursor: pointer;
    touch-action: none;
`;

const Pointer = styled.div`
    width: 100%;
    height: 4px;
    background: #bcbcbc;
    margin: 5px 0;
`;

class FaderControl extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            dragging: false,
        };
        this.defaultValue = props.value;
    }

    getTransform(value = this.props.value) {
        const { min, max, size } = this.props;
        const y = size - transform(value, [min, max], [0, size]);

        return y;
    }

    onMouseDown(event) {
        attachGlobalMoveAndEndListenersToControl (this);

        const dragStartY = event.clientY || event.touches[0].clientY;
        this.setState({
            dragStartY,
            dragging: true,
            freezeValue: this.props.value,
        });
    }

    onMouseUp(event) {
        this.setState({
            dragging: false,
        });
    }

    onMouseMove(event) {
        if (!this.state.dragging) return;

        const { min, max, param, module, moduleIndex, size } = this.props;
        const clientY = event.clientY || event.touches[0].clientY;

        const delta = this.state.dragStartY - clientY;

        const increase = transform(Math.abs(delta), [0, size], [min, max]);

        let newValue =
            this.state.freezeValue + (delta < 0 ? -1 * increase : increase); //transform(newY + size, [0, size], [min, max]);

        if (newValue > max) newValue = max;
        else if (newValue < min) {
            newValue = min;
        }
        // console.log(module, moduleIndex, param, newValue)
        this.props.setParam(module, moduleIndex, param, newValue);
    }

    onDoubleClick(e) {
        //  const { param, module, moduleIndex } = this.props;
        //this.props.setParam(module, moduleIndex, param, this.defaultValue)
    }

    renderScale() {
        const scale = 31;

        const ticks = [];
        for (let i = 0; i < scale; i++) {
            ticks.push(<Tick key={i} size={i % 5 === 0 ? "big" : "small"}></Tick>);
        }
        return ticks;
    }

    render() {
        /* map va;l sto size: transform(this.props.value, [this.props.min, this.props.max],[40,200])*/
        return (
            <Container>
                <Outer size={this.props.size}>
                    <Inner>
                        <Background>
                            <Scale align={"end"}>{this.renderScale()}</Scale>
                            <Track></Track>
                            <Scale align={"start"}>{this.renderScale()}</Scale>
                        </Background>
                        <Knob
                            onDoubleClick={this.onDoubleClick.bind(this)}
                            onMouseDown={this.onMouseDown.bind(this)}
                            onTouchStart={this.onMouseDown.bind(this)}
                            style={{ transform: `translateY(${this.getTransform()}px)` }}
                        >
                            <Grip></Grip>
                            <Grip></Grip>
                            <Grip></Grip>
                            <Pointer></Pointer>
                            <Grip></Grip>
                            <Grip></Grip>
                            <Grip></Grip>
                        </Knob>
                    </Inner>
                </Outer>
            </Container>
        );
    }
}
FaderControl.defaultProps = {
    min: 0,
    max: 100,
    value: 50,
    label: "Gain",
    param: "gain",
    unit: "",
    moduleIndex: false,
    size: 220,
    onBeginGesture: () => {},
    onEndGesture: () => {},
};

const mapDispatchToProps = (dispatch) => {
    return {
        onBeginGesture: (...args) => dispatch (beginSetParam (...args)),
        onEndGesture: (...args) => dispatch (endSetParam (...args)),
        ...bindActionCreators ({ setParam  }, dispatch),
    };
};
export default connect(null, mapDispatchToProps)(FaderControl);
