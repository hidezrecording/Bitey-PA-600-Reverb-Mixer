
export interface ChannelState {
  label: string;
  effectsSend: number; // 0-10
  monitorSend: number; // 0-10
  high: number;        // -15 to +15 (dB)
  low: number;         // -15 to +15 (dB)
  level: number;       // 0-10
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
}
