# Building a plugin with claude

This folder includes an example patch generated completely by claude via prompting. No code was edited by hand, with the plugin being initially built using the instructions in filter.md, and then additional prompts used to tune the UI to achieve the desired user interface.

The prompts to claude were:

* read the filter.md file and implement it
* the user interface is not displaying, can you check to see what the problem is?
* Can you change the layout of the UI so that the filter parameters are below the filter display
* The frequency response display is not appearing, can you fix it?
* The peak filter display is good, but there is an offset in the display of the high pass and low pass filters when Q is not 0, can you resolve this?
* can you update the frequency and Q parameters so that vertical rather than horizontal drag of their value is used to change value?
* can you also change the gain controls to be vertical rather than horizontal dragging to change value
* the enable parameters do not turn up as booleans in the generic gui, can you add the bool annotation to these parameters to solve this?
* In the UI, the frequency parameters have a linear response to value changes. Can the response be updated to exponential so that it is easy to change across the entire range
* can you include handles on the frequency plots that allow the user to alter the cutoff frequency and gain by moving the handle around the frequency display

There is a `claudeSession.txt` file in this directory which includes the interactive output from claude as the plugin was built. From experience, different runs of claude can produce quite different interpretations of the plugin specification, and quite different user interfaces.

## Known problems

* The plugin UI does not correctly synchronise plugin parameter values with the plugin if they are modified by the host - it is not currently using `patchConnection.requestParameterValue` correctly.

* There are problems dragging outside the active area which can lead to dropped drag operations.

## Conclusion

* In general I feel that more information provided in the initial specification is extremely useful. Some of the additional prompts are fixing problems (the user interface is not displaying) but many are clarifying behaviour or layout which was not specified initially (vertial vs horizontal parameter changes, exponential curves for parameter updates).

* Some elements of the specification were not implemented (control handles on the filters) but the original specification only mentioned handles on the combined response, which makes no sense.

* The overall experience was very positive from this exercise. The workflow was fast and efficient, and in this instance, very linear with problems being identified and simple prompts generating correct updates moving the plugin and UI forwards.

* The development took around 30 minutes, and there were a couple of false starts where I improved the filter.md specification so the starting point was more to my liking.

* It's clear to me that there are interesting posibilities of using 'vibe coding' and it appears to be a good match with Cmajor and js. The generated plugin is inherently realtime safe, and the generated js UI is surprisingly readable. Combining this approach with some manual tweaks would be a very powerful way to quickly prototype plugins, especially if more UI examples were present with simple patterns for claude to copy.
