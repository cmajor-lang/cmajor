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

const cpp = engine.generateCode("cpp", {});

const js = engine.generateCode("javascript", {});
console.log({ js, cpp })