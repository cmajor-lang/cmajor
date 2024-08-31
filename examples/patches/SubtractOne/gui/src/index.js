import React from "react";
import ReactDOM from "react-dom";
import "./fonts/arimo/arimo.css";
import "./index.css";

import { Provider } from "react-redux";

(async () => {
    const { Root, store } = await require (`./environments/${process.env.REACT_APP_ENV}.js`).makeEnvironment();

    ReactDOM.render (
        <Provider store={store}>
            <Root />
        </Provider>,
        document.getElementById("root")
    );
})();