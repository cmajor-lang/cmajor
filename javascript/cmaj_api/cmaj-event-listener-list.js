//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


/** This event listener management class allows listeners to be attached and
 *  removed from named event types.
 */
export class EventListenerList
{
    constructor()
    {
        this.listenersPerType = {};
    }

    /** Adds a listener for a specifc event type.
     *  If the listener is already registered, this will simply add it again.
     *  Each call to addEventListener() must be paired with a removeventListener()
     *  call to remove it.
     *
     *  @param {string} type
     */
    addEventListener (type, listener)
    {
        if (type && listener)
        {
            const list = this.listenersPerType[type];

            if (list)
                list.push (listener);
            else
                this.listenersPerType[type] = [listener];
        }
    }

    /** Removes a listener that was previously added for the given event type.
     *  @param {string} type
     */
    removeEventListener (type, listener)
    {
        if (type && listener)
        {
            const list = this.listenersPerType[type];

            if (list)
            {
                const i = list.indexOf (listener);

                if (i >= 0)
                    list.splice (i, 1);
            }
        }
    }

    /** Attaches a callback function that will be automatically unregistered
     *  the first time it is invoked.
     *
     *  @param {string} type
     */
    addSingleUseListener (type, listener)
    {
        const l = message =>
        {
            this.removeEventListener (type, l);
            listener?.(message);
        };

        this.addEventListener (type, l);
    }

    /** Synchronously dispatches an event object to all listeners
     *  that are registered for the given type.
     *
     *  @param {string} type
     */
    dispatchEvent (type, event)
    {
        const list = this.listenersPerType[type];

        if (list)
            for (const listener of list)
                listener?.(event);
    }

    /** Returns the number of listeners that are currently registered
     *  for the given type of event.
     *
     *  @param {string} type
     */
    getNumListenersForType (type)
    {
        const list = this.listenersPerType[type];
        return list ? list.length : 0;
    }
}
