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

/**
    Classes for providing a ServerSession and PatchConnection via a HTTP/websocket
    connection to a cmajor server
*/

import { ServerSession } from "../cmaj_api/cmaj-server-session.js"


//==============================================================================
/// An implementation of a ServerSession which communicates via a WebSocket.
class WebSocketServerSession  extends ServerSession
{
    constructor (sessionID)
    {
        super (sessionID);

        this.socket = new WebSocket (SOCKET_URL + "/" + sessionID);

        this.socket.onopen = () => this.handleSessionConnection();

        this.socket.onmessage = msg =>
        {
            const message = JSON.parse (msg.data);

            if (message)
                this.handleMessageFromServer (message);
        };
    }

    dispose()
    {
        super.dispose();
        this.socket.close();
    }

    sendMessageToServer (msg)
    {
        if (this.socket?.readyState == 1)
        {
            this.socket.send (JSON.stringify (msg));
            return true;
        }

        return false;
    }
}

export function createServerSession (sessionID)
{
    return new WebSocketServerSession (sessionID);
}
