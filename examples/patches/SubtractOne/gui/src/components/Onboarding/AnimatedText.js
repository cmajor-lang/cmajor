import React from "react";
import styled from "styled-components";
import { motion, AnimatePresence } from "framer-motion";
import { connect } from "react-redux";
import { onboardingAnimationComplete } from "../../actions/actions.js";
import { bindActionCreators } from "redux";

const Headline = styled(motion.h1)`
    margin-bottom: 40px;
`;

const Paragraph = styled(motion.p)`
    font-family: Arimo;
    font-style: normal;
    font-weight: normal;
    font-size: 20px;
    line-height: 1.55;
    margin: 0;
    opacity: 1;
`;
const Container = styled.div``;

const HeadlineContainer = styled.div`
    position: relative;
`;

const ParagraphContainer = styled.div`
    position: relative;
    align-self: flex-start;
`;

const Bar = styled(motion.div)`
    background: white;
    position: absolute;
    left: 0;
    top: 0;
    right: 0;
    pointer-events: none;
    opacity: 1;
`;

const bar = {
    headline: 77,
    paragraph: 24,
    spacing: 7,
};

const animation = {
    offset: 0.06,
    speed: 0.5,
    ease: "easeInOut",
};

const ImageContainer = styled.div`
    position: relative;
    display: flex;
    align-self: flex-start;
`;

const Image = styled(motion.div)`
    display: flex;
    flex-direction: column;
    text-align: center;
    margin-right: 90px;
    width: 200px;
    margin-top: 40px;

    img {
        width: 100%;
    }
`;
const ImageLabel = styled.div`
    margin-top: 40px;
    font-size: 16px;
    line-height: 145%;
`;

const ImageSmallLabel = styled.div`
    font-size: 12px;
    opacity: 0.6;
    margin-top: 4px;
`;

class AnimatedText extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            show: false,
            headlineBarCount: 0,
            paragraphBarCount: 0,
            finished: {
                paragraph: false,
                headline: false,
                images: this.props.images ? false : true,
            },
        };
    }

    isCurrent() {
        const { show } = this.props;
        return show && this.state.show;
    }

    componentDidUpdate(prevProps) {
        const { animating, show } = this.props;
        //when prev animation finished
        if (prevProps.animating !== animating && !animating && show) {
            this.show();
        }
    }

    componentDidMount() {
        if (this.props.show) this.show();
    }

    show() {
        this.setState({ show: true }, () => {
            //Headline
            const headlineHeight = this.headlineContainer.getBoundingClientRect()
                .height;
            const headlineBarCount = Math.ceil(
                headlineHeight / (bar.headline + bar.spacing + 0.5 * bar.spacing)
            );

            //Text
            const paragraphHeight = this.paragraphContainer.getBoundingClientRect()
                .height;
            const paragraphBarCount = Math.ceil(
                paragraphHeight / (bar.paragraph + bar.spacing + 0.5 * bar.spacing)
            );
            console.log(paragraphBarCount);

            this.setState({ headlineBarCount, paragraphBarCount });
        });
    }

    setBars() {
        const { barHeight } = this.props;
        const containerHeight = this.container.getBoundingClientRect().height;
        const barCount = Math.ceil(
            containerHeight / (barHeight + bar.spacing + 0.5 * bar.spacing)
        );
        this.setState({ barCount });
    }

    barAnimationComplete(i) {
        if (!this.props.isVisible && i === this.state.barCount - 1) {
            alert(`done`);
        }
    }

    getTransition(count) {
        const restTime = animation.offset * count;
        const duration = animation.speed * 2 + restTime;
        const restRelative = restTime / duration;
        const inOutRelative = animation.speed / duration;
        const times = [0, inOutRelative, inOutRelative + restRelative, 1];

        return {
            ease: animation.ease,
            times,
            duration,
        };
    }

    getBars(height, count, callback) {
        const transition = this.getTransition(count);
        const finished = (i) => {
            if (i === count - 1 && !this.isCurrent()) {
                callback();
            }
        };

        return [...Array(count)].map((b, i) => {
            const top = i * (height + bar.spacing) + 0.5 * bar.spacing;
            const delay = i * animation.offset;

            return (
                <Bar
                    key={`bar_${i}`}
                    style={{ height: `${height}px`, top: `${top}px` }}
                    initial={{ left: `0%`, right: `100%` }}
                    animate={{
                        left: [`0%`, `0%`, `0%`, `100%`],
                        right: [`100%`, `0%`, `0%`, `0%`],
                    }}
                    exit={{
                        left: [`100%`, `0%`, `0%`, `0%`],
                        right: [`0%`, `0%`, `0%`, `100%`],
                    }}
                    transition={{ ...transition, delay }}
                    onAnimationComplete={() => {
                        finished(i);
                    }}
                ></Bar>
            );
        });
    }

    getText(content, wrapper, lineCount) {
        const transition = this.getTransition(lineCount);
        const delay = animation.offset * (lineCount - 1);
        const Wrapper = wrapper;

        return (
            <Wrapper
                ref={`text`}
                initial={{ opacity: 0 }}
                animate={{ opacity: [0, 0, 1, 1] }}
                exit={{ opacity: [1, 1, 0, 0] }}
                transition={{ ...transition, ease: "linear", delay }}
            >
                {content}
            </Wrapper>
        );
    }

    getImages() {
        const { images } = this.props;
        if (!images) return;
        return images.map((item, i) => (
            <Image
                key={i}
                initial={{ opacity: 0, y: 30 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: 30 }}
                transition={{
                    delay: 0.3 + i * 0.2,
                    y: { type: "spring", stiffness: 100 },
                }}
                onAnimationComplete={() => {
                    this.animationComplete("images");
                }}
            >
                <img alt={item.text} src={`./images/${item.image}`} />
                <ImageLabel>{item.text}</ImageLabel>
                {item.small && <ImageSmallLabel>{item.small}</ImageSmallLabel>}
            </Image>
        ));
    }

    animationComplete(element) {
        this.setState(
            {
                finished: {
                    ...this.state.finished,
                    [element]: true,
                },
            },
            () => {
                const { paragraph, headline, images } = this.state.finished;
                if (paragraph && headline && images) {
                    setTimeout(this.props.onboardingAnimationComplete, 300);
                }
            }
        );
    }

    render() {
        const { headlineBarCount, paragraphBarCount } = this.state;
        const { headline, children } = this.props;

        return (
            <Container>
                <HeadlineContainer
                    ref={(el) => {
                        this.headlineContainer = el;
                    }}
                >
                    <AnimatePresence>
                        {this.isCurrent() &&
                            this.getBars(bar.headline, headlineBarCount, () => {
                                this.animationComplete("headline");
                            })}
                        {this.isCurrent() &&
                            this.getText(headline, Headline, headlineBarCount)}
                    </AnimatePresence>
                </HeadlineContainer>
                <ParagraphContainer
                    ref={(el) => {
                        this.paragraphContainer = el;
                    }}
                >
                    <AnimatePresence>
                        {this.isCurrent() &&
                            this.getBars(bar.paragraph, paragraphBarCount, () => {
                                this.animationComplete("paragraph");
                            })}
                        {this.isCurrent() &&
                            this.getText(children, Paragraph, paragraphBarCount)}
                    </AnimatePresence>
                </ParagraphContainer>
                <ImageContainer>
                    <AnimatePresence>
                        {this.isCurrent() && this.getImages()}
                    </AnimatePresence>
                </ImageContainer>
            </Container>
        );
    }
}

const mapStateToProps = (state) => {
    return {
        ...state.state.onboarding,
    };
};
const mapDispatchToProps = (dispatch) => {
    return bindActionCreators({ onboardingAnimationComplete }, dispatch);
};

export default connect(mapStateToProps, mapDispatchToProps)(AnimatedText);
