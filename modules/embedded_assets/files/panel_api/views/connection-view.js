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

import { Disposer } from "../core/dom.js";

/*
    The base class for a view that shows the loaded patch's endpoints: the I/O tab, the graph's
    live mode (LiveGraphLayer), the console and the inspector's parameters.

    It follows the store's connection and endpoint lists. Whenever they change it calls
    detach() then attach (connection), and between the two the view has a SubscriptionGroup
    from the patch's EndpointDataHub (this.group) for all of its subscriptions. A subclass
    overrides:

      - onAttach (connection, group): create its widgets
      - onDetach(): dispose of them (the group itself is disposed afterwards)
      - onNoConnection(): optional, e.g. to say that no patch is loaded
      - onLiveChanged (isLive): optional, e.g. to close a popover when it's paused

    Its subscriptions are only live (holding server listeners) while isLive is true: the view
    is active (setActive(), which the workspace calls while its tab is shown), enabled
    (setEnabled(), e.g. live mode switched on) and the page is visible (a background VS Code
    tab shows nothing). A view that must keep receiving while it can't be seen (the console is a
    log) passes { pausesWhenHidden: false }.

    Call followConnection() at the end of the subclass's constructor.
*/
export class ConnectionView
{
    constructor (ctx, { pausesWhenHidden = true, isEnabled = true } = {})
    {
        this.ctx = ctx;
        this.disposer = new Disposer();
        this.pausesWhenHidden = pausesWhenHidden;
        this.isActive = false;
        this.isEnabled = isEnabled;
        this.connection = null;
        this.group = null;
        this.wasLive = false;
        this.attachCount = 0;       // (how many times its content has been created, for tests)
    }

    /// Starts following the store (call this once the subclass has set itself up). keys are
    /// the store keys whose changes re-create the view's content.
    followConnection (keys = ["connection", "inputs", "outputs"])
    {
        this.disposer.add (this.ctx.store.subscribe (keys, () => this.reattach(), true));

        if (this.pausesWhenHidden)
            this.disposer.listen (document, "visibilitychange", () => this.updateLive());
    }

    dispose()
    {
        this.disposer.dispose();
        this.detach();
    }

    /// True while the view's subscriptions should be receiving data
    get isLive()
    {
        return this.isEnabled && (! this.pausesWhenHidden || (this.isActive && document.visibilityState !== "hidden"));
    }

    setActive (isActive)
    {
        this.isActive = !! isActive;
        this.updateLive();
    }

    setEnabled (isEnabled)
    {
        if (this.isEnabled !== !! isEnabled)
        {
            this.isEnabled = !! isEnabled;
            this.reattach();
        }
    }

    //==============================================================================
    /// Re-creates the content for the store's current connection
    reattach()
    {
        this.detach();
        const connection = this.ctx.store.get ("connection");

        if (connection && this.isEnabled)
            this.attach (connection);
        else
            this.onNoConnection?.();

        this.updateLive();
    }

    attach (connection)
    {
        this.connection = connection;
        this.group = connection.hub.createGroup();
        this.group.setActive (this.isLive);
        ++this.attachCount;
        this.onAttach (connection, this.group);
    }

    detach()
    {
        if (this.connection)
        {
            this.onDetach();
            this.group?.dispose();
            this.group = null;
            this.connection = null;
        }
    }

    updateLive()
    {
        const isLive = this.isLive;
        this.group?.setActive (isLive);

        if (isLive !== this.wasLive)
        {
            this.wasLive = isLive;
            this.onLiveChanged?.(isLive);
        }
    }

    onAttach (_connection, _group)  {}
    onDetach()                      {}
}
