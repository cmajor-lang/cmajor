FilterEQ is a Cmajor patch that implements a multiband filter. The patch implements a number of filters which are applied to the audio.

It has the following features:

* The first filter is a high pass filter, with controls for the cutoff frequency and Q
* There are three peak filters, with controls for centre frequency, gain in decibels and Q
* The last filter is a low pass filter, with controls for the cutoff frequency and Q
* The patch allows each of the filters to be selectively enabled or disabled
* The patch includes a user interface which has a display of the combined filter response of all of the filters in decibels, displayed against a logarithmic scale for the frequency.
* The combined filter response includes handles which allow the user to update the cutoff frequency and gain of the filters
* The filter parameters are also displayed as a value which can be dragged to be updated by the user. The filter Q is displayed as a value and can be updated by dragging.

## Implementation Details

* Add the filter to a new folder, examples/patches/claude/FilterEQ
* The DSP should be implemented as a Cmajor patch, with the DSP algorithm implemented in Cmajor, using the simper filters included in the standard library.
* The User Interface should be written in javascript, and should use a dark mode style with different pastel colours showing the different contribution of each filter to the overall plugin filter frequency response as a shaded region within the overall frequency response.
* Use vanilla javascript and css for the UI
* Use a clean and modern style for the UI

