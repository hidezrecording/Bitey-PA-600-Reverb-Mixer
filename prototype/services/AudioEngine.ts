
import { ChannelState, MasterState } from '../types';

export class AudioEngine {
  ctx: AudioContext;
  audioBuffer: AudioBuffer | null = null;
  source: AudioBufferSourceNode | null = null;
  
  startTime: number = 0;
  pauseOffset: number = 0;
  isPlaying: boolean = false;
  currentReverbTime: number = 2.5;
  tapeSpeed: '7.5' | '15' | '30' = '7.5';
  
  isBypassed: boolean = false;
  currentMix: number = 1.0; // 0.0 = Dry, 1.0 = Wet

  // Input Stage
  sourceGain: GainNode; // Global input pad

  channels: {
    input: GainNode;
    preAmpGain: GainNode;
    shaper: WaveShaperNode;
    rumbleFilter: BiquadFilterNode; // Cleans shaper artifacts (fixed)
    lowCutFilter: BiquadFilterNode; // User switchable 96Hz HPF
    bandwidthFilter: BiquadFilterNode; 
    high: BiquadFilterNode;
    low: BiquadFilterNode;
    postGain: GainNode;
    reverbSend: GainNode;
    monitorSend: GainNode;
  }[] = [];

  // Tape Slap & Saturation Network
  tapeSum: GainNode; // Sums channels into tape
  tapeDelay: DelayNode;
  tapeDrive: GainNode;
  tapeShaper: WaveShaperNode;
  tapeFilter: BiquadFilterNode;
  tapeLowCut: BiquadFilterNode; // Converted to Bell Cut
  tapeFeedback: GainNode;
  tapeWet: GainNode;
  
  // Tape Modulation (Flutter)
  flutterOsc: OscillatorNode;
  flutterGain: GainNode;
  
  // Echo Output Filter (12dB/oct slope)
  tapeOutFilter: BiquadFilterNode;

  // Reverb Network (Tube Driven Spring)
  reverbInputMerger: ChannelMergerNode; // NEW: Merges separate sends to L/R
  reverbBus: GainNode;
  reverbInputHighPass: BiquadFilterNode; // Converted to Bell Cut
  reverbDriverGain: GainNode; // Dwell/Drive control
  reverbDriver: WaveShaperNode; // 12AT7 Driver emulation
  reverbProcessor: ConvolverNode;
  reverbTone: BiquadFilterNode;
  reverbGain: GainNode;
  reverbMakeup: GainNode;
  
  // Reverb Return 1108 Amp Model (Blue Stripe Style)
  rev1108In: BiquadFilterNode;
  rev1108Shaper: WaveShaperNode;
  rev1108Out: BiquadFilterNode;
  
  masterMainGainL: GainNode;
  masterMainGainR: GainNode;
  masterMonitorGain: GainNode;

  // Master Summing & Processing
  masterSum: GainNode;
  
  // Master Scully 280 Line Amp Model
  scullyInFilter: BiquadFilterNode;
  scullyShaper: WaveShaperNode;
  scullyOutFilter: BiquadFilterNode;

  phaseGain: GainNode; // New: Phase Reverse
  masterLow: BiquadFilterNode;
  masterMid: BiquadFilterNode;
  masterHigh: BiquadFilterNode;
  
  // Analog Noise Floor
  noiseNode: AudioBufferSourceNode | null = null;
  noiseGain: GainNode;

  // Limiter Chain
  masterSubCut: BiquadFilterNode; // New: Safety filter
  busDrive: GainNode;
  busSaturator: WaveShaperNode;
  outputLimiter: WaveShaperNode; // New: Zero Latency Limiter
  
  wetGain: GainNode; 
  dryGain: GainNode;
  
  // Latency Compensation (Dummy Stages)
  dryFix1: WaveShaperNode;
  dryFix2: WaveShaperNode;
  dryFix3: WaveShaperNode;
  dryFix4: WaveShaperNode; // Added for Scully Stage compensation

  analyzerMain: AnalyserNode;
  analyzerReverb: AnalyserNode; 

  distortionCurve: Float32Array;
  op6Curve: Float32Array; // RCA OP-6 Curve
  tapeSaturationCurve: Float32Array;
  busSaturationCurve: Float32Array;
  driverTubeCurve: Float32Array; 
  limiterCurve: Float32Array;
  scullyCurve: Float32Array; // Scully 280 Curve
  curve1108: Float32Array; // Vintage 1108 Curve
  identityCurve: Float32Array;

  constructor() {
    this.ctx = new (window.AudioContext || (window as any).webkitAudioContext)({
      latencyHint: 'interactive',
      sampleRate: 44100
    });

    this.op6Curve = this.makeOP6Curve(8192);
    this.distortionCurve = this.makeTubeCurve(8192);
    this.tapeSaturationCurve = this.makeTapeCurve(8192);
    this.busSaturationCurve = this.makeBusSaturationCurve(8192);
    this.driverTubeCurve = this.makeDriverCurve(8192);
    this.limiterCurve = this.makeLimiterCurve(8192);
    this.scullyCurve = this.makeScullyCurve(8192);
    this.curve1108 = this.make1108Curve(8192);
    this.identityCurve = new Float32Array([-1, 1]);

    // --- Input Stage ---
    // Lowered from 0.06 to 0.025 to prevent blown out signals at default settings
    this.sourceGain = this.ctx.createGain();
    this.sourceGain.gain.value = 0.025; 

    // --- Tape Slap Network ---
    this.tapeSum = this.ctx.createGain();
    this.tapeSum.gain.value = 0.5;

    this.tapeDelay = this.ctx.createDelay(1.0);
    this.tapeDrive = this.ctx.createGain();
    this.tapeShaper = this.ctx.createWaveShaper();
    this.tapeShaper.curve = this.tapeSaturationCurve;
    this.tapeShaper.oversample = '4x';
    
    // Flutter setup
    this.flutterOsc = this.ctx.createOscillator();
    this.flutterOsc.type = 'sine';
    this.flutterOsc.frequency.value = 0.5; // Starts slow (Wow)
    this.flutterGain = this.ctx.createGain();
    this.flutterGain.gain.value = 0.0003; // Subtle modulation
    this.flutterOsc.connect(this.flutterGain);
    this.flutterGain.connect(this.tapeDelay.delayTime);
    this.flutterOsc.start();
    
    // CHANGED: From Bandpass to Lowpass for body
    this.tapeFilter = this.ctx.createBiquadFilter();
    this.tapeFilter.type = 'lowpass'; 
    
    // CHANGED: From Cut to Boost for "Low Mid Centric" tone
    this.tapeLowCut = this.ctx.createBiquadFilter();
    this.tapeLowCut.type = 'peaking';
    this.tapeLowCut.frequency.value = 350; // Low Mid Center
    this.tapeLowCut.Q.value = 0.8; 
    this.tapeLowCut.gain.value = 4; // Increased body boost for thick slap

    this.tapeFeedback = this.ctx.createGain();
    this.tapeWet = this.ctx.createGain();
    
    // Single 12dB/oct High Cut (Lowpass)
    this.tapeOutFilter = this.ctx.createBiquadFilter();
    this.tapeOutFilter.type = 'lowpass';
    this.tapeOutFilter.frequency.value = 1100; // Lowered to 1100Hz for warm, dark Sun tone
    this.tapeOutFilter.Q.value = 0.6; 
    
    // CHANGED: Zero feedback for "True Slap" (Single Repeat)
    this.tapeFeedback.gain.value = 0.0; 
    
    this.tapeWet.gain.value = 0.0;
    this.tapeDrive.gain.value = 0.9; 

    // --- Reverb Network (Tube Driven) ---
    // Create Merger for stereo split sends
    this.reverbInputMerger = this.ctx.createChannelMerger(2);
    
    this.reverbBus = this.ctx.createGain();
    this.reverbBus.gain.value = 0.4; 
    
    // Merger feeds the bus (stereo pair preserved)
    this.reverbInputMerger.connect(this.reverbBus);
    
    // Dr Dog "The Truth" Tone: Tighter bass cut to prevent mud
    this.reverbInputHighPass = this.ctx.createBiquadFilter();
    this.reverbInputHighPass.type = 'highpass'; 
    this.reverbInputHighPass.frequency.value = 180; // Tightened from 100Hz
    this.reverbInputHighPass.Q.value = 0.5;

    // Driver Stage 
    this.reverbDriverGain = this.ctx.createGain();
    this.reverbDriver = this.ctx.createWaveShaper();
    this.reverbDriver.curve = this.driverTubeCurve;
    this.reverbDriver.oversample = '4x';

    this.reverbProcessor = this.ctx.createConvolver();
    this.reverbProcessor.normalize = false; 
    
    // Dr Dog Tone: Darker, pillowy top end
    this.reverbTone = this.ctx.createBiquadFilter();
    this.reverbTone.type = 'lowpass';
    this.reverbTone.frequency.value = 2200; // Reduced from 3500Hz
    this.reverbTone.Q.value = 0.4; // Very gentle rolloff

    this.reverbMakeup = this.ctx.createGain();
    // Adjusted to 6.0 for better gain staging with the new cleaner 1108 curve
    this.reverbMakeup.gain.value = 6.0; 

    this.reverbGain = this.ctx.createGain();
    
    // --- Reverb Return 1108 Amp Model (Blue Stripe Style) ---
    // Input Transformer: Tighter bottom end common to 1176s
    this.rev1108In = this.ctx.createBiquadFilter();
    this.rev1108In.type = 'highpass';
    this.rev1108In.frequency.value = 50;
    this.rev1108In.Q.value = 0.5;
    
    // FET Saturation
    this.rev1108Shaper = this.ctx.createWaveShaper();
    this.rev1108Shaper.curve = this.curve1108;
    this.rev1108Shaper.oversample = '4x';
    
    // Output Transformer: Bandwidth limiting but slightly more open/presence
    this.rev1108Out = this.ctx.createBiquadFilter();
    this.rev1108Out.type = 'lowpass';
    this.rev1108Out.frequency.value = 24000;
    this.rev1108Out.Q.value = 0.5;

    this.masterMainGainL = this.ctx.createGain();
    this.masterMainGainR = this.ctx.createGain();
    this.masterMonitorGain = this.ctx.createGain();
    
    // Master Summing
    this.masterSum = this.ctx.createGain();
    this.masterSum.gain.value = 1.0; 
    
    // --- Master Scully 280 Line Amp Modeling ---
    // Input Transformer: Highpass to simulate low-end phase shift/weight
    // Relaxed from 25Hz to 10Hz to reduce phase shift in audible bass
    this.scullyInFilter = this.ctx.createBiquadFilter();
    this.scullyInFilter.type = 'highpass';
    this.scullyInFilter.frequency.value = 10; 
    this.scullyInFilter.Q.value = 0.6; 

    // Line Amp Saturation
    this.scullyShaper = this.ctx.createWaveShaper();
    this.scullyShaper.curve = this.scullyCurve;
    this.scullyShaper.oversample = '4x';

    // Output Transformer: Bandwidth limiting
    this.scullyOutFilter = this.ctx.createBiquadFilter();
    this.scullyOutFilter.type = 'lowpass';
    this.scullyOutFilter.frequency.value = 22000; // Open but limited
    this.scullyOutFilter.Q.value = 0.5;
    
    // Analog Noise Floor
    this.noiseGain = this.ctx.createGain();
    this.noiseGain.gain.value = 0.0004; // -68dB range
    this.startNoiseFloor();
    this.noiseGain.connect(this.masterSum);

    // Phase Switch Node
    this.phaseGain = this.ctx.createGain();
    this.phaseGain.gain.value = 1.0; 

    // Master EQ (Pultec Style)
    this.masterLow = this.ctx.createBiquadFilter();
    this.masterLow.type = 'lowshelf';
    this.masterLow.frequency.value = 60; // Pultec 60Hz
    this.masterLow.Q.value = 0.5; // Broad shelf for "Vintage" weight
    
    this.masterMid = this.ctx.createBiquadFilter();
    this.masterMid.type = 'peaking';
    // Helios Type 69 1kHz Mid Setting (Confirmed)
    this.masterMid.frequency.value = 1000; 
    // Broadened by ~15% (1.25 -> 1.08) to make peak less sharp while retaining character
    this.masterMid.Q.value = 1.08; 
    
    this.masterHigh = this.ctx.createBiquadFilter();
    this.masterHigh.type = 'highshelf';
    this.masterHigh.frequency.value = 12000; // Updated to 12kHz
    this.masterHigh.Q.value = 0.3; // Extremely broad (Baxandall style) to slope from 8k
    
    // Master Sub Cut (Safety Filter) - Kept at 45Hz for protection
    this.masterSubCut = this.ctx.createBiquadFilter();
    this.masterSubCut.type = 'highpass';
    this.masterSubCut.frequency.value = 45; 
    this.masterSubCut.Q.value = 0.5; 
    
    // Bus Processing
    this.busDrive = this.ctx.createGain();
    this.busDrive.gain.value = 0.9; 
    
    this.busSaturator = this.ctx.createWaveShaper();
    this.busSaturator.curve = this.busSaturationCurve;
    this.busSaturator.oversample = '4x'; // Restored for bite

    // Master Limiter REPLACEMENT (Zero Latency)
    // Removed DynamicsCompressor due to phase issues with dry signal.
    // Replaced with a hard clipping safety limiter.
    this.outputLimiter = this.ctx.createWaveShaper();
    this.outputLimiter.curve = this.limiterCurve;
    this.outputLimiter.oversample = '4x'; // Restored for bite

    this.wetGain = this.ctx.createGain();
    this.dryGain = this.ctx.createGain();
    this.dryGain.gain.value = 0; 
    
    // Latency Compensation (Dummy Stages)
    // We create 4 identical stages to match the Wet path's 4 stages of 4x Oversampling
    // 1. Channel Shaper
    // 2. Scully Line Amp Shaper (NEW)
    // 3. Bus Saturator
    // 4. Output Limiter
    this.dryFix1 = this.ctx.createWaveShaper();
    this.dryFix1.curve = this.identityCurve;
    this.dryFix1.oversample = '4x';

    this.dryFix2 = this.ctx.createWaveShaper();
    this.dryFix2.curve = this.identityCurve;
    this.dryFix2.oversample = '4x';

    this.dryFix3 = this.ctx.createWaveShaper();
    this.dryFix3.curve = this.identityCurve;
    this.dryFix3.oversample = '4x';

    this.dryFix4 = this.ctx.createWaveShaper();
    this.dryFix4.curve = this.identityCurve;
    this.dryFix4.oversample = '4x';
    
    // Chain the dry fix stages
    this.dryFix1.connect(this.dryFix2);
    this.dryFix2.connect(this.dryFix3);
    this.dryFix3.connect(this.dryFix4);
    this.dryFix4.connect(this.dryGain);

    this.analyzerMain = this.ctx.createAnalyser();
    this.analyzerMain.fftSize = 256;
    this.analyzerMain.smoothingTimeConstant = 0.7;

    this.analyzerReverb = this.ctx.createAnalyser();
    this.analyzerReverb.fftSize = 256;
    this.analyzerReverb.smoothingTimeConstant = 0.7;

    // --- Graph Connections ---
    
    // Tape Chain: Sum -> Delay -> Drive -> Shaper -> Filter -> LowCut -> Wet/Feedback
    this.tapeSum.connect(this.tapeDelay);
    this.tapeDelay.connect(this.tapeDrive);
    this.tapeDrive.connect(this.tapeShaper);
    this.tapeShaper.connect(this.tapeFilter);
    this.tapeFilter.connect(this.tapeLowCut); 
    
    this.tapeLowCut.connect(this.tapeFeedback);
    this.tapeFeedback.connect(this.tapeDelay);
    
    this.tapeLowCut.connect(this.tapeWet);
    
    // 12dB/oct High Cut
    this.tapeWet.connect(this.tapeOutFilter);
    
    // Echo goes to Main Gain
    this.tapeOutFilter.connect(this.masterMainGainL);
    this.tapeOutFilter.connect(this.masterMainGainR);
    
    // Space Echo bleed - Feed to both L/R of reverb to keep it centered
    const tapeToReverb = this.ctx.createGain();
    tapeToReverb.gain.value = 0.12; 
    this.tapeOutFilter.connect(tapeToReverb);
    tapeToReverb.connect(this.reverbInputMerger, 0, 0);
    tapeToReverb.connect(this.reverbInputMerger, 0, 1);

    // Reverb Path: Bus -> Clean Filter -> Driver Gain -> Tube Shaper -> Tank (Convolver) -> Tone -> Gain -> 1108 AMP -> Master
    this.reverbBus.connect(this.reverbInputHighPass);
    this.reverbInputHighPass.connect(this.reverbDriverGain);
    
    this.reverbDriverGain.connect(this.reverbDriver);
    this.reverbDriver.connect(this.reverbProcessor);
    
    this.reverbProcessor.connect(this.reverbMakeup);
    this.reverbMakeup.connect(this.reverbTone);
    this.reverbTone.connect(this.reverbGain);
    
    // CONNECT REVERB 1108 CHAIN (Replacing Scully)
    this.reverbGain.connect(this.rev1108In);
    this.rev1108In.connect(this.rev1108Shaper);
    this.rev1108Shaper.connect(this.rev1108Out);
    
    // Reverb Metering Tap
    this.reverbGain.connect(this.analyzerReverb); 
    
    // Connect Reverb 1108 Out to Master Sum (Bypassing Main Fader)
    this.rev1108Out.connect(this.masterSum);

    // Master Bus
    const merger = this.ctx.createChannelMerger(2);
    this.masterMainGainL.connect(merger, 0, 0);
    this.masterMainGainR.connect(merger, 0, 1);
    
    // Connect Dry/Echo mix to Master Sum
    merger.connect(this.masterSum);

    // Master Chain: Sum -> SCULLY -> Phase -> EQ -> SubCut -> Drive -> Saturation -> Limiter -> Output
    this.masterSum.connect(this.scullyInFilter);
    this.scullyInFilter.connect(this.scullyShaper);
    this.scullyShaper.connect(this.scullyOutFilter);
    this.scullyOutFilter.connect(this.phaseGain);

    this.phaseGain.connect(this.masterLow);
    
    this.masterLow.connect(this.masterMid);
    this.masterMid.connect(this.masterHigh);
    
    this.masterHigh.connect(this.masterSubCut); 
    this.masterSubCut.connect(this.busDrive);
    
    this.busDrive.connect(this.busSaturator);
    this.busSaturator.connect(this.outputLimiter); // Replaced Compressor
    
    this.outputLimiter.connect(this.wetGain);

    // METERING TAP POINT CHANGED:
    this.outputLimiter.connect(this.analyzerMain);

    // Audio Output
    this.wetGain.connect(this.ctx.destination);
    
    // Dry Path: Fix1 -> Fix2 -> Fix3 -> Fix4 -> Gain -> Destination
    // (Connections set in init and play)
    this.dryGain.connect(this.ctx.destination);
    
    // Note: analyzerMain no longer connects to destination, it acts as a leaf node for metering.

    this.setTapeParameters('7.5', 5, true); 
    this.setTapeSize('1/4"'); 
    this.loadSpringReverb(this.currentReverbTime);
    this.allocateChannels(2);
  }
  
  // ... (rest of methods)

  private makeScullyCurve(amount: number): Float32Array {
    const n = amount;
    const curve = new Float32Array(n);
    for (let i = 0; i < n; ++i) {
      const x = (i * 2) / n - 1;
      
      // High Fidelity Scully 280 Model:
      // Linear region with extremely subtle "Iron" hysteresis simulation (asymmetry)
      let y = x + 0.02 * (x * x); 

      // Transformer Saturation (Rounder than FET) - only kicks in at high levels
      if (y > 0.7) {
        y = 0.7 + (y - 0.7) / (1 + Math.pow((y - 0.7) * 2, 2)); // Soft shoulder
      } 
      if (y < -0.7) {
        y = -0.7 + (y + 0.7) / (1 + Math.pow((y + 0.7) * 2, 2));
      }
      
      // Normalize peak output to prevent volume drop vs dry signal
      y /= 0.85; 

      curve[i] = Math.max(-1, Math.min(1, y));
    }
    return curve;
  }
  
  private make1108Curve(amount: number): Float32Array {
    const n = amount;
    const curve = new Float32Array(n);
    for (let i = 0; i < n; ++i) {
      const x = (i * 2) / n - 1;
      
      // HIGH FIDELITY VINTAGE 1108/1176 MODEL
      // Removes all "fuzz" artifacts by ensuring the center range is nearly linear.
      // 1. Asymmetry (Class A Bias) - adds weight/3D depth without distortion.
      let y = x + 0.05 * (x * x);
      
      // 2. Headroom Soft Clip (FET behavior)
      // Only compress if we exceed threshold, mimicking rail voltage limits
      if (y > 0.8) {
          y = 0.8 + Math.tanh((y - 0.8) * 1.5) * 0.2;
      } else if (y < -0.8) {
          y = -0.8 + Math.tanh((y + 0.8) * 1.5) * 0.2;
      }
      
      curve[i] = y;
    }
    return curve;
  }

  private makeLimiterCurve(amount: number): Float32Array {
      const n = amount;
      const curve = new Float32Array(n);
      for(let i=0; i<n; i++) {
          const x = (i * 2) / n - 1;
          // Hard Clip / Brickwall at 0dB (or slightly below)
          if (x > 0.99) curve[i] = 0.99;
          else if (x < -0.99) curve[i] = -0.99;
          else curve[i] = x;
      }
      return curve;
  }
  
  // ... (rest of private methods same as before)
  private startNoiseFloor() {
      // Create Pink-ish Noise Buffer (2s loop)
      const bufferSize = this.ctx.sampleRate * 2;
      const buffer = this.ctx.createBuffer(1, bufferSize, this.ctx.sampleRate);
      const data = buffer.getChannelData(0);
      let b0, b1, b2, b3, b4, b5, b6;
      b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0;
      for (let i = 0; i < bufferSize; i++) {
        const white = Math.random() * 2 - 1;
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        data[i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
        data[i] *= 0.11; // Compensate gain
        b6 = white * 0.115926;
      }
      
      this.noiseNode = this.ctx.createBufferSource();
      this.noiseNode.buffer = buffer;
      this.noiseNode.loop = true;
      this.noiseNode.connect(this.noiseGain);
      this.noiseNode.start();
  }

  getLevels() {
    return {
        main: this.calculateRMS(this.analyzerMain),
        reverb: this.calculateRMS(this.analyzerReverb)
    };
  }
  
  setPhaseInvert(inverted: boolean) {
      const t = this.ctx.currentTime;
      this.phaseGain.gain.setTargetAtTime(inverted ? -1.0 : 1.0, t, 0.05);
  }

  setTapeParameters(speed: '7.5' | '15' | '30', mix: number, bypass: boolean) {
      this.tapeSpeed = speed;
      const t = this.ctx.currentTime;
      
      let delayTime = 0.134; 
      const rollOffFreq = 1100; 
      
      let flutterFreq = 0.5;
      let flutterDepth = 0.0003;

      if (speed === '7.5') {
          delayTime = 0.134; 
          flutterFreq = 0.8; 
          flutterDepth = 0.0004;
      }
      if (speed === '15') {
          delayTime = 0.085; 
          flutterFreq = 1.2;
          flutterDepth = 0.0002; 
      }
      if (speed === '30') {
          delayTime = 0.042; 
          flutterFreq = 2.5;
          flutterDepth = 0.0001; 
      }
      
      this.tapeDelay.delayTime.setTargetAtTime(delayTime, t, 0.05);
      this.flutterOsc.frequency.setTargetAtTime(flutterFreq, t, 0.1);
      this.flutterGain.gain.setTargetAtTime(flutterDepth, t, 0.1);
      this.tapeOutFilter.frequency.setTargetAtTime(rollOffFreq, t, 0.1);
      this.tapeOutFilter.Q.setTargetAtTime(0.7, t, 0.1); 
      
      const mixNorm = mix / 10;
      const gainVal = bypass ? 0.0 : (mixNorm * 0.8); 
      this.tapeWet.gain.setTargetAtTime(gainVal, t, 0.05);
  }

  setTapeSize(size: '1/4"' | '1/2"' | '1"') {
      const t = this.ctx.currentTime;
      if (size === '1/4"') {
          this.tapeDrive.gain.setTargetAtTime(1.5, t, 0.1);
          this.tapeFilter.frequency.setTargetAtTime(1000, t, 0.1); 
          this.tapeFilter.Q.setTargetAtTime(0.5, t, 0.1); 
      } 
      else if (size === '1/2"') {
          this.tapeDrive.gain.setTargetAtTime(1.1, t, 0.1);
          this.tapeFilter.frequency.setTargetAtTime(2200, t, 0.1);
          this.tapeFilter.Q.setTargetAtTime(0.5, t, 0.1);
      }
      else {
          this.tapeDrive.gain.setTargetAtTime(0.8, t, 0.1);
          this.tapeFilter.frequency.setTargetAtTime(4000, t, 0.1);
          this.tapeFilter.Q.setTargetAtTime(0.5, t, 0.1); 
      }
  }

  private calculateRMS(analyser: AnalyserNode): number {
    const bufferLength = analyser.frequencyBinCount;
    const dataArray = new Uint8Array(bufferLength);
    analyser.getByteTimeDomainData(dataArray);

    let sum = 0;
    for (let i = 0; i < bufferLength; i++) {
        const value = (dataArray[i] - 128) / 128; 
        sum += value * value;
    }
    const rms = Math.sqrt(sum / bufferLength);
    return Math.min(rms * 4.0, 1.4); 
  }

  private makeTubeCurve(amount: number): Float32Array {
    const n_samples = amount;
    const curve = new Float32Array(n_samples);
    for (let i = 0; i < n_samples; ++i) {
      const x = (i * 2) / n_samples - 1;
      let y = x;
      if (x < -0.5) y = x + 0.2 * (x + 0.5);
      else if (x > 0.5) y = x - 0.2 * (x - 0.5);
      else y = x;
      y = Math.tanh(y * 1.2);
      curve[i] = y;
    }
    return curve;
  }
  
  private makeOP6Curve(amount: number): Float32Array {
    const n = amount;
    const curve = new Float32Array(n);
    for (let i = 0; i < n; ++i) {
      const x = (i * 2) / n - 1;
      if (x < 0) {
        curve[i] = Math.tanh(x * 1.2); 
      } else {
        curve[i] = Math.tanh(x * 1.05); 
      }
    }
    return curve;
  }
  
  private makeDriverCurve(amount: number): Float32Array {
    const n_samples = amount;
    const curve = new Float32Array(n_samples);
    for (let i = 0; i < n_samples; ++i) {
      const x = (i * 2) / n_samples - 1;
      curve[i] = Math.tanh(x * 0.8); 
    }
    return curve;
  }

  private makeTapeCurve(amount: number): Float32Array {
    const n_samples = amount;
    const curve = new Float32Array(n_samples);
    for (let i = 0; i < n_samples; ++i) {
      const x = (i * 2) / n_samples - 1;
      curve[i] = Math.tanh(x * 1.5); 
    }
    return curve;
  }

  private makeBusSaturationCurve(amount: number): Float32Array {
    const n_samples = amount;
    const curve = new Float32Array(n_samples);
    for (let i = 0; i < n_samples; ++i) {
        const x = (i * 2) / n_samples - 1;
        curve[i] = x * 0.98 + Math.tanh(x * 0.1) * 0.02; 
    }
    return curve;
  }

  private loadSpringReverb(duration: number) {
    this.currentReverbTime = duration;
    const sampleRate = this.ctx.sampleRate;
    const length = Math.floor(sampleRate * duration);
    const buffer = this.ctx.createBuffer(2, length, sampleRate);
    
    const flutterRate = 3.5; 
    const flutterDepth = 0.02; 

    for (let c = 0; c < 2; c++) {
        const data = buffer.getChannelData(c);
        let lastNoise = 0; 
        let lastVal = 0;

        for (let i = 0; i < length; i++) {
            const t = i / sampleRate;
            let noise = 0;
            const density = 2200; 
            if (Math.random() < density / sampleRate) {
                noise = (Math.random() * 2 - 1);
                if(t < 0.05) noise *= 2.0;
            }
            noise = (noise + lastNoise * 0.8) / 1.8; 
            lastNoise = noise;
            const flutter = 1.0 + flutterDepth * Math.sin(2 * Math.PI * flutterRate * t + (c * Math.PI * 0.5));
            const decay = Math.exp(-t * (4.0 - duration * 0.4)); 
            let signal = noise * decay * flutter;
            signal = (signal + lastVal * 0.9) / 1.9;
            lastVal = signal;
            if (signal > 0.8) signal = 0.8 + (signal - 0.8) * 0.5;
            if (signal < -0.8) signal = -0.8 + (signal + 0.8) * 0.5;
            data[i] = signal * 1.5; 
        }
    }
    this.reverbProcessor.buffer = buffer;
  }

  private allocateChannels(count: number) {
    for (let i = 0; i < count; i++) {
      const input = this.ctx.createGain();
      const preAmpGain = this.ctx.createGain();
      const shaper = this.ctx.createWaveShaper();
      shaper.curve = this.op6Curve;
      shaper.oversample = '4x'; 

      // Relaxed rumble filter to 20Hz to reduce phase shift in audible bass
      const rumbleFilter = this.ctx.createBiquadFilter();
      rumbleFilter.type = 'highpass';
      rumbleFilter.frequency.value = 20; 
      rumbleFilter.Q.value = 0.5;

      // NEW: 96Hz High Pass Filter (Gentle)
      const lowCutFilter = this.ctx.createBiquadFilter();
      lowCutFilter.type = 'highpass';
      lowCutFilter.frequency.value = 96;
      lowCutFilter.Q.value = 0.5; // Gentle slope

      const bandwidthFilter = this.ctx.createBiquadFilter();
      bandwidthFilter.type = 'lowpass';
      bandwidthFilter.frequency.value = 24000; 
      bandwidthFilter.Q.value = 0.5; 
      
      const preEmp = this.ctx.createBiquadFilter();
      preEmp.type = 'lowshelf';
      preEmp.frequency.value = 100;
      preEmp.gain.value = 0.5; 

      const deEmp = this.ctx.createBiquadFilter();
      deEmp.type = 'lowshelf';
      deEmp.frequency.value = 100;
      deEmp.gain.value = -0.5; 
      
      const tolerance = 0.98 + Math.random() * 0.04;
      
      const low = this.ctx.createBiquadFilter();
      low.type = 'peaking'; 
      low.frequency.value = 100 * tolerance; 
      low.Q.value = 0.55; 
      
      const high = this.ctx.createBiquadFilter();
      high.type = 'highshelf';
      high.frequency.value = 10000; // Neve 1064 10kHz
      high.Q.value = 0.6; // Slightly broader/gentler than Butterworth (0.707) to emulate analog smoothness
      
      const postGain = this.ctx.createGain();
      const reverbSend = this.ctx.createGain();
      const monitorSend = this.ctx.createGain();

      input.connect(preEmp);
      preEmp.connect(preAmpGain);
      preAmpGain.connect(shaper);
      shaper.connect(deEmp);
      deEmp.connect(rumbleFilter);
      rumbleFilter.connect(lowCutFilter); // Connect Rumble -> LowCut
      lowCutFilter.connect(bandwidthFilter); // Connect LowCut -> Bandwidth
      bandwidthFilter.connect(low);
      low.connect(high);
      high.connect(postGain);
      postGain.connect(reverbSend);
      
      if (i === 0) postGain.connect(this.masterMainGainL);
      else postGain.connect(this.masterMainGainR);
      
      postGain.connect(this.tapeSum); 

      const bleedGain = 0.15;
      if (i === 0) {
          reverbSend.connect(this.reverbInputMerger, 0, 0);
          const bleed = this.ctx.createGain();
          bleed.gain.value = bleedGain;
          reverbSend.connect(bleed).connect(this.reverbInputMerger, 0, 1);
      } else {
          reverbSend.connect(this.reverbInputMerger, 0, 1);
          const bleed = this.ctx.createGain();
          bleed.gain.value = bleedGain;
          reverbSend.connect(bleed).connect(this.reverbInputMerger, 0, 0);
      }
      
      monitorSend.connect(this.masterMonitorGain);

      this.channels.push({ input, preAmpGain, shaper, rumbleFilter, lowCutFilter, bandwidthFilter, high, low, postGain, reverbSend, monitorSend });
    }
  }

  // --- Export Logic ---

  private writeString(view: DataView, offset: number, string: string) {
    for (let i = 0; i < string.length; i++) {
      view.setUint8(offset + i, string.charCodeAt(i));
    }
  }

  private bufferToWave(abuffer: AudioBuffer): Blob {
    const numOfChan = abuffer.numberOfChannels;
    const length = abuffer.length * numOfChan * 2 + 44;
    const buffer = new ArrayBuffer(length);
    const view = new DataView(buffer);
    const channels = [];
    let i, sample, offset = 0, pos = 0;

    this.writeString(view, 0, 'RIFF');
    view.setUint32(4, 36 + abuffer.length * numOfChan * 2, true);
    this.writeString(view, 8, 'WAVE');
    this.writeString(view, 12, 'fmt ');
    view.setUint32(16, 16, true);
    view.setUint16(20, 1, true); 
    view.setUint16(22, numOfChan, true);
    view.setUint32(24, abuffer.sampleRate, true);
    view.setUint32(28, abuffer.sampleRate * 2 * numOfChan, true);
    view.setUint16(32, numOfChan * 2, true);
    view.setUint16(34, 16, true); 
    this.writeString(view, 36, 'data');
    view.setUint32(40, abuffer.length * numOfChan * 2, true);

    for(i = 0; i < abuffer.numberOfChannels; i++)
      channels.push(abuffer.getChannelData(i));

    offset = 44;
    while(pos < abuffer.length) {
      for(i = 0; i < numOfChan; i++) {
        sample = Math.max(-1, Math.min(1, channels[i][pos]));
        sample = (0.5 + sample < 0 ? sample * 32768 : sample * 32767)|0;
        view.setInt16(offset, sample, true);
        offset += 2;
      }
      pos++;
    }
    return new Blob([buffer], {type: "audio/wav"});
  }

  async exportProcessedAudio(): Promise<Blob | null> {
      if (!this.audioBuffer) return null;
      
      const dur = this.audioBuffer.duration;
      const sr = this.audioBuffer.sampleRate;
      const offline = new OfflineAudioContext(2, dur * sr, sr);
      
      const src = offline.createBufferSource();
      src.buffer = this.audioBuffer;
      
      const srcGain = offline.createGain();
      srcGain.gain.value = this.sourceGain.gain.value;
      src.connect(srcGain);
      const splitter = offline.createChannelSplitter(2);
      srcGain.connect(splitter);
      
      const chPosts: GainNode[] = [];
      const chReverbSends: GainNode[] = [];
      
      for(let i=0; i<2; i++) {
          const live = this.channels[i];
          const input = offline.createGain();
          splitter.connect(input, this.audioBuffer.numberOfChannels > 1 ? i : 0);
          
          const preEmp = offline.createBiquadFilter();
          preEmp.type = 'lowshelf';
          preEmp.frequency.value = 100;
          preEmp.gain.value = 0.5;

          const pre = offline.createGain();
          pre.gain.value = live.preAmpGain.gain.value;
          
          const shaper = offline.createWaveShaper();
          shaper.curve = this.op6Curve; 
          shaper.oversample = '4x';
          
          const deEmp = offline.createBiquadFilter();
          deEmp.type = 'lowshelf';
          deEmp.frequency.value = 100;
          deEmp.gain.value = -0.5; 

          const rumble = offline.createBiquadFilter();
          rumble.type = 'highpass';
          rumble.frequency.value = 20; // Relaxed to 20Hz

          // NEW: Export Low Cut
          const lowCut = offline.createBiquadFilter();
          lowCut.type = 'highpass';
          lowCut.frequency.value = live.lowCutFilter.frequency.value; // Copy current state
          lowCut.Q.value = 0.5;
          
          const bw = offline.createBiquadFilter();
          bw.type = 'lowpass';
          bw.frequency.value = live.bandwidthFilter.frequency.value;
          
          const low = offline.createBiquadFilter();
          low.type = live.low.type; 
          low.frequency.value = live.low.frequency.value;
          low.Q.value = live.low.Q.value;
          low.gain.value = live.low.gain.value;
          
          const high = offline.createBiquadFilter();
          high.type = 'highshelf';
          high.frequency.value = live.high.frequency.value;
          high.Q.value = live.high.Q.value; // Ensure Curve Match
          high.gain.value = live.high.gain.value;
          
          const post = offline.createGain();
          post.gain.value = live.postGain.gain.value;
          
          const rvSend = offline.createGain();
          rvSend.gain.value = live.reverbSend.gain.value;
          
          input.connect(preEmp).connect(pre).connect(shaper).connect(deEmp).connect(rumble).connect(lowCut).connect(bw).connect(low).connect(high).connect(post);
          post.connect(rvSend);
          
          chPosts.push(post);
          chReverbSends.push(rvSend);
      }
      
      const tapeSum = offline.createGain();
      tapeSum.gain.value = this.tapeSum.gain.value;
      chPosts.forEach(p => p.connect(tapeSum));
      
      const delay = offline.createDelay(1.0);
      delay.delayTime.value = this.tapeDelay.delayTime.value;
      
      const flutOsc = offline.createOscillator();
      flutOsc.frequency.value = this.flutterOsc.frequency.value;
      const flutGain = offline.createGain();
      flutGain.gain.value = this.flutterGain.gain.value;
      flutOsc.connect(flutGain);
      flutGain.connect(delay.delayTime);
      flutOsc.start();
      
      const drive = offline.createGain();
      drive.gain.value = this.tapeDrive.gain.value;
      
      const tapeShaper = offline.createWaveShaper();
      tapeShaper.curve = this.tapeSaturationCurve;
      tapeShaper.oversample = '4x';
      
      const tapeFilter = offline.createBiquadFilter();
      tapeFilter.type = this.tapeFilter.type;
      tapeFilter.frequency.value = this.tapeFilter.frequency.value;
      tapeFilter.Q.value = this.tapeFilter.Q.value;
      
      const tapeLowCut = offline.createBiquadFilter();
      tapeLowCut.type = this.tapeLowCut.type;
      tapeLowCut.frequency.value = this.tapeLowCut.frequency.value;
      tapeLowCut.Q.value = this.tapeLowCut.Q.value;
      tapeLowCut.gain.value = this.tapeLowCut.gain.value;
      
      const feedback = offline.createGain();
      feedback.gain.value = this.tapeFeedback.gain.value;
      
      const wet = offline.createGain();
      wet.gain.value = this.tapeWet.gain.value;
      
      const outFilter = offline.createBiquadFilter();
      outFilter.type = 'lowpass';
      outFilter.frequency.value = this.tapeOutFilter.frequency.value;
      outFilter.Q.value = this.tapeOutFilter.Q.value;
      
      tapeSum.connect(delay).connect(drive).connect(tapeShaper).connect(tapeFilter).connect(tapeLowCut);
      tapeLowCut.connect(feedback).connect(delay);
      tapeLowCut.connect(wet).connect(outFilter);
      
      const reverbMerger = offline.createChannelMerger(2);
      const reverbBus = offline.createGain();
      reverbBus.gain.value = this.reverbBus.gain.value;
      reverbMerger.connect(reverbBus);
      
      const bleedGain = 0.15;
      
      chReverbSends[0].connect(reverbMerger, 0, 0); 
      const b1 = offline.createGain(); b1.gain.value = bleedGain;
      chReverbSends[0].connect(b1).connect(reverbMerger, 0, 1);
      
      chReverbSends[1].connect(reverbMerger, 0, 1);
      const b2 = offline.createGain(); b2.gain.value = bleedGain;
      chReverbSends[1].connect(b2).connect(reverbMerger, 0, 0);
      
      const tapeToRv = offline.createGain();
      tapeToRv.gain.value = 0.12;
      outFilter.connect(tapeToRv);
      tapeToRv.connect(reverbMerger, 0, 0);
      tapeToRv.connect(reverbMerger, 0, 1);
      
      const rvHP = offline.createBiquadFilter();
      rvHP.type = 'highpass';
      rvHP.frequency.value = this.reverbInputHighPass.frequency.value;
      rvHP.Q.value = this.reverbInputHighPass.Q.value;
      
      const rvDriveGain = offline.createGain();
      rvDriveGain.gain.value = this.reverbDriverGain.gain.value;
      
      const rvDriver = offline.createWaveShaper();
      rvDriver.curve = this.driverTubeCurve;
      rvDriver.oversample = '4x';
      
      const convolver = offline.createConvolver();
      convolver.normalize = false;
      if (this.reverbProcessor.buffer) convolver.buffer = this.reverbProcessor.buffer;
      
      const rvMakeup = offline.createGain();
      rvMakeup.gain.value = this.reverbMakeup.gain.value;
      
      const rvTone = offline.createBiquadFilter();
      rvTone.type = 'lowpass';
      rvTone.frequency.value = this.reverbTone.frequency.value;
      
      const rvOutGain = offline.createGain();
      rvOutGain.gain.value = this.reverbGain.gain.value;

      // REVERB 1108 CHAIN FOR EXPORT
      const rev1108In = offline.createBiquadFilter();
      rev1108In.type = 'highpass'; rev1108In.frequency.value = 50; rev1108In.Q.value = 0.5;
      
      const rev1108Shaper = offline.createWaveShaper();
      rev1108Shaper.curve = this.curve1108; // Use new 1108 curve
      rev1108Shaper.oversample = '4x';
      
      const rev1108Out = offline.createBiquadFilter();
      rev1108Out.type = 'lowpass'; rev1108Out.frequency.value = 24000; rev1108Out.Q.value = 0.5;

      reverbBus.connect(rvHP).connect(rvDriveGain).connect(rvDriver).connect(convolver).connect(rvMakeup).connect(rvTone).connect(rvOutGain);
      
      const mSum = offline.createGain();
      mSum.gain.value = this.masterSum.gain.value; 
      
      // INSERT REVERB 1108
      rvOutGain.connect(rev1108In).connect(rev1108Shaper).connect(rev1108Out).connect(mSum);
      
      const nBuf = offline.createBuffer(1, dur * sr, sr);
      const nData = nBuf.getChannelData(0);
      for(let i=0; i<nData.length; i++) nData[i] = (Math.random() * 2 - 1) * 0.11;
      const nSrc = offline.createBufferSource();
      nSrc.buffer = nBuf;
      const nGain = offline.createGain();
      nGain.gain.value = this.noiseGain.gain.value;
      nSrc.connect(nGain).connect(mSum);
      nSrc.start(0);

      const mL = offline.createGain();
      mL.gain.value = this.masterMainGainL.gain.value;
      const mR = offline.createGain();
      mR.gain.value = this.masterMainGainR.gain.value;
      
      chPosts[0].connect(mL);
      chPosts[1].connect(mR);
      
      outFilter.connect(mL);
      outFilter.connect(mR);
      
      const mMerger = offline.createChannelMerger(2);
      mL.connect(mMerger, 0, 0);
      mR.connect(mMerger, 0, 1);
      
      mMerger.connect(mSum);
      
      // SCULLY MODEL EXPORT (MASTER BUS)
      const scullyIn = offline.createBiquadFilter();
      scullyIn.type = 'highpass'; scullyIn.frequency.value = 10; scullyIn.Q.value = 0.6; // Relaxed to 10Hz
      
      const scullyShaper = offline.createWaveShaper();
      scullyShaper.curve = this.scullyCurve;
      scullyShaper.oversample = '4x';
      
      const scullyOut = offline.createBiquadFilter();
      scullyOut.type = 'lowpass'; scullyOut.frequency.value = 22000; scullyOut.Q.value = 0.5;
      
      const ph = offline.createGain();
      ph.gain.value = this.phaseGain.gain.value;
      
      // CONNECT SCULLY
      mSum.connect(scullyIn).connect(scullyShaper).connect(scullyOut).connect(ph);
      
      const mLow = offline.createBiquadFilter();
      mLow.type = 'lowshelf';
      mLow.frequency.value = this.masterLow.frequency.value;
      mLow.gain.value = this.masterLow.gain.value;
      
      const mMid = offline.createBiquadFilter();
      mMid.type = 'peaking';
      mMid.frequency.value = this.masterMid.frequency.value; 
      mMid.Q.value = 1.08; 
      mMid.gain.value = this.masterMid.gain.value;
      
      const mHigh = offline.createBiquadFilter();
      mHigh.type = 'highshelf';
      mHigh.frequency.value = this.masterHigh.frequency.value;
      mHigh.Q.value = this.masterHigh.Q.value; // ENSURE THIS IS ADDED
      mHigh.gain.value = this.masterHigh.gain.value;
      
      const sub = offline.createBiquadFilter();
      sub.type = 'highpass';
      sub.frequency.value = this.masterSubCut.frequency.value;
      
      const bDrive = offline.createGain();
      bDrive.gain.value = this.busDrive.gain.value;
      
      const bSat = offline.createWaveShaper();
      bSat.curve = this.busSaturationCurve;
      bSat.oversample = '4x';
      
      const limiter = offline.createWaveShaper();
      limiter.curve = this.limiterCurve;
      limiter.oversample = '4x';

      const mix = this.currentMix;
      const wetMaster = offline.createGain();
      wetMaster.gain.value = mix;
      
      ph.connect(mLow).connect(mMid).connect(mHigh).connect(sub).connect(bDrive).connect(bSat).connect(limiter).connect(wetMaster).connect(offline.destination);
      
      const dryMaster = offline.createGain();
      dryMaster.gain.value = 1.0 - mix;
      
      // DRY SIGNAL LATENCY COMPENSATION (FIXED)
      // Create 4 identical identity-curve waveshapers to match the wet path latency
      const dryFix1 = offline.createWaveShaper();
      dryFix1.curve = this.identityCurve;
      dryFix1.oversample = '4x';
      
      const dryFix2 = offline.createWaveShaper();
      dryFix2.curve = this.identityCurve;
      dryFix2.oversample = '4x';
      
      const dryFix3 = offline.createWaveShaper();
      dryFix3.curve = this.identityCurve;
      dryFix3.oversample = '4x';

      const dryFix4 = offline.createWaveShaper();
      dryFix4.curve = this.identityCurve;
      dryFix4.oversample = '4x';
      
      dryFix1.connect(dryFix2).connect(dryFix3).connect(dryFix4);

      src.connect(dryFix1);
      dryFix4.connect(dryMaster).connect(offline.destination);
      
      src.start(0);
      const rendered = await offline.startRendering();
      return this.bufferToWave(rendered);
  }

  async resumeContext() { 
    if (this.ctx.state !== 'running') {
        try { await this.ctx.resume(); } catch(e) { console.warn("Audio Context resume failed", e); }
    } 
  }
  
  async loadFile(file: File): Promise<AudioBuffer> {
    await this.resumeContext();
    const arrayBuffer = await file.arrayBuffer();
    this.audioBuffer = await this.ctx.decodeAudioData(arrayBuffer);
    this.stop();
    return this.audioBuffer;
  }

  play() {
    if (this.isPlaying || !this.audioBuffer) return;
    this.resumeContext(); 
    
    this.source = this.ctx.createBufferSource();
    this.source.buffer = this.audioBuffer;
    this.source.loop = true;
    
    // SAFETY: Disconnect sourceGain to prevent accumulated connections to downstream nodes
    this.sourceGain.disconnect();
    
    const splitter = this.ctx.createChannelSplitter(2);
    // Re-connect sourceGain (Wet Path Input) to the new splitter
    this.sourceGain.connect(splitter);
    
    // Connect Source to Wet Path (Padded)
    this.source.connect(this.sourceGain);
    
    // Connect Source to Dry Path (Dummy Stages -> Gain)
    this.source.connect(this.dryFix1);
    
    splitter.connect(this.channels[0].input, 0); 
    if (this.audioBuffer.numberOfChannels > 1) {
        splitter.connect(this.channels[1].input, 1);
    } else {
        splitter.connect(this.channels[1].input, 0);
    }

    this.startTime = this.ctx.currentTime - this.pauseOffset;
    this.source.start(0, this.pauseOffset % this.audioBuffer.duration);
    this.isPlaying = true;
  }

  pause() { 
    if (!this.isPlaying) return; 
    this.source?.stop(); 
    this.pauseOffset = (this.ctx.currentTime - this.startTime) % (this.audioBuffer?.duration || 1);
    this.isPlaying = false; 
  }

  stop() { 
    if (this.source) try { this.source.stop(); } catch (e) {} 
    this.pauseOffset = 0; 
    this.isPlaying = false; 
  }

  seek(time: number) {
    if (!this.audioBuffer) return;
    const duration = this.audioBuffer.duration;
    let target = Math.max(0, Math.min(time, duration));
    const wasPlaying = this.isPlaying;
    if (this.source) { try { this.source.stop(); } catch(e) {} }
    this.pauseOffset = target;
    if (wasPlaying) this.play();
  }

  setBypass(bypassed: boolean) {
    this.isBypassed = bypassed;
    this.updateGains();
  }
  
  updateGains() {
      const t = this.ctx.currentTime;
      if (this.isBypassed) {
          // Force Pure Dry
          this.wetGain.gain.setTargetAtTime(0, t, 0.05);
          this.dryGain.gain.setTargetAtTime(1, t, 0.05); 
      } else {
          // Mix
          this.wetGain.gain.setTargetAtTime(this.currentMix, t, 0.05);
          this.dryGain.gain.setTargetAtTime(1.0 - this.currentMix, t, 0.05);
      }
  }

  getDuration(): number { return this.audioBuffer?.duration || 0; }

  getCurrentPosition() {
    if (!this.isPlaying) return this.pauseOffset;
    return (this.ctx.currentTime - this.startTime) % (this.audioBuffer?.duration || 1);
  }

  updateChannel(idx: number, state: ChannelState) {
    const ch = this.channels[idx];
    if (!ch) return;
    const t = this.ctx.currentTime;
    
    // GAIN STAGING LOGIC:
    let padFactor = 1.0;
    if (state.pad === 10) padFactor = 3.162;
    else if (state.pad === 4) padFactor = 1.585;
    else if (state.pad === -10) padFactor = 0.316;

    const normalizedLevel = state.level / 5; // 0..2
    const driveGain = Math.pow(normalizedLevel, 2.4) * 20.0; 
    const finalPreAmpGain = driveGain * padFactor;
    ch.preAmpGain.gain.setTargetAtTime(finalPreAmpGain, t, 0.05);
    ch.postGain.gain.setTargetAtTime(2.0, t, 0.05);
    ch.high.gain.setTargetAtTime(state.high, t, 0.05);
    ch.low.gain.setTargetAtTime(state.low, t, 0.05);

    // Update Low Cut (HPF 96Hz)
    // If OFF, we set freq to something negligible like 10Hz to effectively bypass it 
    // without clicking from changing filter type
    ch.lowCutFilter.frequency.setTargetAtTime(state.lowCut ? 96 : 10, t, 0.05);
    
    ch.reverbSend.gain.setTargetAtTime(Math.pow(state.effectsSend / 10, 2), t, 0.05);
    ch.monitorSend.gain.setTargetAtTime(Math.pow(state.monitorSend / 10, 2), t, 0.05);
  }

  updateMaster(state: MasterState) {
    const t = this.ctx.currentTime;
    
    this.currentMix = state.masterMix / 10;
    this.updateGains();

    // Master Fader:
    const masterGain = (state.masterMain / 5);
    this.masterMainGainL.gain.setTargetAtTime(masterGain, t, 0.05);
    this.masterMainGainR.gain.setTargetAtTime(masterGain, t, 0.05);
    
    this.masterMonitorGain.gain.setTargetAtTime(Math.pow(state.masterMonitor / 10, 2), t, 0.05);
    
    this.masterLow.gain.setTargetAtTime(state.masterLow, t, 0.05);
    this.masterMid.gain.setTargetAtTime(state.masterMid, t, 0.05);
    this.masterHigh.gain.setTargetAtTime(state.masterHigh, t, 0.05);
    
    const freqMap: Record<string, number> = { '0.7k': 700, '1.0k': 1000, '1.4k': 1400 };
    const midFreq = freqMap[state.masterMidFreq] || 1000;
    this.masterMid.frequency.setTargetAtTime(midFreq, t, 0.05);

    const reverbRet = (state.masterReverb / 5) * 0.7;
    this.reverbGain.gain.setTargetAtTime(reverbRet, t, 0.05);
    
    const driveVal = 0.05 + (state.reverbDrive / 10) * 0.25;
    this.reverbDriverGain.gain.setTargetAtTime(driveVal, t, 0.05);

    const toneFreq = 500 + (state.reverbContour / 10) * 5500;
    this.reverbTone.frequency.setTargetAtTime(toneFreq, t, 0.05);

    const targetTime = 0.5 + (state.reverbTime / 10) * 4.0;
    if (Math.abs(targetTime - this.currentReverbTime) > 0.2) {
        this.loadSpringReverb(targetTime);
    }
  }
}
