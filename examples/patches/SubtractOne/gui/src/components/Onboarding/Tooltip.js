import React from "react";
import styled from "styled-components";

const Popup = styled.div`
    padding: 30px;
    background: #1d1d1d;
    width: 400px;
    border-radius: 4px;
    position: absolute;
    bottom: 100%;
    left: 50%;
    right: 0;
    font-size: 16px;
    opacity: 0;
    transition: all 0.3s ease-out;
    transform: translateX(-50%) translateY(20px);
    pointer-events: none;
`;

const Label = styled.span`
    border-bottom: rgba(255, 255, 255, 0.4) 1px dashed;
    cursor: help;
    transition: all 0.2s;
    position: relative;

    &:hover {
        border-color: rgba(255, 255, 255, 0.8);
        div {
            opacity: 1;
            transform: translateX(-50%) translateY(0);
        }
    }
`;

class Tooltip extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            visible: false,
        };
    }
    render() {
        const { children, text } = this.props;
        return (
            <Label>
                {children}
                <Popup>{text}</Popup>
            </Label>
        );
    }
}

Tooltip.defaultProps = {
    text: "Text goes here!",
};

export default Tooltip;
