import React from "react";
import styled from "styled-components";

import { bindActionCreators } from "redux";
import { beginSetParam, setParam, endSetParam } from "../actions/actions.js";
import { connect } from "react-redux";
import { motion } from "framer-motion";
import Label from "./Label";

import { attachGlobalMoveAndEndListenersToControl } from "./gesture-helper.js";

const Container = styled.div`
    position: relative;
    margin: 10px 0;
`;

const Inner = styled.div`
    padding: 0 15px;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-bottom: 8px;
`;

const KnobInner = styled.div`
    height: 100%;
    width: 100%;
    display: flex;
    justify-content: center;
    align-items: flex-end;
`;
const KnobBase = styled.div`
    touch-action: none;
    cursor: pointer;
    height: ${(props) => props.size}px;
    width: ${(props) => props.size}px;
    border-radius: 100%;
    background-image: linear-gradient(180deg, #4d4d4d 0%, #1c1c1c 99%);
    box-shadow: 0 5px 6px 0 rgba(0, 0, 0, 0.18), 0 22px 22px 0 rgba(0, 0, 0, 0.39),
        inset 0 -1px 0 0 rgba(0, 0, 0, 0.5),
        inset 0 1px 0 0 rgba(255, 255, 255, 0.16);
`;

const Pointer = styled.div`
    background-image: linear-gradient(180deg, #1c1c1c 0%, #4d4d4d 99%);
    height: 10px;
    width: 10px;
    border-radius: 100%;
    margin: 10px;
`;

const Value = styled(motion.div)`
    background: #191919;
    box-shadow: 0 12px 33px 0 rgba(0, 0, 0, 0.37);
    border-radius: 20px;
    height: 40px;
    padding: 0 15px;
    font-size: 16px;
    color: #b0b0b0;
    letter-spacing: 1px;
    text-align: center;
    line-height: 40px;
    position: absolute;
    margin-left: 50%;
    width: auto;
    white-space: nowrap;
    z-index: 999;
    pointer-events: none;
`;

const ValueVariants = {
    hidden: { opacity: 0, transform: `translate(-50%, 20px)` },
    visible: { opacity: 1, transform: `translate(-50%, 0px)` },
};

class KnobControl extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            rotate: this.getRotation(props.value),
            dragging: false,
        };
    }

    onKeyDown(e) {
        if (e.key === "Shift") {
            this.setState({
                freezeValue: this.props.value,
                freezeRotation: this.getRotation(this.props.value),
            });
        }
    }

    onKeyUp(e) {
        if (e.key === "Shift") {
            this.setState({
                freezeValue: this.props.value,
                freezeRotation: this.getRotation(this.props.value),
            });
        }
    }

    componentDidMount() {
        window.addEventListener("keydown", this.onKeyDown.bind(this));
        window.addEventListener("keyup", this.onKeyUp.bind(this));
    }

    componentWillUnmount() {
        // todo: should unmount key handlers
    }

    getRotation(value) {
        if (this.props.log) {
            return this.getRotationLog(value);
        } else {
            return this.getRotationLin(value);
        }
    }

    getRotationLin(value) {
        const { min, max, minDeg, maxDeg } = this.props;
        const scale = (maxDeg - minDeg) / (max - min);
        let rotation = minDeg + (value - min) * scale;
        return rotation;
    }

    getRotationLog(value) {
        const { min, max, minDeg, maxDeg } = this.props;

        if (value === 0) return minDeg;

        const minV = Math.log(min);
        const maxV = Math.log(max);
        const scale = (maxV - minV) / (maxDeg - minDeg);

        return minDeg + (Math.log(value) - minV) / scale;
    }

    onMouseDown(event) {
        attachGlobalMoveAndEndListenersToControl (this);

        const dragStartY = event.clientY || event.touches[0].clientY;

        this.setState({
            dragStartY,
            dragging: true,
            freezeValue: this.props.value,
            freezeRotation: this.getRotation(this.props.value),
        });
    }

    onMouseUp(event) {
        this.setState({
            dragging: false,
        });
    }

    getValue(position) {
        if (this.props.log) {
            return this.getValueLog(position);
        } else {
            return this.getValueLin(position);
        }
    }

    getValueLin(position) {
        const { min, max, minDeg, maxDeg } = this.props;
        var scale = (max - min) / (maxDeg - minDeg);
        return (position - minDeg) * scale + min;
    }

    getValueLog(position) {
        const { min, max, minDeg, maxDeg } = this.props;
        const minV = Math.log(min);
        const maxV = Math.log(max);
        const scale = (maxV - minV) / (maxDeg - minDeg);

        return Math.exp((position - minDeg) * scale + minV);
    }

    onMouseMove(event) {
        if (!this.state.dragging) return;

        const { min, max, param, module, moduleIndex, degreePerPixel } = this.props;

        const clientY = event.clientY || event.touches[0].clientY;

        let delta = (this.state.dragStartY - clientY) * degreePerPixel;

        if (event.shiftKey) {
            delta = delta * 0.2;
        }

        let newValue = this.getValue(this.state.freezeRotation + delta);

        if (this.props.snap !== false) {
            newValue = Math.round(newValue);
        }

        if (newValue === this.props.value) return;

        if (newValue > max) newValue = max;
        else if (newValue < min) {
            newValue = min;
        }
        this.props.setParam(module, moduleIndex, param, newValue);
    }

    onDoubleClick(e) {
        const { param, module, moduleIndex } = this.props;

        this.props.setParam(module, moduleIndex, param, this.props.default);
    }

    render() {
        /* map va;l sto size: transform(this.props.value, [this.props.min, this.props.max],[40,200])*/
        return (
            <Container>
                <Inner>
                    <KnobBase
                        size={this.props.size}
                        onDoubleClick={event => this.onDoubleClick (event)}
                        onTouchStart={event => this.onMouseDown (event)}
                        onMouseDown={event => this.onMouseDown (event)}
                    >
                        <KnobInner
                            style={{
                                transform: `rotate(${this.getRotation(this.props.value)}deg)`,
                            }}
                        >
                            <Pointer
                                style={{
                                    transform: `rotate(-${this.getRotation(
                                        this.props.value
                                    )}deg)`,
                                }}
                            ></Pointer>
                        </KnobInner>
                    </KnobBase>
                </Inner>
                <Label>{this.props.label}</Label>

                <Value
                    initial="hidden"
                    animate={this.state.dragging ? "visible" : "hidden"}
                    variants={ValueVariants}
                    transition={{
                        type: "spring",
                        damping: 20,
                        stiffness: 300,
                        delay: this.state.dragging ? 0 : 1,
                    }}
                >
                    {this.props.value.toFixed(this.props.decimals)} {this.props.unit}
                </Value>
            </Container>
        );
    }
}
KnobControl.defaultProps = {
    min: 0,
    max: 100,
    minDeg: 40,
    maxDeg: 320,
    value: 50,
    label: "Pitch",
    param: "pitch",
    unit: "",
    moduleIndex: false,
    size: 60,
    decimals: 0,
    snap: false,
    log: false,
    degreePerPixel: 1.5,
    onBeginGesture: () => {},
    onEndGesture: () => {},
};

const mapDispatchToProps = (dispatch) => {
    return {
        onBeginGesture: (...args) => dispatch (beginSetParam (...args)),
        onEndGesture: (...args) => dispatch (endSetParam (...args)),
        ...bindActionCreators ({ setParam }, dispatch)
    };
};
export default connect(null, mapDispatchToProps)(KnobControl);
