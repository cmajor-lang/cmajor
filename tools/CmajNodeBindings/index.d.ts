import { InputEndpoint, OutputEndpoint } from "@cmajor/types";

export class Program {
	constructor();
	parse(source: string): boolean;
	parseAsync(source: string): Promise<boolean>;
	reset(): void;
	getSyntaxTree(): string | null;
	getBinaryModule(): Uint8Array;
}

export class Engine {
	constructor();
	setBuildSettings(settings: { frequency?: number, sessionID?: number }): void;
	getBuildSettings(): { frequency?: number, sessionID?: number }
	load(program: Program): boolean;
	loadAsync(program: Program): Promise<boolean>;
	unload(): void;
	link(): void;
	linkAsync(): Promise<void>
	isLoaded(): boolean;
	isLinked(): boolean;
	getInputEndpoints(): InputEndpoint[];
	getOutputEndpoints(): OutputEndpoint[];
	getEndpointHandle(id: string): number;
	createPerformer(): Performer;
	generateCode(targetType: string, options: Record<string, any>): string;
}

export class Performer {
	constructor();
	setBlockSize(frames: number): void;
	advance(): void;
	reset(): void;
	getOutputFrames<T extends Int8Array | Uint8Array | Uint8ClampedArray | Int16Array | Uint16Array | Int32Array | Uint32Array | Float32Array | Float64Array | BigInt64Array | BigUint64Array>(endpointHandle: number, channels: number, outArray?: T): Int32Array;
	getOutputEvents(endpointHandle: number): string;
	getOutputValue(endpointHandle: number): string;
	setInputFrames<T extends Int8Array | Uint8Array | Uint8ClampedArray | Int16Array | Uint16Array | Int32Array | Uint32Array | Float32Array | Float64Array | BigInt64Array | BigUint64Array>(endpointHandle: number, frames: T): void;
	setInputValue(endpointHandle: number, value: any, frames?: number): void;
	addInputEvent(endpointHandle: number, event: any): void;
	getXRuns(): number;
}