import { useEffect, useRef } from "react";

export const SpectrumAnalyzer = ({
  fftData,
  sampleRate,
  frequencyIntervals,
}) => {
  const canvasRef = useRef(null);
  const requestRef = useRef(null);
  const prevFftDataRef = useRef([]);

  const smoothData = (currentData, prevData, smoothingFactor) => {
    return currentData.map((value, i) => {
      return prevData[i] * (1 - smoothingFactor) + value * smoothingFactor;
    });
  };

  const drawLines = (ctx, width, height) => {
    ctx.strokeStyle = "#BFBFBF";
    ctx.lineWidth = 1;
    ctx.beginPath();

    // Draw horizontal grid lines (linear scale for magnitude)
    const numHorizontalLines = 7;
    for (let i = 0; i <= numHorizontalLines; i++) {
      const y = (i / numHorizontalLines) * (height - 300) + 190;
      ctx.moveTo(0, y);
      ctx.lineTo(width, y);
    }

    ctx.stroke();
  };

  useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas.getContext("2d");

    if (!prevFftDataRef.current.length) {
      prevFftDataRef.current = new Array(fftData.length).fill(0);
    }

    const draw = () => {
      const width = canvas.width;
      const height = canvas.height;
      const barWidth = width / (frequencyIntervals.length - 1);

      // Clear canvas
      ctx.clearRect(0, 0, width, height);
      ctx.fillStyle = "#A0A0A0";
      ctx.fillRect(0, 0, width, height);

      // Draw grid
      drawLines(ctx, width, height);

      // Smooth FFT data
      const smoothedFftData = smoothData(fftData, prevFftDataRef.current, 0.2);

      // Draw spectrum
      ctx.strokeStyle = "black";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(0, height);

      frequencyIntervals.slice(0, -1).forEach((startFreq, i) => {
        const endFreq = frequencyIntervals[i + 1];
        const startIndex = Math.floor(
          (startFreq / (sampleRate / 2)) * smoothedFftData.length
        );
        const endIndex = Math.floor(
          (endFreq / (sampleRate / 2)) * smoothedFftData.length
        );

        for (let j = startIndex; j <= endIndex; j++) {
          const magnitude = smoothedFftData[j];
          const y = height - (Math.log10(magnitude + 1) / 6) * height * 2;
          const x =
            i * barWidth +
            ((j - startIndex) / (endIndex - startIndex)) * barWidth;
          ctx.lineTo(x, y);
        }
      });

      ctx.lineTo(width, height);
      ctx.stroke();

      // Draw frequency labels
      ctx.fillStyle = "white";
      ctx.font = "20px Arial";
      ctx.textAlign = "center";
      ctx.textBaseline = "top";

      const labelWidth = (width - 80) / (frequencyIntervals.length - 1);
      frequencyIntervals.forEach((freq, i) => {
        const labelX = i * labelWidth + 30;
        ctx.fillText(freq, labelX, height - 30);
      });

      prevFftDataRef.current = smoothedFftData;
      requestRef.current = requestAnimationFrame(draw);
    };

    requestRef.current = requestAnimationFrame(draw);

    return () => {
      cancelAnimationFrame(requestRef.current);
    };
  }, [fftData, sampleRate, frequencyIntervals]);

  return (
    <div>
      <canvas
        ref={canvasRef}
        width={2000}
        height={1000}
        style={{ width: "100%", height: "100%" }}
      />
    </div>
  );
};
