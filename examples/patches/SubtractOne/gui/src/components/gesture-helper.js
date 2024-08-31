export function attachGlobalMoveAndEndListenersToControl (control) {
    const { module, moduleIndex, param } = control.props;
    control.props.onBeginGesture (module, moduleIndex, param);

    attachGlobalMoveAndEndListeners ({
        onEdit: (event) => control.onMouseMove (event),
        onEndEdit: (event) => {
            control.onMouseUp (event);
            control.props.onEndGesture (module, moduleIndex, param);
        },
    });
}

function attachGlobalMoveAndEndListeners ({
    onEdit = () => {},
    onEndEdit = () => {},
} = {}) {
    const onEndGesture = (event) => {
        onEndEdit (event);

        window.removeEventListener ("mousemove", onEdit);
        window.removeEventListener ("touchmove", onEdit);
        window.removeEventListener ("mouseup", onEndGesture);
        window.removeEventListener ("touchend", onEndGesture);
    };

    window.addEventListener ("mousemove", onEdit);
    window.addEventListener ("touchmove", onEdit);
    window.addEventListener ("mouseup", onEndGesture);
    window.addEventListener ("touchend", onEndGesture);
}