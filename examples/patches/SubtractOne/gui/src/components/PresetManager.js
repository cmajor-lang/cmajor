import React from "react";
import styled from "styled-components";
import { savePreset, getPresets, clearPresets } from "../localStorage";
import { bindActionCreators } from "redux";
import { setParam, setPreset, loadPreset } from "../actions/actions.js";
import { connect, ReactReduxContext } from "react-redux";
import PresetButton from "../components/PresetButton";
import presets from "../presets";

const Container = styled.div`
    display: flex;
    padding: 10px;
`;

class PresetManager extends React.Component {
    static contextType = ReactReduxContext;

    constructor(props) {
        super(props);

        this.state = {
            presetName: props.meta.presetName,
            presets: [],
        };
    }
    componentDidMount() {
        this.fetch();
        window.addEventListener("hashchange", this.loadFromUrl.bind(this), false);

        this.loadFromUrl();
    }

    loadFromUrl(href) {
        if (window.location.hash) {
            var hash = decodeURIComponent(window.location.hash.substring(1)); //Puts hash in variable, and removes the # character
            var loadState = JSON.parse(hash);
            if (loadState) {
                console.log(loadState);
                this.props.setPreset(-1);

                this.props.loadPreset(loadState);
            }
        }
        window.dispatchEvent(new Event("popstate"));
    }
    onNameChange(e) {
        this.setState({ presetName: e.target.value });
    }

    onSave() {
        this.props.setParam("meta", false, "presetName", this.state.presetName);

        const state = this.context.store.getState();
        savePreset(state.state, this.state.presetName);
        this.fetch();
    }

    onClear() {
        let confirmed = window.confirm("DELETE ALL? SURE?");
        if (!confirmed) return;

        clearPresets();
        this.setState({ presetName: "" });
        this.fetch();
    }

    onExport() {
        var element = document.createElement("a");
        element.setAttribute(
            "href",
            "data:text/plain;charset=utf-8," +
                encodeURIComponent(JSON.stringify(this.state.presets))
        );
        element.setAttribute("download", "synth-presets");
        element.style.display = "none";
        document.body.appendChild(element);
        element.click();
        document.body.removeChild(element);
    }

    fetch() {
        console.log(getPresets());
        this.setState({ presets: getPresets() });
    }

    loadPreset(presetIndex) {
        this.props.loadPreset(presets[presetIndex]);
        this.props.setPreset(presetIndex);
        // this.setState({ presetName: preset.meta.presetName });
    }

    renderButtons() {
        let buttons = [];
        for (let i = 0; i < 8; i++) {
            buttons.push(
                <PresetButton
                    key={`button_${i}`}
                    onClick={() => {
                        this.loadPreset(i);
                    }}
                    preset={i}
                    active={this.props.meta.preset === i}
                />
            );
        }

        return buttons;
    }

    render() {
        return <Container>{this.renderButtons()}</Container>;
    }
}
const mapStateToProps = (state) => {
    return {
        ...state.state,
    };
};

const mapDispatchToProps = (dispatch) => {
    return bindActionCreators({ setParam, setPreset, loadPreset }, dispatch);
};

export default connect(mapStateToProps, mapDispatchToProps)(PresetManager);
