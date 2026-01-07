export type PrimitiveDataType = { type: 'int32' | 'float32' | 'int64' | 'float64' | 'bool' | 'void'; };
export type ArrayDataType = { type: 'array'; } & (
	{ element: DataType; size: number; types?: undefined; } |
	{ types: readonly { type: ArrayDataType; size?: number }[]; element?: undefined; }
);
export type VectorDataType = { type: 'vector'; element: PrimitiveDataType; size: number; };
export type ObjectDataType = { type: 'object'; class: string; members: Record<string, DataType>; };
export type MIDIMessageType = { type: 'object'; class: 'Message'; members: { message: { type: 'int32'; }; }; }
export type StringType = { type: 'string' }
export type DataType = PrimitiveDataType | ArrayDataType | VectorDataType | ObjectDataType | { type: ArrayDataType; } | StringType;
export type EndpointPurpose = 'parameter' | 'midi in' | 'audio in' | 'transport state' | 'tempo' | 'time signature' | 'audio out' | 'console' | 'midi out';
export type EndpointType = 'value' | 'event' | 'stream';
export type Annotation = Record<string, any> & {
	name?: string;
	boolean?: boolean;
	step?: number;
	min?: number;
	max?: number;
	init?: any;
	text?: string;
}
type EndpointBase<
	TType extends EndpointType = EndpointType,
	TPurpose extends EndpointPurpose | undefined = EndpointPurpose,
	TDataType extends DataType | never = DataType,
	TDataTypes extends DataType | never = DataType
> = {
	endpointID: string;
	endpointType: TType;
	purpose: TPurpose,
	annotation?: Annotation;
	dataType?: TDataType;
	dataTypes?: TDataTypes[];
	defaultValue?: any;
	source?: any
}
export type StreamEndpoint<T extends 'audio in' | 'audio out' = any> = EndpointBase<'stream', T, PrimitiveDataType | VectorDataType, never> & { numAudioChannels: number, dataType: PrimitiveDataType | VectorDataType }
export type InputStreamEndpoint = StreamEndpoint<'audio in'>
export type OutputStreamEndpoint = StreamEndpoint<'audio out'>
export type ParameterEndpoint = EndpointBase<'value' | 'event', 'parameter', PrimitiveDataType, never> & { annotation: Annotation & { name: string } }
export type MIDIEndpoint<T extends 'midi in' | 'midi out'> = EndpointBase<'event', T, MIDIMessageType, never>
export type MIDIInputEndpoint = MIDIEndpoint<'midi in'>
export type MIDIOutputEndpoint = MIDIEndpoint<'midi out'>
export type EventEndpoint = Omit<EndpointBase<'event', undefined>, 'purpose'> & { purpose?: undefined };
export type ValueEndpoint = EndpointBase<'value', undefined> & { purpose?: undefined };
export type TransportStateEndpoint = EndpointBase<'event', 'transport state'>
export type TempoEndpoint = EndpointBase<'event', 'tempo'>
export type TimeSignatureEndpoint = EndpointBase<'event', 'time signature'>
export type ConsoleEndpoint = EndpointBase<'event', 'console', never, DataType> & { endpointID: 'console' }
export type InputEndpoint = ValueEndpoint | EventEndpoint | MIDIInputEndpoint | ParameterEndpoint | InputStreamEndpoint | TransportStateEndpoint | TempoEndpoint | TimeSignatureEndpoint;
export type OutputEndpoint = ValueEndpoint | EventEndpoint | ConsoleEndpoint | MIDIOutputEndpoint | OutputStreamEndpoint;
export type EndpointMeta = InputEndpoint | OutputEndpoint;

export type Endpoints = {
	inputs: InputEndpoint[];
	outputs: OutputEndpoint[];
}

export type StatusDetails = Endpoints & { mainProcessor: string; };
export type PatchView = {
	src: string;
	resizable: boolean;
	width?: number;
	height?: number;
};
export type Manifest = {
	CmajorVersion?: number;
	ID?: string;
	version?: string;
	name: string;
	description?: string;
	category?: string;
	manufacturer?: string;
	isInstrument?: boolean;
	sourceTransformer?: string;
	view?: PatchView;
	worker?: string;
	source?: string | string[];
	externals?: Record<string, any>;
};
export type Status = {
	error: string;
	manifest: Manifest;
	details: StatusDetails;
};
export type StoredStateData = { parameters: Array<{ name: string; value: any; }>; values: Record<string, any>; };
export type KeyValuePair = { key: string; value: any; };
export type EventListenerList = {
	listenersPerType: Record<string, any[]>;
	addEventListener(type: string, listener: Function): void;
	removeEventListener(type: string, listener: Function): void;
	addSingleUseListener(type: string, listener: Function): void;
	dispatchEvent(type: string, event: any): void;
	getNumListenersForType(type: string): number;
};

export type PatchConnection = EventListenerList & {
	getCmajorVersion(): any;
	requestStatusUpdate(): void;
	addStatusListener(listener: (status: Status) => void): void;
	removeStatusListener(listener: (status: Status) => void): void;
	resetToInitialState(): void;
	sendEventOrValue<T = any>(endpointID: string, value: T, rampFrames?: number, timeoutMillisecs?: number): void;
	sendMIDIInputEvent(endpointID: string, shortMIDICode: number): void;
	sendParameterGestureStart(endpointID: string): void;
	sendParameterGestureEnd(endpointID: string): void;
	requestStoredStateValue(key: string): void;
	sendStoredStateValue<T = any>(key: string, newValue: T): void;
	addStoredStateValueListener(listener: (data: KeyValuePair) => void): void;
	removeStoredStateValueListener(listener: (data: KeyValuePair) => void): void;
	sendFullStoredState(fullState: StoredStateData): void;
	requestFullStoredState(callback: (state: StoredStateData) => void): void;
	addEndpointListener<T = any>(endpointID: string, listener: (value: T) => void, granularity?: number, sendFullAudioData?: boolean): void;
	removeEndpointListener(endpointID: string, listener: Function): void;
	requestParameterValue(endpointID: string): void;
	addParameterListener<T = any>(endpointID: string, listener: (value: T) => void): void;
	removeParameterListener(endpointID: string, listener: Function): void;
	addAllParameterListener<T = any>(listener: (data: { endpointID: string; value: T; }) => void): void;
	removeAllParameterListener(listener: Function): void;
	getResourceAddress(path: string): string;
	utilities: any;
	manifest: Manifest;
	session?: {
		socket: WebSocket
	}
};
export type WorkerPatchConnection = PatchConnection & {
	readResource(path: string): Promise<Response>
	readResourceAsAudioData(path: string): Promise<{ frames: number[][], sampleRate: number }>
}