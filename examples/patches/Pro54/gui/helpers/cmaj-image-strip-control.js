//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  The Cmajor project is subject to commercial or open-source licensing.
//  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
//  visit https://cmajor.dev to learn about our commercial licence options.
//
//  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
//  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
//  DISCLAIMED.

export default class ImageStripControl extends HTMLElement
{
    constructor()
    {
        super();

        this.currentValue = 0;
        this.rangeMin       = this.getAttribute ("min-value");
        this.rangeMax       = this.getAttribute ("max-value");
        this.default        = this.getAttribute ("default-value");
        this.label          = this.getAttribute ("label");
        this.horizontalMode = this.getAttribute ("horizontalMode");

        this.addEventListener ('mousedown', this.startDrag);
        this.addEventListener ('touchstart', this.handleTouch);
        this.addEventListener ("dblclick", this.onReset);
    }

    setImage ({ imageURL, numImagesPerStrip, imageHeightPixels, sensitivity })
    {
        this.imageURL = imageURL;
        this.numImagesPerStrip = numImagesPerStrip;
        this.imageHeightPixels = imageHeightPixels;
        this.sensitivity = sensitivity;

        this.innerHTML = `<img draggable="false" class="strip" style="display:block; position: absolute;" src="${imageURL}"></img>`;
        this.imageStrip = this.children[0];
        this.updateKnobImage();
    }

    /// This updates the knob with a new value
    setCurrentValue (newValue)
    {
        this.currentValue = newValue;
        this.updateKnobImage();
    }

    /// These are called when the user drags the knob - override them to handle it
    onStartDrag() {}
    onEndDrag() {}
    onValueDragged (newValue)   {}
    onReset() {}

    updateKnobImage()
    {
        const proportion = (this.currentValue - this.rangeMin) / (this.rangeMax - this.rangeMin);
        const imageIndex = Math.max (0, Math.min (this.numImagesPerStrip - 1, Math.floor (this.numImagesPerStrip * proportion)));
        this.imageStrip.style.top = `${imageIndex * -this.imageHeightPixels}px`;
    }

    handleTouch (event)
    {
        this.dragStartValue = this.currentValue;
        this.dragStartPos = this.horizontalMode ? -event.changedTouches[0].clientX : event.changedTouches[0].clientY;
        this.dragging = true;
        this.touchIdentifier = event.changedTouches[0].identifier;

        const dragTo = (event) =>
        {
            for (const touch of event.changedTouches)
            {
                if (touch.identifier == this.touchIdentifier)
                {
                    let currentPos = this.horizontalMode ? -touch.clientX : touch.clientY;
                    const delta = currentPos - this.dragStartPos;
                    const deltaProportion = delta / -this.sensitivity;
                    const newValue = this.dragStartValue + deltaProportion * (this.rangeMax - this.rangeMin);
                    const clippedValue = Math.min (this.rangeMax, Math.max (this.rangeMin, newValue));
                    this.onValueDragged (clippedValue);
                }
            }
        }

        const endDrag = (event) =>
        {
            for (const touch of event.changedTouches)
            {
                if (touch.identifier == this.touchIdentifier)
                {
                    this.dragging = false;
                    this.onEndDrag();
                    window.removeEventListener('touchmove', dragTo);
                    window.removeEventListener('touchend', endDrag);
                    event.preventDefault();
                }
            }
        }

        this.onStartDrag();
        window.addEventListener('touchmove', dragTo);
        window.addEventListener('touchend', endDrag);
        event.preventDefault();
    }

    startDrag (event)
    {
        this.dragStartValue = this.currentValue;
        this.dragStartPos = this.horizontalMode ? -event.screenX  : event.screenY;
        this.dragging = true;

        const dragTo = (event) =>
        {
            let currentPos = this.horizontalMode ? -event.screenX : event.screenY;
            const delta = currentPos - this.dragStartPos;
            const deltaProportion = delta / -this.sensitivity;
            const newValue = this.dragStartValue + deltaProportion * (this.rangeMax - this.rangeMin);
            const clippedValue = Math.min (this.rangeMax, Math.max (this.rangeMin, newValue));
            this.onValueDragged (clippedValue);
            event.preventDefault();
        }

        const endDrag = (event) =>
        {
            this.dragging = false;
            this.onEndDrag();
            window.removeEventListener('mousemove', dragTo);
            window.removeEventListener('mouseup', endDrag);
            event.preventDefault();
        }

        this.onStartDrag();
        window.addEventListener('mousemove', dragTo);
        window.addEventListener('mouseup', endDrag);
        event.preventDefault();
    }

    static get observedAttributes()
    {
        return ["min-value", "max-value", "label"];
    }
}
