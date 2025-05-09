import { Program, Engine } from "..";

const program = new Program();

await program.parseAsync(`processor Counter [[ main ]] {
		output stream float out;
		output value int test;
		void main () { test <- 66; float x; loop { out <- x; x += 1.f; advance(); } }
	}`);
const engine = new Engine();
engine.setBuildSettings({ frequency: 44100, sessionID: 1 });
await engine.loadAsync(program);

const inputs = (engine.getInputEndpoints());
const outputs = (engine.getOutputEndpoints());
const outHandle = engine.getEndpointHandle("out");
const testHandle = engine.getEndpointHandle("test");
await engine.linkAsync();
const performer = engine.createPerformer();
performer.setBlockSize(16);
performer.advance();
const outBlock = performer.getOutputFrames(outHandle, 1);
console.log({
	inputs,
	outputs,
	outBlock
})