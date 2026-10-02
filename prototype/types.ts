
export interface ChannelState {
  label: string;
  effectsSend: number; // 0-10
  monitorSend: number; // 0-10
  high: number;        // -15 to +15 (dB)
  low: number;         // -15 to +15 (dB)
  level: number;       // 0-10
  pad: 10 | 4 | 0 | -10;  // Input Pad levels (dB).
  lowCut: boolean;     // 96Hz High Pass Filter
}

export interface MasterState {
  auxMonitor: number;
  auxMain: number;
  masterMonitor: number;
  masterMain: number;
  masterReverb: number;
  reverbContour: number;
  reverbDrive: number;
  reverbTime: number; // 0-10
  masterHigh: number; // -15 to +15
  masterMid: number;  // -15 to +15
  masterLow: number;  // -15 to +15
  masterMidFreq: '0.7k' | '1.0k' | '1.4k'; // Helios Type 69 frequencies (kHz)
  masterMix: number; // 0-10 Global Dry/Wet
}
