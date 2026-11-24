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

/*
    The EndpointDataHub sits between the panel's widgets and a PatchConnection. Widgets
    subscribe to the hub, never to the PatchConnection directly.

    - It keeps ONE server-side listener per (endpoint, granularity, fullAudioData), however many
      widgets are watching it, and removes it when the last one unsubscribes. (Every
      addEndpointListener() call creates a separate monitor on the server, so two widgets showing
      the same output would otherwise double the websocket traffic.)
    - The server carries its listeners over when the patch rebuilds (Patch::setNewRenderer() in
      cmaj_Patch.h), so a rebuild needs nothing from the hub except asking for the parameters'
      values again (handleNewBuild()).
    - It remembers the last value of each parameter, so a widget created later can show it
      straight away.
    - Parameters are watched with the connection's param_value events (which the server sends
      to everyone anyway), rather than with endpoint listeners.

        const hub = new EndpointDataHub (patchConnection);
        const unsubscribe = hub.subscribe ("out", data => ..., { granularity: 400 });
        const group = hub.createGroup();      // to pause a whole view at once
        group.subscribe (...); group.setActive (false);
*/
export class EndpointDataHub
{
    constructor (patchConnection)
    {
        this.patchConnection = patchConnection;
        this.feeds = new Map();             // key -> { endpointID, granularity, fullAudioData, listeners, fanOut }
        this.parameterFeeds = new Map();    // endpointID -> { listeners, handler, value, hasValue }
    }

    dispose()
    {
        for (const feed of this.feeds.values())
            this.patchConnection.removeEndpointListener (feed.endpointID, feed.fanOut);

        for (const [endpointID, feed] of this.parameterFeeds)
            this.patchConnection.removeParameterListener (endpointID, feed.handler);

        this.feeds.clear();
        this.parameterFeeds.clear();
    }

    //==============================================================================
    /// Subscribes to the data from an endpoint. For an audio endpoint, options.granularity is the
    /// number of frames per update and options.fullAudioData asks for samples rather than min/max.
    /// Returns a function that unsubscribes.
    subscribe (endpointID, listener, options = {})
    {
        const granularity = options.granularity ?? 0;
        const fullAudioData = !! options.fullAudioData;
        const key = `${endpointID}\u0000${granularity}\u0000${fullAudioData}`;
        let feed = this.feeds.get (key);

        if (! feed)
        {
            feed = { endpointID, granularity, fullAudioData, listeners: new Set() };
            feed.fanOut = (value) => this.deliver (feed, value);
            this.feeds.set (key, feed);
            this.patchConnection.addEndpointListener (endpointID, feed.fanOut, granularity || undefined, fullAudioData);
        }

        feed.listeners.add (listener);

        return () =>
        {
            if (feed.listeners.delete (listener) && feed.listeners.size === 0)
            {
                this.feeds.delete (key);
                this.patchConnection.removeEndpointListener (endpointID, feed.fanOut);
            }
        };
    }

    /// Subscribes to a parameter's value. The listener is called with the current value
    /// straight away if it's known, and the hub asks the server for it if not.
    subscribeParameter (endpointID, listener)
    {
        let feed = this.parameterFeeds.get (endpointID);

        if (! feed)
        {
            feed = { listeners: new Set(), value: undefined, hasValue: false };
            feed.handler = (value) =>
            {
                feed.value = value;
                feed.hasValue = true;

                for (const l of [...feed.listeners])
                {
                    try { l (value); }
                    catch (e) { console.error (e); }
                }
            };

            this.parameterFeeds.set (endpointID, feed);
            this.patchConnection.addParameterListener (endpointID, feed.handler);
            this.patchConnection.requestParameterValue (endpointID);
        }

        feed.listeners.add (listener);

        if (feed.hasValue)
            listener (feed.value);

        return () =>
        {
            if (feed.listeners.delete (listener) && feed.listeners.size === 0)
            {
                this.parameterFeeds.delete (endpointID);
                this.patchConnection.removeParameterListener (endpointID, feed.handler);
            }
        };
    }

    /// The number of server-side listeners the hub currently holds (for tests)
    get numServerListeners()                    { return this.feeds.size; }

    /// Returns a SubscriptionGroup, for a view to pause and resume all of its subscriptions
    createGroup()                               { return new SubscriptionGroup (this); }

    //==============================================================================
    /// Call when the patch has been rebuilt (the store's buildNumber changes): its parameters
    /// may have new values (e.g. a changed default), so this asks for them again. (The server
    /// keeps the endpoint listeners.)
    handleNewBuild()
    {
        for (const endpointID of this.parameterFeeds.keys())
            this.patchConnection.requestParameterValue (endpointID);
    }

    deliver (feed, value)
    {
        for (const l of [...feed.listeners])
        {
            try { l (value); }
            catch (e) { console.error (e); }
        }
    }
}

//==============================================================================
/// A set of subscriptions that can be switched on and off together, e.g. when a view is
/// hidden. While inactive, it holds no server listeners at all.
export class SubscriptionGroup
{
    constructor (hub)
    {
        this.hub = hub;
        this.items = new Set();
        this.isActive = true;
    }

    /// Like hub.subscribe(). Returns a function that removes the subscription from the group.
    subscribe (endpointID, listener, options)
    {
        return this.add ({ subscribeTo: (hub) => hub.subscribe (endpointID, listener, options) });
    }

    /// Like hub.subscribeParameter()
    subscribeParameter (endpointID, listener)
    {
        return this.add ({ subscribeTo: (hub) => hub.subscribeParameter (endpointID, listener) });
    }

    add (item)
    {
        this.items.add (item);

        if (this.isActive)
            item.unsubscribe = item.subscribeTo (this.hub);

        return () =>
        {
            item.unsubscribe?.();
            item.unsubscribe = null;
            this.items.delete (item);
        };
    }

    setActive (shouldBeActive)
    {
        if (this.isActive === shouldBeActive)
            return;

        this.isActive = shouldBeActive;

        for (const item of this.items)
        {
            if (shouldBeActive)
            {
                item.unsubscribe = item.subscribeTo (this.hub);
            }
            else
            {
                item.unsubscribe?.();
                item.unsubscribe = null;
            }
        }
    }

    dispose()
    {
        this.setActive (false);
        this.items.clear();
    }
}
