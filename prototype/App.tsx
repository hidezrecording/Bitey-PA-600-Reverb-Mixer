
import React, { useState, useEffect, useRef, useLayoutEffect, useCallback } from 'react';
import { AudioEngine } from './services/AudioEngine';
import Knob from './components/Knob';
import VUMeter from './components/VUMeter';
import WaveformPlayer from './components/WaveformPlayer';
import { ChannelState, MasterState } from './types';

const INIT_CH = (lbl: string): ChannelState => ({ label: lbl, effectsSend: 5, monitorSend: 0, high: 0, low: 0, level: 5, pad: 0, lowCut: false });

// --- UI Sub-Components Defined OUTSIDE App to prevent remounting/flashing ---

const MetalToggleSwitch = ({ 
    values, 
    displayValues,
    current, 
    onChange, 
    label,
    labelSide = 'left',
    labelClassName = "text-gray-400 text-[6px]",
    heightClassName = "h-[42px]"
}: { 
    values: string[],
    displayValues?: React.ReactNode[], 
    current: string, 
    onChange: (v: string) => void, 
    label?: string,
    labelSide?: 'left' | 'right',
    labelClassName?: string,
    heightClassName?: string
}) => {
     const idx = values.indexOf(current);
     
     // Determine offset dynamically based on number of values
     // Maps index 0..(n-1) to -6..6
     const range = 12; // Total travel distance in pixels (-6 to 6)
     const step = values.length > 1 ? range / (values.length - 1) : 0;
     const offset = -6 + (idx * step);
     
     return (
        <div className="flex flex-col items-center gap-1 group">
             {label && <div className="text-[7px] text-white font-bold tracking-wider font-['Michroma'] mb-1 opacity-90 drop-shadow-md">{label}</div>}
             <div className="flex h-full gap-2 items-center justify-center">
                 {/* Switch Body */}
                 <div 
                    className={`relative w-9 h-9 flex items-center justify-center cursor-pointer ${labelSide === 'right' ? 'order-first' : 'order-last'}`}
                    onClick={(e) => {
                        e.stopPropagation();
                        const next = values[(idx + 1) % values.length];
                        onChange(next);
                    }}
                 >
                    {/* Mounting Nut */}
                    <div className="absolute w-8 h-8 rounded-full bg-gradient-to-br from-[#ccc] to-[#666] border border-[#444] shadow-[0_3px_5px_black]"></div>
                    <div className="absolute w-6 h-6 rounded-full bg-[#1a1a1a] shadow-[inset_0_1px_3px_black]"></div>
                    
                    {/* Bat Handle Tip */}
                    <div 
                        className="absolute w-3 h-3 rounded-full bg-gradient-to-br from-[#eee] to-[#888] shadow-[0_2px_4px_black] z-10 transition-transform duration-150"
                        style={{ 
                            transform: `translateY(${offset}px)` 
                        }}
                    >
                         <div className="absolute top-[20%] left-[20%] w-[30%] h-[30%] bg-white/80 blur-[0.5px] rounded-full"></div>
                    </div>
                 </div>

                 {/* Labels */}
                 <div className={`flex flex-col justify-between ${heightClassName} font-['Michroma'] leading-none py-1 ${labelClassName} ${labelSide === 'right' ? 'text-left' : 'text-right'}`}>
                     {(displayValues || values).map((v, i) => (
                         <span key={i} className={values[i]===current ? 'text-[#4ade80] drop-shadow-[0_0_4px_rgba(74,222,128,1)]' : ''}>{v}</span>
                     ))}
                 </div>
             </div>
        </div>
     );
};

const ToggleSwitch = ({ isOn, onToggle, label }: { isOn: boolean, onToggle: () => void, label?: string }) => (
    <div className="flex flex-col items-center group relative z-50">
        {label && <div className="text-[7px] text-white font-bold tracking-widest mb-1 select-none opacity-90 group-hover:opacity-100 transition-opacity font-['Michroma'] drop-shadow-[0_0_2px_rgba(255,255,255,0.5)]">{label}</div>}
        <div 
            className="relative cursor-pointer w-10 h-12 flex justify-center items-center"
            onClick={(e) => { e.stopPropagation(); onToggle(); }}
        >
            <div className={`absolute w-3 h-6 bg-black blur-sm opacity-50 transition-all duration-200 ${isOn ? 'top-6 translate-y-1' : 'top-1 translate-y-[-2px]'}`}></div>
            <div className="absolute w-8 h-8 rounded-full bg-gradient-to-b from-[#555] to-[#222] shadow-[0_2px_4px_black] border border-[#666]"></div>
            <div className="absolute w-6 h-6 bg-[#888] flex items-center justify-center shadow-[inset_0_-2px_4px_rgba(0,0,0,0.5)]" 
                 style={{ 
                     clipPath: 'polygon(50% 0%, 100% 25%, 100% 75%, 50% 100%, 0% 75%, 0% 25%)',
                     background: 'linear-gradient(135deg, #bbb 0%, #666 50%, #444 100%)'
                 }}
            >
               <div className="w-4 h-4 rounded-full bg-[#111] shadow-[inset_0_2px_2px_rgba(0,0,0,0.8)]"></div>
            </div>
            <div 
                className={`absolute w-3 h-10 bg-gradient-to-r from-[#888] via-[#e0e0e0] to-[#888] rounded-full transition-all duration-150 z-20 shadow-[0_0_2px_rgba(0,0,0,0.5),inset_0_-2px_4px_rgba(0,0,0,0.5)]`}
                style={{ 
                    top: '50%',
                    transformOrigin: '50% 50%',
                    transform: `translateY(-50%) rotateX(${isOn ? '30deg' : '-30deg'}) scaleY(${isOn ? 0.9 : 0.9}) perspective(500px)`,
                    marginTop: isOn ? '-8px' : '8px' 
                }}
            >
               <div className="absolute top-2 left-1/2 -translate-x-1/2 w-1 h-6 bg-white/60 blur-[1px] rounded-full mix-blend-overlay"></div>
            </div>
        </div>
    </div>
);

const DistressOverlay = () => (
    <div className="absolute inset-0 pointer-events-none z-0 overflow-hidden rounded-[12px]">
        <div className="absolute inset-0 opacity-10 bg-[url('https://www.transparenttextures.com/patterns/scratched-metal.png')] mix-blend-overlay"></div>
        <div className="absolute inset-0 bg-[radial-gradient(circle_at_center,transparent_50%,rgba(0,0,0,0.4)_100%)]"></div>
        {/* Replaced complex SVG filter with simple noise texture for Safari compatibility */}
        <div className="absolute inset-0 opacity-5 bg-[url('https://www.transparenttextures.com/patterns/noise.png')]"></div>
    </div>
);

const PanelBox = ({ children, className = "", title = "" }: { children?: React.ReactNode, className?: string, title?: string }) => (
    <div className={`relative border-[6px] border-[#f2f2f2] rounded-[16px] bg-[#1a1a1a] shadow-[0_2px_10px_rgba(0,0,0,0.8)] overflow-hidden ${className}`}>
      {title && (
         <div className="absolute top-2 w-full text-center pointer-events-none z-10">
            <span className="bg-[#1a1a1a] px-2 text-[10px] text-white font-bold tracking-widest leading-none border border-[#333] shadow-md font-['Michroma'] drop-shadow-[0_0_3px_rgba(255,255,255,0.6)]">
              {title}
            </span>
         </div>
      )}
      {children}
    </div>
);

const BoardTape = () => (
    <div 
        className="relative h-10 w-48 flex items-center justify-center z-30 opacity-90 hover:opacity-100 transition-opacity"
        style={{ transform: 'rotate(-1.5deg) scale(0.85)', transformOrigin: 'center' }}
    >
        <div 
            className="absolute inset-0 bg-[#e8e0c5] shadow-[1px_2px_4px_rgba(0,0,0,0.4)]"
            style={{
                clipPath: 'polygon(2% 4%, 5% 0%, 95% 2%, 98% 5%, 100% 95%, 96% 100%, 4% 98%, 0% 90%)',
                opacity: 0.95
            }}
        ></div>
        <div className="absolute inset-0 opacity-20 bg-[url('https://www.transparenttextures.com/patterns/paper-fibers.png')] mix-blend-multiply pointer-events-none"></div>
        <div className="absolute top-0 left-10 w-[1px] h-full bg-black/10"></div>
        <div className="absolute top-0 right-12 w-[2px] h-full bg-black/5"></div>
        <span className="relative z-10 text-black text-2xl font-['Permanent_Marker'] -rotate-1 opacity-80 whitespace-nowrap">
            reverb mixer
        </span>
    </div>
);

const ScreenLine = () => (
    <div 
      className="w-full h-[4px] my-3 relative z-0 mb-6" 
      style={{
          background: '#f0f0f0',
          opacity: 1.0,
          boxShadow: '0 1px 2px rgba(0,0,0,0.5)'
      }}
    ></div>
);

const PowerJewel = ({ isOn }: { isOn: boolean }) => (
    <div className="relative w-14 h-14 rounded-full border-[3px] border-[#555] shadow-[0_2px_4px_black] bg-[#000] overflow-hidden">
        <div className={`absolute inset-0 opacity-100 transition-all duration-300 ${isOn ? 'brightness-125' : 'brightness-50'}`}
             style={{
                background: isOn 
                 ? 'radial-gradient(circle at 40% 40%, #ffdd88 0%, #ffaa00 40%, #cc5500 100%)'
                 : 'radial-gradient(circle at 40% 40%, #552200 0%, #331100 60%, #110500 100%)'
             }}>
        </div>
        <div className="absolute inset-0 opacity-40 mix-blend-overlay"
             style={{
                 background: 'conic-gradient(from 0deg, transparent 0deg, #000 10deg, transparent 20deg, transparent 180deg, #000 190deg, transparent 200deg)'
             }}></div>
         <div className="absolute inset-0 opacity-40 mix-blend-overlay rotate-90"
             style={{
                 background: 'conic-gradient(from 0deg, transparent 0deg, #fff 10deg, transparent 20deg, transparent 180deg, #fff 190deg, transparent 200deg)'
             }}></div>
        {isOn && <div className="absolute inset-[20%] rounded-full bg-orange-400 blur-md opacity-60 animate-pulse"></div>}
        <div className="absolute top-2 left-3 w-4 h-2 bg-white blur-[2px] opacity-70 rotate-[-45deg]"></div>
    </div>
);

const KnobContainer = ({ label, children, label2 = "", labelSize = "text-[10px]" }: { label: string, children?: React.ReactNode, label2?: string, labelSize?: string }) => (
    <div className="flex flex-col items-center justify-start h-[96px] w-full relative">
       {label2 ? (
         <div className="flex w-full justify-between px-3 text-[7px] text-white font-bold tracking-wider mb-1 absolute top-1 font-['Michroma'] drop-shadow-[0_0_2px_rgba(255,255,255,0.6)]">
            <span>{label}</span><span>{label2}</span>
         </div>
       ) : (
         <span className={`${labelSize} text-white tracking-widest absolute top-1 font-['Michroma'] font-bold drop-shadow-[0_0_2px_rgba(255,255,255,0.7)]`}>{label}</span>
       )}
       <div className="mt-10 relative z-40 w-full flex justify-center">
         {children}
       </div>
    </div>
);

const ChannelStrip = ({ index, data, label, topLabel, disabled, onUpdate }: { index: number, data: ChannelState, label: string, topLabel: string, disabled?: boolean, onUpdate: (i:number, k:keyof ChannelState, v:any)=>void }) => (
    <PanelBox className={`w-[145px] flex flex-col items-center py-2 h-full bg-[#181818] transition-all duration-300 ${disabled ? 'opacity-60 grayscale-[0.5]' : ''}`} title={topLabel}>
        <div className="flex flex-col w-full mt-4 gap-1 relative">
            <KnobContainer label="REVERB">
                <Knob value={data.effectsSend} onChange={v => onUpdate(index, 'effectsSend', v)} min={0} max={10} size={50} scaleType="0-10" />
            </KnobContainer>
            <KnobContainer label="HIGH">
                <Knob value={data.high} onChange={v => onUpdate(index, 'high', v)} min={-15} max={15} size={50} scaleType="eq" />
            </KnobContainer>
            
            <KnobContainer label="LOW">
                <Knob value={data.low} onChange={v => onUpdate(index, 'low', v)} min={-15} max={15} size={50} scaleType="eq" />
            </KnobContainer>
        </div>
        <div className="mt-auto flex flex-col items-center w-full">
            <ScreenLine />
            <div className="mb-2 relative z-40">
                <Knob value={data.level} onChange={v => onUpdate(index, 'level', v)} min={0} max={10} size={90} variant="skirted" scaleType="0-10" />
            </div>
            <div className="w-full flex justify-center items-center pb-4 relative">
                 {/* LOW CUT SWITCH (LEFT) */}
                 <div className="absolute left-1 top-[-4px] transform scale-75 origin-top-left z-50 flex flex-col items-start">
                    <MetalToggleSwitch 
                        values={['OFF', 'ON']} 
                        displayValues={[
                            // OFF: Flat - pushed down further to clear pot markings
                            <div className="w-3 h-[2px] bg-current opacity-80 translate-y-[6px]"></div>,
                            // ON: Cut (Correct Bode shape /‾‾)
                            <svg width="12" height="10" viewBox="0 0 12 10" fill="none" stroke="currentColor" strokeWidth="2">
                                <path d="M0 9 L5 1 L12 1" strokeLinecap="round" strokeLinejoin="round"/>
                            </svg>
                        ]}
                        current={data.lowCut ? 'ON' : 'OFF'} 
                        onChange={(v) => onUpdate(index, 'lowCut', v === 'ON')}
                        labelSide="right"
                        labelClassName="text-gray-400"
                        heightClassName="h-[38px]"
                    />
                    <span className="text-[9px] text-gray-400 font-bold font-['Michroma'] ml-[7px] mt-0.5 opacity-90 drop-shadow-md">96Hz</span>
                 </div>

                 <span className="text-[22px] text-white font-['Michroma'] font-bold leading-none drop-shadow-[0_0_5px_rgba(255,255,255,0.8)]">{label}</span>
                 
                 {/* PAD SWITCH (RIGHT) */}
                 <div className="absolute right-1 top-[-4px] transform scale-75 origin-top-right z-50 flex flex-col items-end">
                    <MetalToggleSwitch 
                        label={undefined}
                        values={['+10', '+4', '0', '-10']} 
                        current={data.pad > 0 ? `+${data.pad}` : data.pad.toString()} 
                        onChange={(v) => onUpdate(index, 'pad', parseInt(v))} 
                        labelSide="left"
                        labelClassName="text-gray-400 text-[9px] font-bold"
                        heightClassName="h-[38px]"
                    />
                    <span className="text-[9px] text-gray-400 font-bold font-['Michroma'] mr-[7px] mt-0.5 opacity-90 drop-shadow-md">PAD</span>
                 </div>
            </div>
        </div>
    </PanelBox>
);

const MasterStrip = ({ disabled, master, onUpdate }: { disabled?: boolean, master: MasterState, onUpdate: (k:keyof MasterState, v:any)=>void }) => (
    <PanelBox className={`w-[145px] flex flex-col items-center py-2 h-full bg-[#181818] transition-all duration-300 ${disabled ? 'opacity-60 grayscale-[0.5]' : ''}`} title="MASTER">
       <div className="flex flex-col w-full mt-4 gap-1">
           <KnobContainer label="HIGH">
               <Knob value={master.masterHigh} onChange={v => onUpdate('masterHigh', v)} min={-15} max={15} size={50} scaleType="eq" />
           </KnobContainer>
           
           {/* MID font match HIGH/LOW */}
           <KnobContainer label="MID">
               <div className="relative w-full h-full flex justify-center items-center">
                   <Knob value={master.masterMid} onChange={v => onUpdate('masterMid', v)} min={-15} max={15} size={50} scaleType="eq" />
                   <div className="absolute left-[50%] ml-[20px] top-[-33px] transform scale-[0.5] origin-top-left z-50">
                       <MetalToggleSwitch
                            label=""
                            values={['0.7k', '1.0k', '1.4k']}
                            current={master.masterMidFreq}
                            onChange={(v) => onUpdate('masterMidFreq', v)}
                            labelSide="right"
                            labelClassName="text-gray-400 text-[14px] font-bold"
                            heightClassName="h-[60px]"
                       />
                   </div>
               </div>
           </KnobContainer>
           
           <KnobContainer label="LOW">
               <Knob value={master.masterLow} onChange={v => onUpdate('masterLow', v)} min={-15} max={15} size={50} scaleType="eq" />
           </KnobContainer>
       </div>
       <div className="mt-auto flex flex-col items-center w-full">
            <ScreenLine />
            <div className="mb-2 relative z-40">
                 <Knob value={master.masterMain} onChange={v => onUpdate('masterMain', v)} min={0} max={10} size={90} variant="skirted" scaleType="0-10" />
            </div>
            <div className="w-full flex justify-center pb-4">
                 <span className="text-[22px] text-white font-['Michroma'] font-bold leading-none tracking-tight drop-shadow-[0_0_5px_rgba(255,255,255,0.8)]">MAIN</span>
            </div>
       </div>
    </PanelBox>
);

const ReverbStrip = ({ disabled, master, onUpdate }: { disabled?: boolean, master: MasterState, onUpdate: (k:keyof MasterState, v:number)=>void }) => (
    <PanelBox className={`w-[145px] flex flex-col items-center py-2 h-full bg-[#181818] transition-all duration-300 ${disabled ? 'opacity-60 grayscale-[0.5]' : ''}`} title="REVERB">
       <div className="flex flex-col w-full mt-4 gap-1">
           <KnobContainer label="DRIVE">
               <Knob value={master.reverbDrive} onChange={v => onUpdate('reverbDrive', v)} min={0} max={10} size={50} scaleType="0-10" />
           </KnobContainer>
           <KnobContainer label="CONTOUR">
                <Knob value={master.reverbContour} onChange={v => onUpdate('reverbContour', v)} min={0} max={10} size={50} scaleType="0-10" />
           </KnobContainer>
           <KnobContainer label="TIME">
               <Knob value={master.reverbTime} onChange={v => onUpdate('reverbTime', v)} min={0} max={10} size={50} scaleType="0-10" />
           </KnobContainer>
       </div>
       <div className="mt-auto flex flex-col items-center w-full">
            <ScreenLine />
            <div className="mb-2 relative z-40">
                <Knob value={master.masterReverb} onChange={v => onUpdate('masterReverb', v)} min={0} max={10} size={90} variant="skirted" scaleType="0-10" />
            </div>
            <div className="w-full flex justify-center pb-4">
                 <span className="text-[22px] text-white font-['Michroma'] font-bold leading-none tracking-tighter drop-shadow-[0_0_5px_rgba(255,255,255,0.8)]">REVERB</span>
            </div>
       </div>
    </PanelBox>
);


const App: React.FC = () => {
  const [channels, setChannels] = useState<ChannelState[]>([INIT_CH('1'), INIT_CH('2')]);
  const [master, setMaster] = useState<MasterState>({
    auxMonitor: 5, auxMain: 5,
    masterMonitor: 5, masterMain: 5, 
    masterReverb: 5,
    reverbContour: 5, reverbDrive: 5,
    reverbTime: 5,
    masterHigh: 0, masterMid: 0, masterLow: 0,
    masterMidFreq: '1.0k', // Default Helios Type 69 Mid
    masterMix: 10 // Default to 100% Wet
  });
  const [playback, setPlayback] = useState({ isPlaying: false });
  const [audioBuffer, setAudioBuffer] = useState<AudioBuffer | null>(null);
  const [power, setPower] = useState(true);
  const [phaseInvert, setPhaseInvert] = useState(false); // Changed from reverbStereo
  const [scale, setScale] = useState(1);
  const [tapeSize, setTapeSize] = useState<'1/4"' | '1/2"' | '1"'>('1/4"');
  const [tapeSpeed, setTapeSpeed] = useState<'7.5' | '15' | '30'>('7.5');
  const [tapeMix, setTapeMix] = useState(0); // Default to 0 as requested
  
  const engineRef = useRef<AudioEngine | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!engineRef.current) engineRef.current = new AudioEngine();
    const engine = engineRef.current;
    
    engine.setBypass(!power);
    
    if (power) {
        if(channels[0]) engine.updateChannel(0, channels[0]);
        if(channels[1]) engine.updateChannel(1, channels[1]);
        engine.updateMaster(master);
        engine.setTapeParameters(tapeSpeed, tapeMix, false);
        engine.setTapeSize(tapeSize);
        engine.setPhaseInvert(phaseInvert); // Update phase
    }
    
    setPlayback({ isPlaying: engine.isPlaying });
  }, [channels, master, power, tapeSpeed, tapeMix, tapeSize, phaseInvert]);

  useLayoutEffect(() => {
    const resize = () => {
      if (!containerRef.current) return;
      const targetW = 1050; 
      const targetH = 650; 
      const winW = window.innerWidth - 20;
      const winH = window.innerHeight - 20;
      const s = Math.min(winW / targetW, winH / targetH);
      setScale(s);
    };
    window.addEventListener('resize', resize);
    resize();
    return () => window.removeEventListener('resize', resize);
  }, []);

  const getReverbLevel = useCallback(() => {
     if (!engineRef.current || !power) return 0;
     return engineRef.current.getLevels().reverb;
  }, [power]);

  const getMainLevel = useCallback(() => {
     if (!engineRef.current || !power) return 0;
     return engineRef.current.getLevels().main;
  }, [power]);

  const updateCh = (i: number, k: keyof ChannelState, v: number) => {
    const next = [...channels];
    next[i] = { ...next[i], [k]: v };
    setChannels(next);
  };
  const updateMs = (k: keyof MasterState, v: any) => setMaster(p => ({ ...p, [k]: v }));

  const handleLoadFile = async (file: File) => {
    if (!engineRef.current) return;
    try {
        await engineRef.current.resumeContext();
        const buf = await engineRef.current.loadFile(file);
        setAudioBuffer(buf);
        engineRef.current.play();
        setPlayback({ isPlaying: true });
    } catch (e) {
        console.error("Failed to load file", e);
    }
  };

  const handleTogglePlay = async () => {
    if (!engineRef.current) return;
    await engineRef.current.resumeContext();
    if (playback.isPlaying) {
      engineRef.current.pause();
    } else {
      engineRef.current.play();
    }
    setPlayback({ isPlaying: engineRef.current.isPlaying });
  };
  
  const handleInteraction = () => {
      if (engineRef.current && engineRef.current.ctx.state === 'suspended') {
          engineRef.current.ctx.resume().catch(() => {});
      }
  };

  return (
    <div 
      className="fixed inset-0 flex items-center justify-center bg-[#050505]" 
      onPointerDown={handleInteraction} 
    >
      <div 
        ref={containerRef}
        style={{ width: 1050, height: 650, transform: `scale(${scale})` }}
        className="flex flex-col items-center justify-center p-2"
      >
        {/* === MIXER UNIT === */}
        <div className="w-full h-[520px] flex gap-2 relative shadow-[0_50px_100px_black] z-10">
            <DistressOverlay />
            <div className="w-[30px] h-full wood-pattern border-r border-black/80 rounded-l-md relative z-10 shadow-[5px_0_15px_rgba(0,0,0,0.5)]">
                <div className="absolute top-0 bottom-0 right-0 w-[2px] bg-black/30"></div>
            </div>

            {/* Main Controls Container */}
            <div className="flex-1 h-full bg-[#161616] flex flex-col p-2 relative z-20">
            <div className="w-full flex gap-3 h-full justify-between">
                <ChannelStrip index={0} data={channels[0]} label="1" topLabel="LEFT" disabled={!power} onUpdate={updateCh} />
                <ChannelStrip index={1} data={channels[1]} label="2" topLabel="RIGHT" disabled={!power} onUpdate={updateCh} />

                {/* Center Section */}
                <PanelBox className="w-[240px] flex-none flex flex-col items-center py-4 bg-[#222] shadow-inner">
                        <div className="flex-1 w-full flex flex-col items-center justify-start gap-1">
                            
                            {/* Control Cluster */}
                            <div className="w-full flex items-center justify-between px-3 h-[100px] bg-[#000] border-y border-[#333] shadow-inner relative">
                                {/* Left: Tape Controls */}
                                <div className="flex-1 flex flex-row justify-center items-center gap-6 h-full pr-2">
                                    <MetalToggleSwitch 
                                        label="IPS" 
                                        values={['7.5', '15', '30']} 
                                        current={tapeSpeed} 
                                        onChange={(v) => setTapeSpeed(v as any)} 
                                        labelClassName="text-gray-400 text-[8px]"
                                    />
                                    {/* Aligned Echo Knob */}
                                    <div className="flex flex-col items-center justify-start h-full">
                                        <div className="text-[7px] text-white font-bold tracking-wider font-['Michroma'] mb-0.5 mt-[1px] opacity-90 drop-shadow-md">ECHO</div>
                                        <div className="mt-3 relative">
                                            <Knob value={tapeMix} onChange={setTapeMix} min={0} max={10} size={40} scaleType="0-10" />
                                        </div>
                                    </div>
                                </div>
                                
                                {/* Right: Tape Size */}
                                <div className="flex-1 flex flex-row justify-center items-center gap-6 h-full pl-2">
                                     <MetalToggleSwitch 
                                        label="TAPE" 
                                        values={['1/4"', '1/2"', '1"']} 
                                        current={tapeSize} 
                                        onChange={(v) => setTapeSize(v as any)} 
                                        labelClassName="text-gray-400 text-[8px]"
                                    />
                                </div>
                            </div>

                            {/* VU Meters - Moved Up into Reclaimed Space */}
                            <div className="flex flex-col items-center w-full mt-2">
                                <div className={`transition-opacity duration-500 ${!power ? 'opacity-40' : 'opacity-100'}`}>
                                    <VUMeter getValue={getReverbLevel} width={200} height={85} />
                                </div>
                                <span className="text-[10px] text-white font-bold tracking-[0.2em] mt-0.5 mb-1 font-['Michroma'] drop-shadow-[0_0_2px_rgba(255,255,255,0.6)]">REVERB</span>
                            </div>

                            <div className="flex flex-col items-center w-full">
                                <div className={`transition-opacity duration-500 ${!power ? 'opacity-40' : 'opacity-100'}`}>
                                    <VUMeter getValue={getMainLevel} width={200} height={85} />
                                </div>
                                <span className="text-[10px] text-white font-bold tracking-[0.2em] mt-0.5 font-['Michroma'] drop-shadow-[0_0_2px_rgba(255,255,255,0.6)]">MAIN</span>
                            </div>
                        </div>
                        
                        {/* New Mixed Control Section: Centered Tape + Dry/Wet Knob */}
                        <div className="relative w-full h-[60px] mt-1 mb-1 z-30">
                            {/* Tape moved further left (left-4) to align with meter. Text size increased to 2xl. */}
                            <div className="absolute left-[4px] top-[0px] flex items-center justify-start scale-90 origin-left">
                                <BoardTape />
                            </div>
                            
                            {/* Dry/Wet Knob - Repositioned to top-[-4px] to clear the meters and fit within the strip. Label under knob. */}
                            <div className="absolute right-6 top-[-4px] flex flex-col items-center justify-start">
                                <div className="relative mb-1">
                                    <Knob value={master.masterMix} onChange={v => updateMs('masterMix', v)} min={0} max={10} size={28} scaleType="0-10" />
                                </div>
                                <div className="mt-1 text-[7px] text-white font-bold tracking-widest font-['Michroma'] opacity-90 drop-shadow-md text-center leading-tight whitespace-nowrap">
                                    DRY / WET
                                </div>
                            </div>
                        </div>

                        {/* Centered Power Section */}
                        <div className="w-full border-t-[2px] border-[#444] pt-2 grid grid-cols-3 items-end pb-4 relative z-50">
                            {/* Left: Power Switch */}
                            <div className="flex justify-center">
                                <ToggleSwitch isOn={power} onToggle={() => setPower(!power)} label="POWER" />
                            </div>

                            {/* Center: Jewel */}
                            <div className="flex flex-col items-center mb-1">
                                <div className="text-[9px] text-white font-bold tracking-widest mb-1 font-['Michroma'] drop-shadow-[0_0_2px_rgba(255,255,255,0.6)]">ON</div>
                                <PowerJewel isOn={power} />
                            </div>

                            {/* Right: Phase Switch (Replaced Mono Switch) */}
                            <div className="flex justify-center relative flex-col items-center -mt-1">
                                <div className="text-[7px] text-white font-bold tracking-widest mb-0.5 select-none opacity-90 font-['Michroma'] drop-shadow-[0_0_2px_rgba(255,255,255,0.5)] flex items-center justify-center gap-1">
                                    <span className="text-[10px] leading-none">Ø</span><span>NOR</span>
                                </div>
                                <ToggleSwitch isOn={!phaseInvert} onToggle={() => setPhaseInvert(!phaseInvert)} />
                                <div className="absolute top-[100%] -mt-1 text-[7px] text-white font-bold tracking-widest select-none opacity-90 font-['Michroma'] drop-shadow-[0_0_2px_rgba(255,255,255,0.5)] flex items-center justify-center gap-1">
                                    <span className="text-[10px] leading-none">Ø</span><span>REV</span>
                                </div>
                            </div>
                        </div>
                </PanelBox>

                <MasterStrip disabled={!power} master={master} onUpdate={updateMs} />
                <ReverbStrip disabled={!power} master={master} onUpdate={updateMs} />
            </div>
            </div>
            
            <div className="w-[30px] h-full wood-pattern border-l border-black/80 rounded-r-md relative z-10 shadow-[-5px_0_15px_rgba(0,0,0,0.5)]">
                <div className="absolute top-0 bottom-0 left-0 w-[2px] bg-black/30"></div>
            </div>
        </div>

        <div className="relative z-50 w-full">
             <WaveformPlayer 
                engine={engineRef.current}
                buffer={audioBuffer}
                onLoadFile={() => fileInputRef.current?.click()}
                isPlaying={playback.isPlaying}
                onTogglePlay={handleTogglePlay}
            />
        </div>
        
        <input 
            type="file" 
            ref={fileInputRef} 
            className="hidden" 
            accept="audio/*" 
            onClick={(e) => (e.currentTarget.value = '')}
            onChange={e => e.target.files?.[0] && handleLoadFile(e.target.files[0])} 
        />
      </div>
    </div>
  );
};

export default App;
