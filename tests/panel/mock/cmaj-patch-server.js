// Served as /cmaj-patch-server.js by harness/serve.mjs, in place of the server's
// embedded_patch_session_template.js. The session ID names the fixture to replay.

import { MockServerSession } from "./mock/mock-server-session.js";

export function createServerSession (sessionID)
{
    const fixture = new URLSearchParams (location.search).get ("fixture") ?? sessionID;
    return new MockServerSession (sessionID, fixture);
}
