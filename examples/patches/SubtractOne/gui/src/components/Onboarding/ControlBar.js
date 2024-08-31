import React from "react";
import styled from "styled-components";
import { bindActionCreators } from "redux";
import { connect } from "react-redux";
import { onboardingNextStep, onboardingFinish } from "../../actions/actions.js";
import { motion } from "framer-motion";
import { setOnboardingState } from "../../localStorage";

const Container = styled.div`
    display: flex;
    justify-content: space-between;
    align-items: center;
    position: relative;
    z-index: 10;
`;
const CounterContainer = styled.div`
    display: flex;
    height: 20px;
    font-size: 20px;
    letter-spacing: 8px;
    line-height: 1;
    position: relative;
    width: 100px;
    overflow: hidden;
    height: 40px;
`;
const CurrentPageContainer = styled.div`
    width: 20px;
    position: absolute;
    bottom: 0;
`;

const Page = styled(motion.div)`
    position: absolute;
    text-align: right;
    font-size: 40px;
    left: 0px;
    bottom: 0px;
    width: 30px;
`;

const TotalSteps = styled.div`
    position: absolute;
    bottom: 0;
    left: 30px;
`;

const ArrowButtonContainer = styled(motion.div)`
    cursor: pointer;
`;

const circleMotion = {
    rest: {
        strokeDasharray: 170,
        strokeDashoffset: 170,
        rotate: 0,
        stroke: "white",
    },
    hover: {
        rotate: 270,
        strokeDashoffset: 0,
    },
    loading: {
        strokeDasharray: 170,
        strokeDashoffset: 170,
        rotate: 900,
    },
};

const arrowMotion = {
    rest: {
        opacity: 0.8,
        translateX: 0,
    },
    hover: {
        opacity: 1,
        translateX: 0,
    },
    tap: {},
    loading: {
        opacity: [1, 1, 0, 0, 1],
        translateX: [0, 200, 200, -200, -200],
        transition: {
            duration: 2,
            times: [0, 0.3, 0.4, 0.6, 1],
        },
    },
};

class ArrowButton extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            hover: false,
            state: "rest",
        };

        this.onTap = this.onTap.bind(this);
        this.onHoverStart = this.onHoverStart.bind(this);
        this.onHoverEnd = this.onHoverEnd.bind(this);
        this.onTapStart = this.onTapStart.bind(this);
    }
    onTap() {
        if (this.state.state === "loading") return;

        this.props.onClick();
        this.setState({ state: "loading" });
        setTimeout(() => {
            this.setState({ state: this.state.hover ? "hover" : "rest" });
        }, 2000);
    }
    onHoverStart() {
        this.setState({ hover: true });
        if (this.state.state === "loading") return;
        this.setState({ state: "hover" });
    }
    onHoverEnd() {
        this.setState({ hover: false });
        if (this.state.state === "loading") return;
        this.setState({ state: "rest" });
    }
    onTapStart() {
        if (this.state.state === "loading") return;
        this.setState({ state: "tap" });
    }

    render() {
        const { state } = this.state;
        return (
            <ArrowButtonContainer
                onHoverEnd={this.onHoverEnd}
                onTapStart={this.onTapStart}
                onHoverStart={this.onHoverStart}
                onTap={this.onTap}
                transition={{ damping: 300, type: "spring" }}
                initial="rest"
            >
                <svg width="54" height="54" viewBox="0 0 54 54" fill="none">
                    <circle cx="27" cy="27" r="26.5" stroke="grey" />
                    <motion.circle
                        transition={{ damping: 300, type: "spring", mass: 2 }}
                        animate={state}
                        variants={circleMotion}
                        cx="27"
                        cy="27"
                        r="26.5"
                        stroke="white"
                    />
                    <motion.path
                        transition={{ damping: 300, type: "spring", mass: 2 }}
                        animate={state}
                        variants={arrowMotion}
                        d="M32.7895 21.12L31.1395 22.74L34.5895 26.52H16.4395V28.77H34.5895L31.1395 32.55L32.7895 34.17L38.6995 27.63L32.7895 21.12Z"
                        fill="#ffffff"
                    />
                </svg>
            </ArrowButtonContainer>
        );
    }
}

class ControlBar extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            prevStep: 0,
            animating: false,
        };
        this.totalSteps = 3;
        this.onPressNext = this.onPressNext.bind(this);
    }
    renderCounter() {
        const { currentStep } = this.props;
        const { animating } = this.state;
        const transition = { type: "spring", damping: 300, mass: 2 };
        return (
            <CounterContainer>
                <CurrentPageContainer>
                    {animating && (
                        <Page
                            key={currentStep}
                            animate={{ y: `-100%` }}
                            transition={transition}
                        >
                            {currentStep}
                        </Page>
                    )}
                    {animating && (
                        <Page
                            key={currentStep + 1}
                            initial={{ y: `100%` }}
                            animate={{ y: 0 }}
                            onAnimationComplete={() => {
                                this.setState({ animating: false });
                            }}
                            transition={transition}
                        >
                            {currentStep + 1}
                        </Page>
                    )}
                    {!animating && <Page>{currentStep + 1}</Page>}
                </CurrentPageContainer>
                <TotalSteps>/{this.totalSteps}</TotalSteps>
            </CounterContainer>
        );
    }

    onPressNext() {
        const { currentStep, onboardingNextStep, onboardingFinish } = this.props;
        if (currentStep < 2) {
            onboardingNextStep();
        } else {
            setOnboardingState(true);
            onboardingFinish();
        }
    }

    render() {
        return (
            <Container>
                {this.renderCounter()}
                <ArrowButton onClick={this.onPressNext}></ArrowButton>
            </Container>
        );
    }
    componentDidUpdate(prevProps) {
        if (prevProps.currentStep === this.props.currentStep) return;
        this.setState({ animating: true, prevStep: prevProps.currentStep });
    }
}

const mapStateToProps = (state) => {
    return {
        ...state.state.onboarding,
    };
};

const mapDispatchToProps = (dispatch) => {
    return bindActionCreators({ onboardingNextStep, onboardingFinish }, dispatch);
};
export default connect(mapStateToProps, mapDispatchToProps)(ControlBar);
