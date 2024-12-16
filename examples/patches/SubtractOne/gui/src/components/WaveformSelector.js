import React from "react";
import styled from "styled-components";
import { bindActionCreators } from "redux";
import { beginSetParam, setParam, endSetParam } from "../actions/actions.js";

import { connect } from "react-redux";

import { attachGlobalMoveAndEndListenersToControl } from "./gesture-helper.js";

const iconWidth = 20;
const iconWidthPixels = `${iconWidth}px`;
const Container = styled.div`
    display: flex;
    height: ${(props) => `${props.trackHeight}px`};
    justify-content: center;
`;

const Labels = styled.div`
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    margin-left: 10px;
`;

const Track = styled.div`
    cursor: pointer;
    height: 100%;
    width: ${iconWidthPixels};
    background: #111111;
    box-shadow: inset 0 1px 0 0 rgba(0, 0, 0, 0.5),
        inset 0 -1px 0 0 rgba(255, 255, 255, 0.16);
    border-radius: 10px;
`;
const Knob = styled.div`
    cursor: pointer;
    width: ${iconWidthPixels};
    height: ${iconWidthPixels};
    border-radius: 100%;
    background-image: linear-gradient(180deg, #4d4d4d 0%, #1c1c1c 99%);
    box-shadow: 0 5px 3px 0 rgba(0, 0, 0, 0.19), 0 8px 7px 0 rgba(0, 0, 0, 0.5),
        inset 0 -1px 0 0 rgba(0, 0, 0, 0.5),
        inset 0 1px 0 0 rgba(255, 255, 255, 0.16);
    position: relative;
    transition: all 0.2s;
    touch-action: none;
`;

const ShapeImage = styled.div`
    cursor: pointer;
`;

const shapeSVGFactories = {
    sine: () => (
        <svg
            width={iconWidthPixels}
            height="11px"
            viewBox="0 0 20 11"
            version="1.1"
            xmlns="http://www.w3.org/2000/svg"
        >
            <g
                id="Artboard"
                stroke="none"
                strokeWidth="1"
                fill="none"
                fillRule="evenodd"
                strokeLinecap="round"
            >
                <path
                    d="M19,5.5 C19,7.985 16.985,10 14.5,10 C12.015,10 10,7.985 10,5.5 C10,3.015 7.985,1 5.5,1 C3.015,1 1,3.015 1,5.5"
                    id="Stroke-1"
                    stroke="#C6C6C6"
                ></path>
            </g>
        </svg>
    ),
    triangle: () => (
        <svg
            width={iconWidthPixels}
            height="11px"
            viewBox="0 0 20 11"
            version="1.1"
            xmlns="http://www.w3.org/2000/svg"
        >
            <g
                id="Artboard-Copy-36"
                stroke="none"
                strokeWidth="1"
                fill="none"
                fillRule="evenodd"
                strokeLinecap="round"
                strokeLinejoin="round"
            >
                <polyline
                    id="Path-4"
                    stroke="#C6C6C6"
                    points="1 10 10 1 19 10"
                ></polyline>
            </g>
        </svg>
    ),
    sawtooth: (overrides = {}) => (
        <svg
            width={iconWidthPixels}
            height="11px"
            viewBox="0 0 20 11"
            version="1.1"
            xmlns="http://www.w3.org/2000/svg"
            {...overrides}
        >
            <g
                id="Artboard-Copy-38"
                stroke="none"
                strokeWidth="1"
                fill="none"
                fillRule="evenodd"
                strokeLinecap="round"
                strokeLinejoin="round"
            >
                <polyline
                    id="Path-7"
                    stroke="#C6C6C6"
                    points="1 10 19 1 19 10"
                ></polyline>
            </g>
        </svg>
    ),
    square: () => (
        <svg
            width={iconWidthPixels}
            height="11px"
            viewBox="0 0 20 11"
            version="1.1"
            xmlns="http://www.w3.org/2000/svg"
        >
            <g
                id="Artboard-Copy-37"
                stroke="none"
                strokeWidth="1"
                fill="none"
                fillRule="evenodd"
                strokeLinecap="round"
                strokeLinejoin="round"
            >
                <polyline
                    id="Path-5-Copy"
                    stroke="#C6C6C6"
                    points="1 10 1 1 10 1 10 10 19 10"
                ></polyline>
            </g>
        </svg>
    ),
};
const makeWaveformIconComponent = (key) => {
    const makeComponent = shapeSVGFactories[key];
    if (makeComponent)
        return makeComponent();

    return key === "rampdown" ? shapeSVGFactories["sawtooth"]({style: {transform: "scale(-1,1)"}}) : undefined;
}

class WaveformSelector extends React.Component {
    constructor(props) {
        super(props);
        this.state = {};

        this.getTrackHeight = () => this.props.shapes.length * iconWidth;
        this.audio = new Audio("./sounds/switch.mp3");
    }

    select(type) {
        const { module, moduleIndex } = this.props;
        const audio = new Audio("./sounds/switch.mp3");
        audio.play();
        this.props.setParam(module, moduleIndex, "type", type);
    }

    getShapes() {
        return this.props.shapes.map((s, i) => (
            <ShapeImage
                key={`${this.props.moduleIndex}shape_${i}`}
                onClick={(e) => {
                    this.select(s);
                }}
            >
                {makeWaveformIconComponent (s)}
            </ShapeImage>
        ));
    }

    getIndex(type) {
        return this.props.shapes.indexOf(type);
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

    onMouseMove(event) {
        if (!this.state.dragging) return;
        const clientY = event.clientY || event.touches[0].clientY;
        const delta = this.state.dragStartY - clientY;

        const stepSize = this.getTrackHeight() / this.props.shapes.length;

        //if (Math.abs(delta / stepSize) > 1) {
        const diff = -1 * Math.round(delta / stepSize);
        let newVal = this.getIndex(this.state.freezeValue) + diff;

        if (
            newVal > -1 &&
            newVal < this.props.shapes.length &&
            this.props.shapes[newVal] !== this.props.value
        ) {
            this.select(this.props.shapes[newVal]);
        }
        //  }
    }
    onMouseUp(event) {
        this.setState({
            dragging: false,
        });
    }

    handleTrackClick(e) {
        var bb = e.target.getBoundingClientRect();

        const newVal = Math.round(
            (e.clientY - bb.top) / (this.getTrackHeight() / this.props.shapes.length)
        );

        if (this.props.shapes[newVal] !== this.props.value) {
            this.select(this.props.shapes[newVal]);
        }
    }

    render() {
        const { value } = this.props;

        return (
            <Container trackHeight={this.getTrackHeight()}>
                <Track onClick={this.handleTrackClick.bind(this)}>
                    <Knob
                        onClick={(e) => {
                            e.stopPropagation();
                            return false;
                        }}
                        onTouchStart={this.onMouseDown.bind(this)}
                        onMouseDown={this.onMouseDown.bind(this)}
                        style={{ top: this.getIndex(value) * (100 / this.props.shapes.length) + "%" }}
                    ></Knob>
                </Track>
                <Labels>{this.getShapes()}</Labels>
            </Container>
        );
    }
}

WaveformSelector.defaultProps = {
    shapes: ["sine", "triangle", "sawtooth", "square"],
    value: "sine",
    onBeginGesture: () => {},
    onEndGesture: () => {},
};

const mapStateToProps = (state) => {
    return {
        shapes: state.state.vcoShapes,
    };
};

const mapDispatchToProps = (dispatch) => {
    return {
        onBeginGesture: (...args) => dispatch (beginSetParam (...args)),
        onEndGesture: (...args) => dispatch (endSetParam (...args)),
        ...bindActionCreators ({ setParam }, dispatch),
    };
};
export default connect(mapStateToProps, mapDispatchToProps)(WaveformSelector);
