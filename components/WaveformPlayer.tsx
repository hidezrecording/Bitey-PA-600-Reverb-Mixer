
import React, { useEffect, useRef, useState } from 'react';
import { AudioEngine } from '../services/AudioEngine';

interface WaveformPlayerProps {
  engine: AudioEngine | null;
  buffer: AudioBuffer | null;
  onLoadFile: () => void;
  isPlaying: boolean;
  onTogglePlay: () => void;
}

const WaveformPlayer: React.FC<WaveformPlayerProps> = ({ engine, buffer, onLoadFile, isPlaying, onTogglePlay }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [currentTime, setCurrentTime] = useState(0);
  const containerRef = useRef<HTMLDivElement>(null);

  // Animation Loop for playhead
  useEffect(() => {
    let animId: number;
    const loop = () => {
      if (engine && buffer) {
        setCurrentTime(engine.getCurrentPosition());
      }
      animId = requestAnimationFrame(loop);
    };
    loop();
    return () => cancelAnimationFrame(animId);
  }, [engine, buffer]);

  // Draw Waveform
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !buffer) return;
    
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Handle high DPI
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);
    
    const width = rect.width;
    const height = rect.height;
    
    // Clear background
    ctx.fillStyle = '#111';
    ctx.fillRect(0, 0, width, height);
    
    // Draw Grid
    ctx.strokeStyle = '#222';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for(let i=0; i<width; i+=20) { ctx.moveTo(i,0); ctx.lineTo(i,height); }
    ctx.stroke();

    // Draw Waveform (Simple downsampling)
    const data = buffer.getChannelData(0); // Use left channel
    const step = Math.ceil(data.length / width);
    const amp = height / 2;

    ctx.beginPath();
    ctx.strokeStyle = '#4ade80'; // Retro green terminal color
    ctx.lineWidth = 1;

    for (let i = 0; i < width; i++) {
        let min = 1.0;
        let max = -1.0;
        
        // Find peak in chunk
        for (let j = 0; j < step; j++) {
            const datum = data[(i * step) + j];
            if (datum < min) min = datum;
            if (datum > max) max = datum;
        }
        
        // Draw vertical line for peak
        ctx.moveTo(i, (1 + min) * amp);
        ctx.lineTo(i, (1 + max) * amp);
    }
    ctx.stroke();

    // Add CRT scanline effect
    ctx.fillStyle = 'rgba(0,0,0,0.1)';
    for(let i=0; i<height; i+=2) {
        ctx.fillRect(0, i, width, 1);
    }

  }, [buffer]); // Redraw only when buffer changes (resize handling omitted for simplicity)

  const handleSeek = (e: React.MouseEvent) => {
    if (!engine || !buffer || !containerRef.current) return;
    const rect = containerRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const pct = Math.max(0, Math.min(1, x / rect.width));
    const time = pct * buffer.duration;
    engine.seek(time);
  };

  const formatTime = (t: number) => {
    const m = Math.floor(t / 60);
    const s = Math.floor(t % 60);
    const ms = Math.floor((t % 1) * 100);
    return `${m.toString().padStart(2,'0')}:${s.toString().padStart(2,'0')}.${ms.toString().padStart(2,'0')}`;
  };

  return (
    <div className="w-full h-[120px] bg-[#1a1a1a] border-[4px] border-[#333] rounded-[8px] shadow-[0_10px_20px_black] flex flex-row overflow-hidden relative mt-2">
       {/* Rack Ears/Sides */}
       <div className="w-[12px] h-full bg-[#111] border-r border-[#444] flex flex-col items-center justify-between py-2">
           <div className="w-1.5 h-1.5 rounded-full bg-[#000] border border-[#555]"></div>
           <div className="w-1.5 h-1.5 rounded-full bg-[#000] border border-[#555]"></div>
       </div>

       {/* Control Panel */}
       <div className="w-[140px] bg-[#222] flex flex-col p-3 gap-2 border-r border-[#000] shadow-[inset_-2px_0_5px_rgba(0,0,0,0.5)] z-10">
            {/* Display Time */}
            <div className="bg-[#000] border border-[#444] rounded px-1 py-0.5 mb-1 shadow-[inset_0_2px_4px_rgba(0,0,0,0.8)]">
                <div className="font-['Michroma'] text-[#4ade80] text-[10px] text-center tracking-widest tabular-nums">
                    {formatTime(currentTime)}
                </div>
            </div>

            <button 
                onClick={onTogglePlay}
                className={`flex-1 rounded-[4px] font-['Michroma'] text-[10px] font-bold tracking-wider transition-all
                    ${isPlaying 
                        ? 'bg-orange-500 text-black shadow-[inset_0_2px_5px_rgba(0,0,0,0.4)] translate-y-[1px]' 
                        : 'bg-[#444] text-white shadow-[0_2px_0_#111] hover:bg-[#555] active:translate-y-[2px] active:shadow-none'}
                `}
            >
                {isPlaying ? 'STOP' : 'PLAY'}
            </button>
            
            <button 
                onClick={onLoadFile}
                className="h-[24px] bg-[#333] text-[#aaa] text-[9px] font-['Michroma'] rounded-[2px] border border-[#444] hover:text-white hover:border-[#666] shadow-sm"
            >
                LOAD TAPE
            </button>
       </div>

       {/* Waveform Area */}
       <div 
         ref={containerRef}
         className="flex-1 bg-[#050505] relative cursor-crosshair group"
         onClick={handleSeek}
       >
          <canvas ref={canvasRef} className="w-full h-full block" />
          
          {/* Glass Reflection */}
          <div className="absolute inset-0 pointer-events-none bg-gradient-to-tr from-transparent via-white/5 to-transparent"></div>
          
          {/* Playhead */}
          <div 
             className="absolute top-0 bottom-0 w-[1px] bg-orange-500 shadow-[0_0_4px_orange] pointer-events-none transition-transform duration-75 ease-linear"
             style={{ 
                 left: 0,
                 transform: `translateX(${buffer ? (currentTime / buffer.duration) * (containerRef.current?.offsetWidth || 0) : 0}px)` 
             }}
          ></div>

          {/* Overlay Text when empty */}
          {!buffer && (
              <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
                  <span className="text-[#444] font-['Michroma'] text-xs tracking-[0.2em] animate-pulse">NO TAPE LOADED</span>
              </div>
          )}
       </div>

       {/* Rack Right Ear */}
       <div className="w-[12px] h-full bg-[#111] border-l border-[#444] flex flex-col items-center justify-between py-2">
           <div className="w-1.5 h-1.5 rounded-full bg-[#000] border border-[#555]"></div>
           <div className="w-1.5 h-1.5 rounded-full bg-[#000] border border-[#555]"></div>
       </div>
    </div>
  );
};

export default WaveformPlayer;
