/*
    FilterEQ GUI - Interactive frequency response display with dark mode styling

    Features:
    - Visual frequency response display with logarithmic frequency scale
    - Interactive handles for adjusting filter parameters
    - Draggable parameter values
    - Dark mode with pastel colors for each filter
    - Enable/disable buttons for each filter
*/

class FilterEQGUI extends HTMLElement
{
    constructor(patchConnection)
    {
        super();
        this.patchConnection = patchConnection;
        this.attachShadow({ mode: 'open' });
        this.parameters = {};
        this.canvas = null;
        this.ctx = null;
        this.isDragging = false;
        this.dragFilter = null;
        this.dragParam = null;
        this.dragHandle = null;
        this.isHandleDragging = false;

        // Filter colors (pastel colors for dark mode)
        this.colors = {
            background: '#1a1a1a',
            grid: '#333333',
            text: '#e0e0e0',
            highpass: '#ff9999',
            peak1: '#99ffcc',
            peak2: '#ffcc99',
            peak3: '#cc99ff',
            lowpass: '#99ccff',
            combined: '#ffffff'
        };

        this.setupUI();
        this.setupEventListeners();
    }

    setupUI()
    {
        this.shadowRoot.innerHTML = `
            <style>
                * {
                    margin: 0;
                    padding: 0;
                    box-sizing: border-box;
                    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                }

                .container {
                    width: 800px;
                    height: 600px;
                    background: ${this.colors.background};
                    color: ${this.colors.text};
                    display: flex;
                    flex-direction: column;
                    padding: 20px;
                    overflow: hidden;
                }

                .title {
                    text-align: center;
                    font-size: 24px;
                    font-weight: bold;
                    margin-bottom: 20px;
                    color: ${this.colors.text};
                }

                .main-content {
                    display: flex;
                    flex-direction: column;
                    flex: 1;
                    gap: 20px;
                }

                .frequency-response {
                    height: 350px;
                    position: relative;
                }

                #responseCanvas {
                    width: 100%;
                    height: 100%;
                    border: 1px solid ${this.colors.grid};
                    background: #222222;
                    cursor: crosshair;
                }

                .controls {
                    display: flex;
                    flex-direction: row;
                    gap: 15px;
                    flex-wrap: wrap;
                    justify-content: space-between;
                }

                .filter-group {
                    padding: 12px;
                    border: 1px solid ${this.colors.grid};
                    border-radius: 6px;
                    background: rgba(255, 255, 255, 0.05);
                    flex: 1;
                    min-width: 140px;
                    max-width: 180px;
                }

                .filter-header {
                    display: flex;
                    justify-content: space-between;
                    align-items: center;
                    margin-bottom: 8px;
                }

                .filter-title {
                    font-weight: bold;
                    font-size: 14px;
                }

                .enable-button {
                    width: 50px;
                    height: 24px;
                    border: none;
                    border-radius: 12px;
                    cursor: pointer;
                    font-size: 11px;
                    font-weight: bold;
                    transition: all 0.2s;
                }

                .enable-button.enabled {
                    background: #4CAF50;
                    color: white;
                }

                .enable-button.disabled {
                    background: #666;
                    color: #ccc;
                }

                .param-row {
                    display: flex;
                    justify-content: space-between;
                    align-items: center;
                    margin-bottom: 4px;
                    font-size: 12px;
                }

                .param-label {
                    color: #ccc;
                }

                .param-value {
                    background: #333;
                    color: white;
                    padding: 2px 6px;
                    border-radius: 3px;
                    user-select: none;
                    min-width: 50px;
                    text-align: center;
                }
                
                .param-value[data-param*="Frequency"], .param-value[data-param*="Q"], .param-value[data-param*="Gain"] {
                    cursor: ns-resize;
                }

                .param-value:hover {
                    background: #444;
                }

                .highpass .filter-title { color: ${this.colors.highpass}; }
                .peak1 .filter-title { color: ${this.colors.peak1}; }
                .peak2 .filter-title { color: ${this.colors.peak2}; }
                .peak3 .filter-title { color: ${this.colors.peak3}; }
                .lowpass .filter-title { color: ${this.colors.lowpass}; }
            </style>

            <div class="container">
                <div class="title">FilterEQ</div>
                <div class="main-content">
                    <div class="frequency-response">
                        <canvas id="responseCanvas"></canvas>
                    </div>
                    <div class="controls">
                        <div class="filter-group highpass">
                            <div class="filter-header">
                                <span class="filter-title">High Pass</span>
                                <button class="enable-button enabled" data-filter="hp">ON</button>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Freq:</span>
                                <span class="param-value" data-param="hpFrequency">80 Hz</span>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Q:</span>
                                <span class="param-value" data-param="hpQ">0.71</span>
                            </div>
                        </div>

                        <div class="filter-group peak1">
                            <div class="filter-header">
                                <span class="filter-title">Peak 1</span>
                                <button class="enable-button enabled" data-filter="peak1">ON</button>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Freq:</span>
                                <span class="param-value" data-param="peak1Frequency">200 Hz</span>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Gain:</span>
                                <span class="param-value" data-param="peak1Gain">0.0 dB</span>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Q:</span>
                                <span class="param-value" data-param="peak1Q">1.0</span>
                            </div>
                        </div>

                        <div class="filter-group peak2">
                            <div class="filter-header">
                                <span class="filter-title">Peak 2</span>
                                <button class="enable-button enabled" data-filter="peak2">ON</button>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Freq:</span>
                                <span class="param-value" data-param="peak2Frequency">1000 Hz</span>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Gain:</span>
                                <span class="param-value" data-param="peak2Gain">0.0 dB</span>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Q:</span>
                                <span class="param-value" data-param="peak2Q">1.0</span>
                            </div>
                        </div>

                        <div class="filter-group peak3">
                            <div class="filter-header">
                                <span class="filter-title">Peak 3</span>
                                <button class="enable-button enabled" data-filter="peak3">ON</button>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Freq:</span>
                                <span class="param-value" data-param="peak3Frequency">5000 Hz</span>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Gain:</span>
                                <span class="param-value" data-param="peak3Gain">0.0 dB</span>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Q:</span>
                                <span class="param-value" data-param="peak3Q">1.0</span>
                            </div>
                        </div>

                        <div class="filter-group lowpass">
                            <div class="filter-header">
                                <span class="filter-title">Low Pass</span>
                                <button class="enable-button enabled" data-filter="lp">ON</button>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Freq:</span>
                                <span class="param-value" data-param="lpFrequency">10000 Hz</span>
                            </div>
                            <div class="param-row">
                                <span class="param-label">Q:</span>
                                <span class="param-value" data-param="lpQ">0.71</span>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        `;

        // Get canvas and set up drawing context
        this.canvas = this.shadowRoot.getElementById('responseCanvas');
        this.ctx = this.canvas.getContext('2d');
    }

    setupCanvas()
    {
        if (!this.canvas) return;

        const rect = this.canvas.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;

        // If canvas has no dimensions, use fallback dimensions
        const width = rect.width > 0 ? rect.width : 740;
        const height = rect.height > 0 ? rect.height : 350;

        this.canvas.width = width * dpr;
        this.canvas.height = height * dpr;

        // Reset transform and apply new scaling
        this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        this.canvas.style.width = width + 'px';
        this.canvas.style.height = height + 'px';

        this.drawFrequencyResponse();
    }

    setupEventListeners()
    {
        // Enable/disable buttons
        const enableButtons = this.shadowRoot.querySelectorAll('.enable-button');
        enableButtons.forEach(button => {
            button.addEventListener('click', (e) => this.toggleFilter(e));
        });

        // Parameter value dragging
        const paramValues = this.shadowRoot.querySelectorAll('.param-value');
        paramValues.forEach(value => {
            value.addEventListener('mousedown', (e) => this.startParamDrag(e));
        });

        // Canvas interaction for frequency response handles
        this.canvas.addEventListener('mousedown', (e) => this.startCanvasDrag(e));
        this.canvas.addEventListener('mousemove', (e) => this.handleCanvasMove(e));
        this.canvas.addEventListener('mouseup', (e) => this.endDrag(e));
        this.canvas.addEventListener('mouseleave', (e) => this.endDrag(e));

        // Global mouse events for parameter dragging
        document.addEventListener('mousemove', (e) => this.handleParamDrag(e));
        document.addEventListener('mouseup', (e) => this.endParamDrag(e));

        // Resize observer
        const resizeObserver = new ResizeObserver(() => {
            this.setupCanvas();
        });
        resizeObserver.observe(this.canvas);
    }

    toggleFilter(event)
    {
        const button = event.target;
        const filterType = button.dataset.filter;
        const isEnabled = button.classList.contains('enabled');

        if (isEnabled) {
            button.classList.remove('enabled');
            button.classList.add('disabled');
            button.textContent = 'OFF';
        } else {
            button.classList.remove('disabled');
            button.classList.add('enabled');
            button.textContent = 'ON';
        }

        // Send parameter update to processor
        const paramName = filterType + 'Enable';
        this.sendParameterUpdate(paramName, !isEnabled);

        this.drawFrequencyResponse();
    }

    startParamDrag(event)
    {
        event.preventDefault();
        this.isDragging = true;
        this.dragParam = event.target.dataset.param;
        this.dragStartX = event.clientX;
        this.dragStartY = event.clientY;
        this.dragStartValue = this.getCurrentParameterValue(this.dragParam);

        // All parameters now use vertical drag
        document.body.style.cursor = 'ns-resize';
    }

    handleParamDrag(event)
    {
        if (!this.isDragging || !this.dragParam) return;

        // Use vertical drag for all parameters (negative because Y increases downward)
        const delta = -(event.clientY - this.dragStartY);

        let newValue;
        
        if (this.dragParam.includes('Frequency')) {
            // Use exponential response for frequency parameters
            newValue = this.calculateExponentialFrequency(this.dragParam, delta);
        } else {
            // Use linear response for Q and Gain parameters
            const sensitivity = this.getParameterSensitivity(this.dragParam);
            newValue = this.dragStartValue + (delta * sensitivity);
        }

        const clampedValue = this.clampParameterValue(this.dragParam, newValue);

        this.updateParameterValue(this.dragParam, clampedValue);
        this.sendParameterUpdate(this.dragParam, clampedValue);
        this.drawFrequencyResponse();
    }

    endParamDrag(event)
    {
        if (this.isDragging) {
            this.isDragging = false;
            this.dragParam = null;
            document.body.style.cursor = 'default';
        }
    }

    startCanvasDrag(event)
    {
        const rect = this.canvas.getBoundingClientRect();
        const x = event.clientX - rect.left;
        const y = event.clientY - rect.top;
        
        // Find if we clicked on a handle
        const clickedHandle = this.findHandleAtPosition(x, y);
        
        if (clickedHandle) {
            event.preventDefault();
            this.isHandleDragging = true;
            this.dragHandle = clickedHandle;
            this.dragStartX = event.clientX;
            this.dragStartY = event.clientY;
            this.dragStartFrequency = this.getFilterFrequency(clickedHandle);
            this.dragStartGain = this.getFilterGain(clickedHandle);
            
            document.body.style.cursor = 'move';
        }
    }

    handleCanvasMove(event)
    {
        if (!this.isHandleDragging || !this.dragHandle) {
            // Update cursor based on hover state
            const rect = this.canvas.getBoundingClientRect();
            const x = event.clientX - rect.left;
            const y = event.clientY - rect.top;
            const hoveredHandle = this.findHandleAtPosition(x, y);
            
            this.canvas.style.cursor = hoveredHandle ? 'move' : 'crosshair';
            return;
        }

        // Handle dragging
        const rect = this.canvas.getBoundingClientRect();
        const canvasWidth = rect.width;
        const canvasHeight = rect.height;
        
        const currentX = event.clientX - rect.left;
        const currentY = event.clientY - rect.top;
        
        // Convert mouse position to frequency and gain
        const newFrequency = this.xToFrequency(currentX, canvasWidth);
        const newGain = this.yToGain(currentY, canvasHeight);
        
        // Update parameters based on filter type
        this.updateHandleParameters(this.dragHandle, newFrequency, newGain);
        
        this.drawFrequencyResponse();
    }

    endDrag(event)
    {
        if (this.isHandleDragging && this.dragHandle) {
            this.isHandleDragging = false;
            this.dragHandle = null;
            document.body.style.cursor = 'default';
        }
    }

    getCurrentParameterValue(paramName)
    {
        // Default parameter values
        const defaults = {
            'hpFrequency': 80,
            'hpQ': 0.707,
            'peak1Frequency': 200,
            'peak1Gain': 0,
            'peak1Q': 1.0,
            'peak2Frequency': 1000,
            'peak2Gain': 0,
            'peak2Q': 1.0,
            'peak3Frequency': 5000,
            'peak3Gain': 0,
            'peak3Q': 1.0,
            'lpFrequency': 10000,
            'lpQ': 0.707
        };

        return this.parameters[paramName] || defaults[paramName];
    }

    getParameterSensitivity(paramName)
    {
        const sensitivities = {
            'hpFrequency': 0.5,
            'hpQ': 0.01,
            'peak1Frequency': 2.0,
            'peak1Gain': 0.1,
            'peak1Q': 0.01,
            'peak2Frequency': 5.0,
            'peak2Gain': 0.1,
            'peak2Q': 0.01,
            'peak3Frequency': 10.0,
            'peak3Gain': 0.1,
            'peak3Q': 0.01,
            'lpFrequency': 10.0,
            'lpQ': 0.01
        };

        return sensitivities[paramName] || 1.0;
    }

    clampParameterValue(paramName, value)
    {
        const ranges = {
            'hpFrequency': [10, 1000],
            'hpQ': [0.1, 10],
            'peak1Frequency': [20, 20000],
            'peak1Gain': [-20, 20],
            'peak1Q': [0.1, 10],
            'peak2Frequency': [20, 20000],
            'peak2Gain': [-20, 20],
            'peak2Q': [0.1, 10],
            'peak3Frequency': [20, 20000],
            'peak3Gain': [-20, 20],
            'peak3Q': [0.1, 10],
            'lpFrequency': [1000, 20000],
            'lpQ': [0.1, 10]
        };

        const range = ranges[paramName];
        if (range) {
            return Math.max(range[0], Math.min(range[1], value));
        }
        return value;
    }

    calculateExponentialFrequency(paramName, delta)
    {
        // Get parameter range for exponential scaling
        const ranges = {
            'hpFrequency': [10, 1000],
            'peak1Frequency': [20, 20000],
            'peak2Frequency': [20, 20000],
            'peak3Frequency': [20, 20000],
            'lpFrequency': [1000, 20000]
        };
        
        const range = ranges[paramName];
        if (!range) return this.dragStartValue;
        
        const [minFreq, maxFreq] = range;
        
        // Convert current frequency to logarithmic position (0-1)
        const logMin = Math.log10(minFreq);
        const logMax = Math.log10(maxFreq);
        const currentLogPos = (Math.log10(this.dragStartValue) - logMin) / (logMax - logMin);
        
        // Apply delta with exponential sensitivity
        // Smaller sensitivity for more precise control across the range
        const sensitivity = 0.003; // Adjust this value to control drag sensitivity
        const newLogPos = Math.max(0, Math.min(1, currentLogPos + (delta * sensitivity)));
        
        // Convert back to frequency
        const newLogFreq = logMin + (newLogPos * (logMax - logMin));
        return Math.pow(10, newLogFreq);
    }

    updateParameterValue(paramName, value)
    {
        this.parameters[paramName] = value;
        const element = this.shadowRoot.querySelector(`[data-param="${paramName}"]`);
        if (element) {
            let displayValue;
            if (paramName.includes('Frequency')) {
                displayValue = Math.round(value) + ' Hz';
            } else if (paramName.includes('Gain')) {
                displayValue = value.toFixed(1) + ' dB';
            } else {
                displayValue = value.toFixed(2);
            }
            element.textContent = displayValue;
        }
    }

    sendParameterUpdate(paramName, value)
    {
        // Send parameter to the Cmajor processor
        if (this.patchConnection) {
            this.patchConnection.sendEventOrValue(paramName, value);
        }
    }

    // Frequency response calculation and drawing
    drawFrequencyResponse()
    {
        if (!this.canvas || !this.ctx) return;

        const width = this.canvas.width / (window.devicePixelRatio || 1);
        const height = this.canvas.height / (window.devicePixelRatio || 1);

        if (width <= 0 || height <= 0) return;

        // Clear canvas
        this.ctx.fillStyle = this.colors.background;
        this.ctx.fillRect(0, 0, width, height);

        // Draw grid
        this.drawGrid(width, height);

        // Draw individual filter responses with transparency
        this.drawIndividualFilters(width, height);

        // Draw combined response
        this.drawCombinedResponse(width, height);

        // Draw frequency and gain labels
        this.drawLabels(width, height);
        
        // Draw interactive handles
        this.drawHandles(width, height);
    }

    drawGrid(width, height)
    {
        this.ctx.strokeStyle = this.colors.grid;
        this.ctx.lineWidth = 1;

        // Vertical lines (frequency)
        const frequencies = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
        frequencies.forEach(freq => {
            const x = this.frequencyToX(freq, width);
            this.ctx.beginPath();
            this.ctx.moveTo(x, 0);
            this.ctx.lineTo(x, height);
            this.ctx.stroke();
        });

        // Horizontal lines (gain)
        const gains = [-20, -10, 0, 10, 20];
        gains.forEach(gain => {
            const y = this.gainToY(gain, height);
            this.ctx.beginPath();
            this.ctx.moveTo(0, y);
            this.ctx.lineTo(width, y);
            this.ctx.stroke();
        });
    }

    drawIndividualFilters(width, height)
    {
        // Draw each filter with transparency to show individual contributions
        const filters = [
            { name: 'hp', color: this.colors.highpass, enabled: this.isFilterEnabled('hp') },
            { name: 'peak1', color: this.colors.peak1, enabled: this.isFilterEnabled('peak1') },
            { name: 'peak2', color: this.colors.peak2, enabled: this.isFilterEnabled('peak2') },
            { name: 'peak3', color: this.colors.peak3, enabled: this.isFilterEnabled('peak3') },
            { name: 'lp', color: this.colors.lowpass, enabled: this.isFilterEnabled('lp') }
        ];

        filters.forEach(filter => {
            if (filter.enabled) {
                this.ctx.strokeStyle = filter.color;
                this.ctx.globalAlpha = 0.6;
                this.ctx.lineWidth = 2;
                this.drawFilterResponse(filter.name, width, height);
            }
        });

        this.ctx.globalAlpha = 1.0;
    }

    drawCombinedResponse(width, height)
    {
        this.ctx.strokeStyle = this.colors.combined;
        this.ctx.lineWidth = 3;

        this.ctx.beginPath();

        for (let x = 0; x < width; x++) {
            const freq = this.xToFrequency(x, width);
            const gain = this.calculateCombinedResponse(freq);
            const y = this.gainToY(gain, height);

            if (x === 0) {
                this.ctx.moveTo(x, y);
            } else {
                this.ctx.lineTo(x, y);
            }
        }

        this.ctx.stroke();
    }

    drawFilterResponse(filterName, width, height)
    {
        this.ctx.beginPath();

        for (let x = 0; x < width; x++) {
            const freq = this.xToFrequency(x, width);
            const gain = this.calculateFilterResponse(filterName, freq);
            const y = this.gainToY(gain, height);

            if (x === 0) {
                this.ctx.moveTo(x, y);
            } else {
                this.ctx.lineTo(x, y);
            }
        }

        this.ctx.stroke();
    }

    drawLabels(width, height)
    {
        this.ctx.fillStyle = this.colors.text;
        this.ctx.font = '12px Arial';

        // Frequency labels
        const frequencies = [20, 100, 1000, 10000];
        frequencies.forEach(freq => {
            const x = this.frequencyToX(freq, width);
            const label = freq >= 1000 ? (freq / 1000) + 'k' : freq.toString();
            this.ctx.fillText(label, x - 10, height - 5);
        });

        // Gain labels
        const gains = [-20, -10, 0, 10, 20];
        gains.forEach(gain => {
            const y = this.gainToY(gain, height);
            this.ctx.fillText(gain + 'dB', 5, y - 3);
        });
    }

    drawHandles(width, height)
    {
        const handleRadius = 6;
        const filters = [
            { name: 'hp', color: this.colors.highpass, enabled: this.isFilterEnabled('hp'), hasGain: false },
            { name: 'peak1', color: this.colors.peak1, enabled: this.isFilterEnabled('peak1'), hasGain: true },
            { name: 'peak2', color: this.colors.peak2, enabled: this.isFilterEnabled('peak2'), hasGain: true },
            { name: 'peak3', color: this.colors.peak3, enabled: this.isFilterEnabled('peak3'), hasGain: true },
            { name: 'lp', color: this.colors.lowpass, enabled: this.isFilterEnabled('lp'), hasGain: false }
        ];

        filters.forEach(filter => {
            if (!filter.enabled) return;

            const freq = this.getFilterFrequency(filter.name);
            const gain = filter.hasGain ? this.getFilterGain(filter.name) : 0;
            
            const x = this.frequencyToX(freq, width);
            const y = this.gainToY(gain, height);

            // Draw handle circle
            this.ctx.fillStyle = filter.color;
            this.ctx.strokeStyle = '#ffffff';
            this.ctx.lineWidth = 2;
            
            this.ctx.beginPath();
            this.ctx.arc(x, y, handleRadius, 0, 2 * Math.PI);
            this.ctx.fill();
            this.ctx.stroke();

            // Add a small center dot for better visibility
            this.ctx.fillStyle = '#ffffff';
            this.ctx.beginPath();
            this.ctx.arc(x, y, 2, 0, 2 * Math.PI);
            this.ctx.fill();
        });
    }

    getFilterFrequency(filterName)
    {
        switch (filterName) {
            case 'hp': return this.getCurrentParameterValue('hpFrequency');
            case 'peak1': return this.getCurrentParameterValue('peak1Frequency');
            case 'peak2': return this.getCurrentParameterValue('peak2Frequency');
            case 'peak3': return this.getCurrentParameterValue('peak3Frequency');
            case 'lp': return this.getCurrentParameterValue('lpFrequency');
            default: return 1000;
        }
    }

    getFilterGain(filterName)
    {
        switch (filterName) {
            case 'peak1': return this.getCurrentParameterValue('peak1Gain');
            case 'peak2': return this.getCurrentParameterValue('peak2Gain');
            case 'peak3': return this.getCurrentParameterValue('peak3Gain');
            default: return 0;
        }
    }

    findHandleAtPosition(x, y)
    {
        const handleRadius = 8; // Slightly larger for easier clicking
        const canvasWidth = this.canvas.width / (window.devicePixelRatio || 1);
        const canvasHeight = this.canvas.height / (window.devicePixelRatio || 1);
        
        const filters = ['hp', 'peak1', 'peak2', 'peak3', 'lp'];
        
        for (const filterName of filters) {
            if (!this.isFilterEnabled(filterName)) continue;
            
            const freq = this.getFilterFrequency(filterName);
            const gain = filterName.includes('peak') ? this.getFilterGain(filterName) : 0;
            
            const handleX = this.frequencyToX(freq, canvasWidth);
            const handleY = this.gainToY(gain, canvasHeight);
            
            const distance = Math.sqrt((x - handleX) ** 2 + (y - handleY) ** 2);
            
            if (distance <= handleRadius) {
                return filterName;
            }
        }
        
        return null;
    }

    updateHandleParameters(filterName, frequency, gain)
    {
        // Clamp frequency to valid ranges
        const frequencyRanges = {
            'hp': [10, 1000],
            'peak1': [20, 20000],
            'peak2': [20, 20000],
            'peak3': [20, 20000],
            'lp': [1000, 20000]
        };
        
        const freqRange = frequencyRanges[filterName];
        if (freqRange) {
            frequency = Math.max(freqRange[0], Math.min(freqRange[1], frequency));
        }
        
        // Clamp gain for peak filters
        if (filterName.includes('peak')) {
            gain = Math.max(-20, Math.min(20, gain));
        }
        
        // Update frequency parameter
        const freqParamName = filterName + 'Frequency';
        this.updateParameterValue(freqParamName, frequency);
        this.sendParameterUpdate(freqParamName, frequency);
        
        // Update gain parameter for peak filters
        if (filterName.includes('peak')) {
            const gainParamName = filterName + 'Gain';
            this.updateParameterValue(gainParamName, gain);
            this.sendParameterUpdate(gainParamName, gain);
        }
    }

    // Helper functions for coordinate conversion
    frequencyToX(freq, width)
    {
        const logFreq = Math.log10(freq);
        const logMin = Math.log10(20);
        const logMax = Math.log10(20000);
        return ((logFreq - logMin) / (logMax - logMin)) * width;
    }

    xToFrequency(x, width)
    {
        const logMin = Math.log10(20);
        const logMax = Math.log10(20000);
        const logFreq = logMin + (x / width) * (logMax - logMin);
        return Math.pow(10, logFreq);
    }

    gainToY(gain, height)
    {
        const minGain = -24;
        const maxGain = 24;
        return height - ((gain - minGain) / (maxGain - minGain)) * height;
    }

    yToGain(y, height)
    {
        const minGain = -24;
        const maxGain = 24;
        return minGain + ((height - y) / height) * (maxGain - minGain);
    }

    // Filter response calculations
    calculateFilterResponse(filterName, frequency)
    {
        // Simplified filter response calculations for visualization
        // These are approximations for display purposes

        switch (filterName) {
            case 'hp':
                return this.calculateHighPassResponse(frequency);
            case 'peak1':
                return this.calculatePeakResponse(frequency, 'peak1');
            case 'peak2':
                return this.calculatePeakResponse(frequency, 'peak2');
            case 'peak3':
                return this.calculatePeakResponse(frequency, 'peak3');
            case 'lp':
                return this.calculateLowPassResponse(frequency);
            default:
                return 0;
        }
    }

    calculateHighPassResponse(frequency)
    {
        const cutoff = this.getCurrentParameterValue('hpFrequency');
        const q = this.getCurrentParameterValue('hpQ');

        // Proper second-order high-pass filter response calculation
        const w = frequency / cutoff;
        const w2 = w * w;
        const qInv = 1.0 / q;

        // Transfer function magnitude: |H(jw)| = w^2 / sqrt((1-w^2)^2 + (w/Q)^2)
        const denominator = Math.sqrt(Math.pow(1 - w2, 2) + Math.pow(w * qInv, 2));
        const magnitude = w2 / denominator;

        // Convert to dB
        return 20 * Math.log10(Math.max(magnitude, 1e-6));
    }

    calculateLowPassResponse(frequency)
    {
        const cutoff = this.getCurrentParameterValue('lpFrequency');
        const q = this.getCurrentParameterValue('lpQ');

        // Proper second-order low-pass filter response calculation
        const w = frequency / cutoff;
        const w2 = w * w;
        const qInv = 1.0 / q;

        // Transfer function magnitude: |H(jw)| = 1 / sqrt((1-w^2)^2 + (w/Q)^2)
        const denominator = Math.sqrt(Math.pow(1 - w2, 2) + Math.pow(w * qInv, 2));
        const magnitude = 1.0 / denominator;

        // Convert to dB
        return 20 * Math.log10(Math.max(magnitude, 1e-6));
    }

    calculatePeakResponse(frequency, peakName)
    {
        const centerFreq = this.getCurrentParameterValue(peakName + 'Frequency');
        const gain = this.getCurrentParameterValue(peakName + 'Gain');
        const q = this.getCurrentParameterValue(peakName + 'Q');

        const ratio = frequency / centerFreq;
        const bandwidth = 1 / q;
        const response = gain / (1 + Math.pow((ratio - 1/ratio) / bandwidth, 2));

        return response;
    }

    calculateCombinedResponse(frequency)
    {
        let totalGain = 0;

        if (this.isFilterEnabled('hp')) {
            totalGain += this.calculateHighPassResponse(frequency);
        }

        if (this.isFilterEnabled('peak1')) {
            totalGain += this.calculatePeakResponse(frequency, 'peak1');
        }

        if (this.isFilterEnabled('peak2')) {
            totalGain += this.calculatePeakResponse(frequency, 'peak2');
        }

        if (this.isFilterEnabled('peak3')) {
            totalGain += this.calculatePeakResponse(frequency, 'peak3');
        }

        if (this.isFilterEnabled('lp')) {
            totalGain += this.calculateLowPassResponse(frequency);
        }

        return totalGain;
    }

    isFilterEnabled(filterName)
    {
        const button = this.shadowRoot.querySelector(`[data-filter="${filterName}"]`);
        return button && button.classList.contains('enabled');
    }

    // Cmajor integration
    connectedCallback()
    {
        // Called when the element is added to the DOM
        // Now we can set up the canvas since the element is in the DOM
        this.setupCanvas();

        if (this.patchConnection) {
            this.setupCmajorConnection();
        }

        // Force a redraw after a short delay to ensure canvas is ready
        setTimeout(() => {
            this.setupCanvas();
        }, 100);
    }

    setupCmajorConnection()
    {
        // Set up status listener to receive parameter updates
        this.statusListener = (status) => {
            this.status = status;
            this.drawFrequencyResponse();
        };

        this.patchConnection.addStatusListener(this.statusListener);
        this.patchConnection.requestStatusUpdate();

        // Set up parameter change listener if available
        if (this.patchConnection.addAllParameterListener) {
            this.patchConnection.addAllParameterListener((parameterUpdates) => {
                for (let update of parameterUpdates) {
                    this.updateParameterValue(update.endpointID, update.value);
                }
                this.drawFrequencyResponse();
            });
        }
    }
}

// Export function for Cmajor patch system
export default function createPatchView(patchConnection)
{
    const customElementName = "filtereq-gui";

    if (!window.customElements.get(customElementName))
        window.customElements.define(customElementName, FilterEQGUI);

    return new (window.customElements.get(customElementName))(patchConnection);
}