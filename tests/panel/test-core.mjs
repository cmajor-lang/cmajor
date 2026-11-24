// Runs mock/core-tests.js (tests for panel_api/core/*) in headless Chrome.
// Usage: node cmajor/tests/panel/test-core.mjs

import { startServer } from "./harness/serve.mjs";
import { openChrome } from "./harness/cdp.mjs";

const server = await startServer();
const page = await openChrome ({ port: 9336 });

try
{
    await page.navigate (`${server.url}/mock/core-tests.html`);
    await page.waitFor ("!! window.testResults", { timeoutMs: 10000 });
    const results = await page.evaluate ("window.testResults") ?? [];

    for (const r of results)
        if (! r.ok)
            console.log (`  FAIL ${r.test}: ${r.what}`);

    const failed = results.filter (r => ! r.ok).length;
    const problems = page.problems.filter (p => ! p.includes ("boom"));
    console.log (`${results.length - failed}/${results.length} core checks passed`);

    if (problems.length)
        console.log ("problems:\n  " + problems.join ("\n  "));

    process.exitCode = (failed === 0 && results.length > 0 && problems.length === 0) ? 0 : 1;
}
finally
{
    await page.close();
    server.close();
}
