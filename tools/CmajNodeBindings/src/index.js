import raw from './raw';
export { Performer, Program } from './raw'
export class Engine extends raw.Engine {
	getInputEndpoints = () => JSON.parse(super.getInputEndpoints());
	getOutputEndpoints = () => JSON.parse(super.getOutputEndpoints());
	getBuildSettings = () => JSON.parse(super.getBuildSettings());
	createPerformer = () => new class Performer extends (performer => class extends raw.Performer {
		constructor() { return performer; }
	})(super.createPerformer()) {
		getOutputFrames = (endpointHandle, channels, outArray) => {
			// todo : normalize result
			return super.getOutputFrames(endpointHandle, channels, outArray);
		};
	}()
}