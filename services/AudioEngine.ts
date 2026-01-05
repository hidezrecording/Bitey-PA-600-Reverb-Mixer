
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
  reverbStereo: boolean = true; // Default to Stereo

  // Input Stage
  sourceGain: GainNode; // Global input pad

  channels: {
    input: GainNode;
    preAmpGain: GainNode;
    shaper: WaveShaperNode;
    rumbleFilter: BiquadFilterNode; // New: Cleans shaper artifacts
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
  
  // Echo Output Filter (12dB/oct slope)
  tapeOutFilter: BiquadFilterNode;

  // Reverb Network (Tube Driven Spring)
  reverbBus: GainNode;
  reverbInputHighPass: BiquadFilterNode; // Converted to Bell Cut
  reverbDriverGain: GainNode; // Dwell/Drive control
  reverbDriver: WaveShaperNode; // 12AT7 Driver emulation
  reverbProcessor: ConvolverNode;
  reverbTone: BiquadFilterNode;
  reverbGain: GainNode;
  reverbMakeup: GainNode;
  
  masterMainGainL: GainNode;
  masterMainGainR: GainNode;
  masterMonitorGain: GainNode;

  // Master Summing & Processing
  masterSum: GainNode;
  masterLow: BiquadFilterNode;
  masterMid: BiquadFilterNode;
  masterHigh: BiquadFilterNode;
  
  // Limiter Chain
  masterSubCut: BiquadFilterNode; // New: Safety filter
  busDrive: GainNode;
  busSaturator: WaveShaperNode;
  masterLimiter: DynamicsCompressorNode;
  
  wetGain: GainNode; 
  dryGain: GainNode; 
  analyzerMain: AnalyserNode;
  analyzerReverb: AnalyserNode; 

  distortionCurve: Float32Array;
  tapeSaturationCurve: Float32Array;
  busSaturationCurve: Float32Array;
  driverTubeCurve: Float32Array; 

  constructor() {
    this.ctx = new (window.AudioContext || (window as any).webkitAudioContext)({
      latencyHint: 'interactive',
      sampleRate: 44100
    });

    this.distortionCurve = this.makeTubeCurve(8192);
    this.tapeSaturationCurve = this.makeTapeCurve(8192);
    this.busSaturationCurve = this.makeBusSaturationCurve(8192);
    this.driverTubeCurve = this.makeDriverCurve(8192);

    // --- Input Stage ---
    // Lowered gain to 0.15 to prevent jump when toggling power
    this.sourceGain = this.ctx.createGain();
    this.sourceGain.gain.value = 0.15; 

    // --- Tape Slap Network ---
    this.tapeSum = this.ctx.createGain();
    this.tapeSum.gain.value = 0.5;

    this.tapeDelay = this.ctx.createDelay(1.0);
    this.tapeDrive = this.ctx.createGain();
    this.tapeShaper = this.ctx.createWaveShaper();
    this.tapeShaper.curve = this.tapeSaturationCurve;
    this.tapeShaper.oversample = '4x';
    
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
    this.reverbBus = this.ctx.createGain();
    this.reverbBus.gain.value = 0.4; 
    
    // CHANGED: From Peaking/600Hz to Highpass/100Hz with gentle Q
    this.reverbInputHighPass = this.ctx.createBiquadFilter();
    this.reverbInputHighPass.type = 'highpass'; 
    this.reverbInputHighPass.frequency.value = 100; // Lowered to 100Hz
    this.reverbInputHighPass.Q.value = 0.4; // Gentler slope (underdamped)

    // Driver Stage 
    this.reverbDriverGain = this.ctx.createGain();
    this.reverbDriver = this.ctx.createWaveShaper();
    this.reverbDriver.curve = this.driverTubeCurve;
    this.reverbDriver.oversample = '4x';

    this.reverbProcessor = this.ctx.createConvolver();
    this.reverbProcessor.normalize = false; 
    
    this.reverbTone = this.ctx.createBiquadFilter();
    this.reverbTone.type = 'lowpass';
    this.reverbTone.frequency.value = 3500; 
    this.reverbTone.Q.value = 0.5;

    this.reverbMakeup = this.ctx.createGain();
    // CHANGED: Boosted makeup gain for louder return
    this.reverbMakeup.gain.value = 1.6; 

    this.reverbGain = this.ctx.createGain();

    this.masterMainGainL = this.ctx.createGain();
    this.masterMainGainR = this.ctx.createGain();
    this.masterMonitorGain = this.ctx.createGain();
    
    // Master Summing
    this.masterSum = this.ctx.createGain();
    this.masterSum.gain.value = 0.6; 

    // Master EQ 
    this.masterLow = this.ctx.createBiquadFilter();
    this.masterLow.type = 'lowshelf';
    this.masterLow.frequency.value = 100; 
    
    this.masterMid = this.ctx.createBiquadFilter();
    this.masterMid.type = 'peaking';
    // Helios Type 69 1kHz Mid Setting
    this.masterMid.frequency.value = 1000; 
    this.masterMid.Q.value = 1.25; // Characteristic "woody" Q of the Helios inductor mid
    
    this.masterHigh = this.ctx.createBiquadFilter();
    this.masterHigh.type = 'highshelf';
    this.masterHigh.frequency.value = 10000;
    
    // Master Sub Cut (Safety Filter)
    this.masterSubCut = this.ctx.createBiquadFilter();
    this.masterSubCut.type = 'highpass';
    this.masterSubCut.frequency.value = 45; 
    this.masterSubCut.Q.value = 0.5; 
    
    // Bus Processing
    this.busDrive = this.ctx.createGain();
    this.busDrive.gain.value = 0.9; 
    
    this.busSaturator = this.ctx.createWaveShaper();
    this.busSaturator.curve = this.busSaturationCurve;

    // Master Limiter 
    this.masterLimiter = this.ctx.createDynamicsCompressor();
    this.masterLimiter.threshold.value = -6.0; 
    this.masterLimiter.knee.value = 12.0; 
    this.masterLimiter.ratio.value = 8.0; 
    this.masterLimiter.attack.value = 0.005; 
    this.masterLimiter.release.value = 0.15; 

    this.wetGain = this.ctx.createGain();
    this.dryGain = this.ctx.createGain();
    this.dryGain.gain.value = 0; 

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
    
    // Space Echo bleed 
    const tapeToReverb = this.ctx.createGain();
    tapeToReverb.gain.value = 0.12; 
    this.tapeOutFilter.connect(tapeToReverb);
    tapeToReverb.connect(this.reverbBus);

    // Reverb Path: Bus -> Clean Filter -> Driver Gain -> Tube Shaper -> Tank (Convolver) -> Tone -> Gain -> Master
    this.reverbBus.connect(this.reverbInputHighPass);
    this.reverbInputHighPass.connect(this.reverbDriverGain);
    
    this.reverbDriverGain.connect(this.reverbDriver);
    this.reverbDriver.connect(this.reverbProcessor);
    
    this.reverbProcessor.connect(this.reverbMakeup);
    this.reverbMakeup.connect(this.reverbTone);
    this.reverbTone.connect(this.reverbGain);
    this.reverbGain.connect(this.analyzerReverb); 
    
    // Connect Reverb to Master Sum (Bypassing Main Fader)
    this.reverbGain.connect(this.masterSum);

    // Master Bus
    const merger = this.ctx.createChannelMerger(2);
    this.masterMainGainL.connect(merger, 0, 0);
    this.masterMainGainR.connect(merger, 0, 1);
    
    // Connect Dry/Echo mix to Master Sum
    merger.connect(this.masterSum);

    // Master Chain: Sum -> EQ -> SubCut -> Drive -> Saturation -> Limiter -> Output
    this.masterSum.connect(this.masterLow);
    this.masterLow.connect(this.masterMid);
    this.masterMid.connect(this.masterHigh);
    
    this.masterHigh.connect(this.masterSubCut); 
    this.masterSubCut.connect(this.busDrive);
    
    this.busDrive.connect(this.busSaturator);
    this.busSaturator.connect(this.masterLimiter);
    
    this.masterLimiter.connect(this.wetGain);

    this.wetGain.connect(this.analyzerMain);
    this.dryGain.connect(this.analyzerMain);
    this.analyzerMain.connect(this.ctx.destination);

    this.setTapeParameters('7.5', 5, true); 
    this.setTapeSize('1/4'); 
    this.loadSpringReverb(this.currentReverbTime);
    this.allocateChannels(2);
  }

  getLevels() {
    return {
        main: this.calculateRMS(this.analyzerMain),
        reverb: this.calculateRMS(this.analyzerReverb)
    };
  }
  
  setReverbStereo(isStereo: boolean) {
      if (this.reverbStereo !== isStereo) {
          this.reverbStereo = isStereo;
          this.loadSpringReverb(this.currentReverbTime);
      }
  }

  setTapeParameters(speed: '7.5' | '15' | '30', mix: number, bypass: boolean) {
      this.tapeSpeed = speed;
      const t = this.ctx.currentTime;
      
      let delayTime = 0.134; // Default to classic slap
      // Adjusted roll-off to 1100Hz for more mid-centric/vintage tone
      const rollOffFreq = 1100; 

      // Adjusted times for authentic slap vibe
      if (speed === '7.5') delayTime = 0.134; // Classic Sun Slap (approx 134ms)
      if (speed === '15') delayTime = 0.085;  // Tight Slap (85ms)
      if (speed === '30') delayTime = 0.042;  // Doubling (42ms)
      
      this.tapeDelay.delayTime.setTargetAtTime(delayTime, t, 0.05);
      
      this.tapeOutFilter.frequency.setTargetAtTime(rollOffFreq, t, 0.1);
      this.tapeOutFilter.Q.setTargetAtTime(0.7, t, 0.1); 
      
      const mixNorm = mix / 10;
      const gainVal = bypass ? 0.0 : (mixNorm * 0.8); 
      this.tapeWet.gain.setTargetAtTime(gainVal, t, 0.05);
  }

  setTapeSize(size: '1/4' | '1/2' | '1') {
      const t = this.ctx.currentTime;
      // Modified frequency ranges for Lowpass (was Bandpass) to ensure low-mid focus
      if (size === '1/4') {
          this.tapeDrive.gain.setTargetAtTime(1.5, t, 0.1);
          this.tapeFilter.frequency.setTargetAtTime(1000, t, 0.1); // Warmest
          this.tapeFilter.Q.setTargetAtTime(0.5, t, 0.1); 
      } 
      else if (size === '1/2') {
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
      // Very gentle saturation
      if (x < -0.5) y = x + 0.2 * (x + 0.5);
      else if (x > 0.5) y = x - 0.2 * (x - 0.5);
      else y = x;
      
      // Soft tanh limit
      y = Math.tanh(y * 1.2);
      curve[i] = y;
    }
    return curve;
  }
  
  // Specific curve for the reverb driver
  private makeDriverCurve(amount: number): Float32Array {
    const n_samples = amount;
    const curve = new Float32Array(n_samples);
    for (let i = 0; i < n_samples; ++i) {
      const x = (i * 2) / n_samples - 1;
      // Extremely gentle soft clip for headroom
      curve[i] = Math.tanh(x * 0.7); 
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
        // Linear until very high levels
        curve[i] = Math.tanh(x * 0.9); 
    }
    return curve;
  }

  // Princeton/Deluxe Style Spring Reverb Algorithm
  private loadSpringReverb(duration: number) {
    this.currentReverbTime = duration;
    const sampleRate = this.ctx.sampleRate;
    const length = Math.floor(sampleRate * duration);
    const buffer = this.ctx.createBuffer(2, length, sampleRate);
    
    // Fender tanks are often long 2-spring units
    // Significantly reduced flutter rate and depth to remove "wah"
    const flutterRate = 0.5; // Was 5.0

    for (let c = 0; c < 2; c++) {
        // If mono mode, just copy channel 0 to channel 1
        if (c === 1 && !this.reverbStereo) {
            const data0 = buffer.getChannelData(0);
            buffer.copyToChannel(data0, 1);
            continue;
        }

        const data = buffer.getChannelData(c);
        let lastNoise = 0; 

        for (let i = 0; i < length; i++) {
            const t = i / sampleRate;
            
            // REMOVED: Transient Chirp/Drip
            let chirp = 0; 

            // 2. Metallic Impulse & Noise (Velvet Noise)
            // Independent Math.random() calls per channel = decorrelated stereo field
            let noise = 0;
            const density = 2000; 
            if (Math.random() < density / sampleRate) {
                noise = (Math.random() * 2 - 1);
            }
            
            // Lighter Low-pass: Changed from 0.95 to 0.4.
            // 0.95 created heavy Brown noise (sub-bass). 0.4 creates brighter texture.
            noise = (noise + lastNoise * 0.4) / 1.4; 
            lastNoise = noise;
            
            // Modulation (Flutter)
            // Drastically reduced depth (0.005) to eliminate wah/chirp artifacts
            const flutter = 1.0 + 0.005 * Math.sin(2 * Math.PI * flutterRate * t + (c * Math.PI));

            const decay = Math.exp(-t * (3.5 - duration * 0.5)); 
            
            // Composite signal (No Chirp)
            let signal = (noise * 1.0);
            signal *= decay * flutter;
            
            // Soft clip the tank output (simulating transducer limits)
            if (signal > 1) signal = 1;
            if (signal < -1) signal = -1;

            // Rescaled Impulse to 0.2
            data[i] = signal * 0.2; 
        }
    }
    this.reverbProcessor.buffer = buffer;
  }

  private allocateChannels(count: number) {
    for (let i = 0; i < count; i++) {
      const input = this.ctx.createGain();
      const preAmpGain = this.ctx.createGain();
      const shaper = this.ctx.createWaveShaper();
      shaper.curve = this.distortionCurve;
      shaper.oversample = '4x'; 

      // NEW: Rumble Filter (Post Shaper)
      const rumbleFilter = this.ctx.createBiquadFilter();
      rumbleFilter.type = 'highpass';
      rumbleFilter.frequency.value = 60; // Standard console HighPass
      rumbleFilter.Q.value = 0.5;

      // Relaxed bandwidth filter: 10k -> 11.5k (15% increase)
      const bandwidthFilter = this.ctx.createBiquadFilter();
      bandwidthFilter.type = 'lowpass';
      bandwidthFilter.frequency.value = 11500; 
      bandwidthFilter.Q.value = 0.5; 

      const low = this.ctx.createBiquadFilter();
      low.type = 'lowshelf';
      low.frequency.value = 100;
      
      const high = this.ctx.createBiquadFilter();
      high.type = 'highshelf';
      high.frequency.value = 8000;
      
      const postGain = this.ctx.createGain();
      const reverbSend = this.ctx.createGain();
      const monitorSend = this.ctx.createGain();

      input.connect(preAmpGain);
      preAmpGain.connect(shaper);
      // Connect new rumble filter after saturation to remove DC offset/mud
      shaper.connect(rumbleFilter);
      rumbleFilter.connect(bandwidthFilter);

      bandwidthFilter.connect(low);
      low.connect(high);

      // Re-ordered chain: Input -> EQ (High) -> PostGain (Fader) -> Reverb Send
      high.connect(postGain);
      
      postGain.connect(reverbSend);
      
      if (i === 0) postGain.connect(this.masterMainGainL);
      else postGain.connect(this.masterMainGainR);
      
      // Connect to Tape Sum (Attenuated Entry) instead of direct Delay
      postGain.connect(this.tapeSum); 

      reverbSend.connect(this.reverbBus);
      monitorSend.connect(this.masterMonitorGain);

      this.channels.push({ input, preAmpGain, shaper, rumbleFilter, bandwidthFilter, high, low, postGain, reverbSend, monitorSend });
    }
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
    
    const splitter = this.ctx.createChannelSplitter(2);
    this.source.connect(this.sourceGain); // Pad input
    this.sourceGain.connect(this.dryGain); // Dry bypass path also padded
    this.sourceGain.connect(splitter); // To Channels
    
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
    const t = this.ctx.currentTime;
    if (bypassed) {
        this.wetGain.gain.setTargetAtTime(0, t, 0.05);
        this.dryGain.gain.setTargetAtTime(1, t, 0.05); // Unity Dry (after pad)
    } else {
        this.wetGain.gain.setTargetAtTime(1, t, 0.05);
        this.dryGain.gain.setTargetAtTime(0, t, 0.05);
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
    
    // Gain Staging Calibrated for Clean Headroom with matched Bypass
    // Source Pad = 0.15
    // PreAmp = 1.0 (Unity)
    ch.preAmpGain.gain.setTargetAtTime(1.0, t, 0.05);

    // PostGain (Fader):
    // Center (5) = 1.0. Total Gain = 0.15 * 1.0 = 0.15
    const faderGain = (state.level / 5) * 1.0;
    ch.postGain.gain.setTargetAtTime(faderGain, t, 0.05);

    ch.high.gain.setTargetAtTime(state.high, t, 0.05);
    ch.low.gain.setTargetAtTime(state.low, t, 0.05);
    
    ch.reverbSend.gain.setTargetAtTime(Math.pow(state.effectsSend / 10, 2), t, 0.05);
    ch.monitorSend.gain.setTargetAtTime(Math.pow(state.monitorSend / 10, 2), t, 0.05);
  }

  updateMaster(state: MasterState) {
    const t = this.ctx.currentTime;
    
    // Master Fader:
    // Path: Input (0.15) -> Ch (1.0) -> Sum (0.6) -> 0.09
    // Target: 0.15 (Unity Bypass).
    // Required Gain: 0.15 / 0.09 = 1.66
    // Lowered multiplier to 0.9 (was 1.1) for softer output taper
    const masterGain = (state.masterMain / 5) * 0.9;
    this.masterMainGainL.gain.setTargetAtTime(masterGain, t, 0.05);
    this.masterMainGainR.gain.setTargetAtTime(masterGain, t, 0.05);
    
    this.masterMonitorGain.gain.setTargetAtTime(Math.pow(state.masterMonitor / 10, 2), t, 0.05);
    
    this.masterLow.gain.setTargetAtTime(state.masterLow, t, 0.05);
    this.masterMid.gain.setTargetAtTime(state.masterMid, t, 0.05);
    this.masterHigh.gain.setTargetAtTime(state.masterHigh, t, 0.05);

    // Reverb Return:
    // Increased Multiplier (was 1.0) -> 1.4 for louder reverb
    const reverbRet = (state.masterReverb / 5) * 1.4;
    this.reverbGain.gain.setTargetAtTime(reverbRet, t, 0.05);
    
    // Drive: Reduced significant gain to prevent clipping
    // Was: 0.1 + (val/10)*0.7 (max 0.8)
    // Now: 0.05 + (val/10)*0.25 (max 0.3)
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
