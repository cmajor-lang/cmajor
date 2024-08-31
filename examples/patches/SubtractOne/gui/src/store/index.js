import { applyMiddleware, createStore, combineReducers } from "redux";
import reducers, { defaultState } from "../reducers/reducers.js";

export const makeStore = ({ stateOverrides, middleware }) => createStore (
    combineReducers ({
        state: reducers,
    }),
    {
        state: {
            ...defaultState,
            ...stateOverrides,
        },
    },
    applyMiddleware (...middleware),
);
