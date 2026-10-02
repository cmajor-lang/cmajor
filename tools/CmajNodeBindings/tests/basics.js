import  "../index.js";
import { Program, Engine } from "../index.js";

const program = new Program();

program.parse(`processor Counter [[ main ]] {
		output stream float out;
		output value int test;
		void main () { test <- 66; float x; loop { out <- x; x += 1.f; advance(); } }
	}`);
const engine = new Engine();
engine.setBuildSettings({ frequency: 44100, sessionID: 1 });
engine.load(program);

const inputs = (engine.getInputEndpoints());
const outputs = (engine.getOutputEndpoints());
const outHandle = engine.getEndpointHandle("out");
engine.link();
const performer = engine.createPerformer();
performer.setBlockSize(16);
performer.advance();
const outBlock = performer.getOutputFrames(outHandle, 1);
console.log({
	inputs,
	outputs,
	outBlock
})