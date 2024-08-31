import React from "react";
import styled from "styled-components";

import { connect } from "react-redux";

const Container = styled.div`
    display: flex;
    align-items: space-around;
`;
const LED = styled.div`
    margin: 0 10px;
    height: 10px;
    width: 10px;
    border-radius: 100%;

    ${(props) =>
        props.active
            ? "background: #25d025; background: radial-gradient(40% 35%, #5aef5a, #25d025 60%); box-shadow: inset 0 3px 5px 1px rgba(0,0,0,0.1),  0 1px 0 rgba(255,255,255,0.4),  0 0 10px 2px rgba(0, 210, 0, 0.5);"
            : "background: #ddd; background: linear-gradient(#ccc, #fff); box-shadow: inset 0 2px 1px rgba(0,0,0,0.15), 0 2px 5px rgba(200,200,200,0.1); "}
`;

// background:${props => props.active ? 'green': 'black'};

class Sequencer extends React.Component {
    renderLEDs() {
        const leds = [];
        const { steps, currentStep } = this.props.sequencer;
        for (let i = 0; i < steps; i++) {
            leds.push(<LED key={i} active={i === currentStep}></LED>);
        }
        return leds;
    }

    render() {
        const { props } = this;
        console.log(props);

        return <Container>{this.renderLEDs()}</Container>;
    }
}
const mapStateToProps = (state) => {
    return {
        ...state.state,
    };
};
/*
const mapDispatchToProps = (dispatch) => {
    return bindActionCreators({ setPitch }, dispatch)
}
*/

export default connect(mapStateToProps, null)(Sequencer);
