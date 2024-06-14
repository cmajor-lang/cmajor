import { useEffect, useState, useRef } from "react";
import { SpectrumAnalyzer } from "./SpectrumAnalyzer";
import "./index.css";

const SAMPLE_RATE = 44100;
const FREQUENCY_INTERVALS = [
  0, 29, 61, 115, 218, 411, 777, 1500, 2800, 5200, 11000,
];
const FILTER_FREQS = FREQUENCY_INTERVALS.slice(2, 10);

const UI = ({ amps, pans, handleSliderChange }) => {
  return (
    <div className="ui">
      {amps.map((amp, i) => (
        <div
          key={i}
          style={{
            position: "relative",
            left: `${(i / amps.length) * 76.8 + 21.75}%`,
          }}
        >
          <input
            className="freq-slider"
            type="range"
            value={amp}
            min={0.0}
            max={1.0}
            step={0.001}
            onChange={(e) => handleSliderChange("amp", i, e.target.value)}
            onDoubleClick={() => {
              handleSliderChange("amp", i, 1.0);
            }}
          />
          <input
            className="pan-slider"
            type="range"
            min={-1.0}
            max={1.0}
            step={0.001}
            value={pans[i]}
            onChange={(e) => handleSliderChange("pan", i, e.target.value)}
            onDoubleClick={() => {
              handleSliderChange("pan", i, 0.0);
            }}
          />
        </div>
      ))}
    </div>
  );
};

export default function View({ patchConnection }) {
  const [stateLoaded, setStateLoaded] = useState(false);
  const [fftData, setFFTData] = useState([]);
  const [amps, setAmps] = useState(Array(8).fill(0.0));
  const [pans, setPans] = useState(Array(8).fill(0.0));

  const updateValues = (type, index, value) => {
    if (type === "amp") {
      setAmps((prev) => prev.map((amp, i) => (i === index ? value : amp)));
    } else if (type === "pan") {
      setPans((prev) => prev.map((pan, i) => (i === index ? value : pan)));
    }
  };

  const handleSliderChange = (type, index, value) => {
    updateValues(type, index, value);
    patchConnection?.sendEventOrValue(`${type}Band${index}`, value, 100);
  };

  const handleDftOut = (event) => {
    setFFTData(event.magnitudes);
  };

  useEffect(() => {
    const requestParameterValues = (type) => {
      for (let i = 0; i < FILTER_FREQS.length; i++) {
        patchConnection?.requestParameterValue(`${type}Band${i}`);
      }
    };

    const handleControlChange = (event) => {
      const id = event.endpointID;
      const val = event.value;
      const index = parseInt(id.slice(-1));
      const type = id.slice(0, 3);

      updateValues(type, index, val);
    };

    // Add listeners for DFT output and input controls
    patchConnection?.addEndpointListener("dftOut", handleDftOut);
    patchConnection?.addAllParameterListener(handleControlChange);

    // Load initial state
    if (!stateLoaded) {
      requestParameterValues("amp");
      requestParameterValues("pan");

      setStateLoaded(true);
    }

    return () => {
      patchConnection?.removeEndpointListener("dftOut", handleDftOut);
      patchConnection?.removeAllParameterListener(handleControlChange);
    };
  }, [patchConnection, stateLoaded]);

  return (
    <div className="container">
      <div className="stack">
        <SpectrumAnalyzer
          className="analyzer"
          fftData={fftData}
          // TODO: this should be sent from the backend
          sampleRate={SAMPLE_RATE}
          frequencyIntervals={FREQUENCY_INTERVALS}
          filterFreqs={FILTER_FREQS}
          amps={amps}
        />
        <UI amps={amps} pans={pans} handleSliderChange={handleSliderChange} />
      </div>
    </div>
  );
}
