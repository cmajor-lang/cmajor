import React from "react";
import { useEffect, useRef } from "react";
import styled from "styled-components";

import { connect } from "react-redux";

const Container = styled.div`
    padding: 4px;
    background: linear-gradient(
        180deg,
        rgba(36, 36, 36, 0.9) 0%,
        rgba(23, 23, 23, 0.9) 100%
    );
    border: 1px solid rgba(33, 33, 33, 0.4);
    border-radius: 6px;
    margin: 8px;
    width: auto;
    height: 110px;
    position: relative;
`;

const Display = styled.div`
    background: linear-gradient(180deg, #1e1e1e 0.01%, #101010 100%);
    border-radius: 2px;
    position: relative;
    height: 100%;
    & > * {
        opacity: ${(props) => (props.power === true ? 1 : 0)};
        transition: all 0.5s;
        transition-delay: 0.2s;
    }
`;

const Canvas = styled.canvas`
width:auto;
position:absolute;
top:0;
left:0
width:100%;
height:100%;
`;

function Oscilloscope(props) {
    const { dataArray, note, power, sampleRate } = props;
    const canvasRef = useRef();

    useEffect(() => {
        let canvas = canvasRef.current;
        let context = canvas.getContext("2d");

        context.clearRect(0, 0, canvas.width, canvas.height);
        if (!power) return;

        let requestId;

        const render = () => {
            context.clearRect(0, 0, canvas.width, canvas.height);

            var x = 0;

            context.lineWidth = 2;
            context.strokeStyle = "#11CD43";
            context.shadowOffsetX = 0;
            context.shadowOffsetY = 0;
            context.shadowBlur = 12;
            context.shadowColor = "rgba(17, 205, 67, 0.88)";
            context.beginPath();
            let period = (sampleRate / note.freq) * 3;

            /*
             const sampleTime = sampleRate * currentTime;
             const sampleOffset = sampleTime % period;
            */

            let last;
            let start = 0;
            //let end = dataArray.length;
            for (var i = 0; i < dataArray.length; i++) {
                if (last > 0 && dataArray[i] <= 0) {
                    start = i;
                    break;
                }

                /*if (start && last < 0 && dataArray[i] >= 0) {
                    end = i + (i - start) * 5
                    break;

                }*/
                last = dataArray[i];
            }

            // period = end - start
            let max = Math.max(...dataArray);
            max = max < 1 ? 1 : max;
            var sliceWidth = (canvas.width * 1) / period;

            for (var j = 0; j < period; j++) {
                var v = (dataArray[j + start] * 95) / (max * 2);
                var y = v + canvas.height / 2;
                x = sliceWidth * j;
                if (j === 0) {
                    context.moveTo(x, y);
                } else {
                    context.lineTo(x, y);
                }
            }
            context.stroke();

            requestId = requestAnimationFrame(render);
        };

        render();

        return () => {
            cancelAnimationFrame(requestId);
        };
    });

    return (
        <Container>
            <Display power={power}>
                <Canvas ref={canvasRef} />
            </Display>
        </Container>
    );
}

const mapStateToProps = (state) => {
    return {
        dataArray: state.state.oscilloscope.dataArray,
        fftSize: state.state.oscilloscope.fftSize,
        sampleRate: state.state.oscilloscope.sampleRate,
        currentTime: state.state.general.currentTime,
        note: state.state.keyboard.note,
        power: state.state.power.active,
    };
};

export default connect(mapStateToProps, null)(Oscilloscope);
