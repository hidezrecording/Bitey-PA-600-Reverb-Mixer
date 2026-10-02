
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
  const [isExporting, setIsExporting] = useState(false);
  const [showExportModal, setShowExportModal] = useState(false);
  const [exportFilename, setExportFilename] = useState('bitey_master_tape');
  const wasPlayingRef = useRef(false);

  // Animation Loop for playhead
  useEffect(() => {
    let animId: number;
    const loop = () => {
      // Only update from engine if we are NOT dragging (implied if we are playing)
      // or if we simply check engine position.
      if (engine && buffer) {
          // If the user is dragging, the UI is updated by the drag event. 
          // However, the engine might be paused.
          // We can just rely on engine's reported time.
          // Note: When dragging, we pause the engine, so this value stops updating automatically.
          // We manually update state during drag.
          if (engine.isPlaying) {
             setCurrentTime(engine.getCurrentPosition());
          }
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
    ctx.fillStyle = '#080808';
    ctx.fillRect(0, 0, width, height);
    
    // Draw Grid
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for(let i=0; i<width; i+=40) { ctx.moveTo(i,0); ctx.lineTo(i,height); }
    for(let i=0; i<height; i+=20) { ctx.moveTo(0,i); ctx.lineTo(width,i); }
    ctx.stroke();

    // Draw Waveform
    const data = buffer.getChannelData(0); // Use left channel
    const step = Math.ceil(data.length / width);
    const amp = height / 2;

    ctx.beginPath();
    // Slightly brighter green for visibility
    ctx.strokeStyle = '#4ade80'; 
    ctx.lineWidth = 1;

    for (let i = 0; i < width; i++) {
        let min = 1.0;
        let max = -1.0;
        for (let j = 0; j < step; j++) {
            const datum = data[(i * step) + j];
            if (datum < min) min = datum;
            if (datum > max) max = datum;
        }
        ctx.moveTo(i, (1 + min) * amp);
        ctx.lineTo(i, (1 + max) * amp);
    }
    ctx.stroke();
    
    // Add glow
    ctx.shadowBlur = 4;
    ctx.shadowColor = '#4ade80';
    ctx.stroke();
    ctx.shadowBlur = 0;

  }, [buffer]); 

  // Real-time Scrub/Seek handler
  const handlePointerDown = (e: React.PointerEvent) => {
    if (!engine || !buffer || !containerRef.current) return;
    // Removed preventDefault to allow dblclick to fire
    e.stopPropagation(); 

    const target = e.currentTarget as HTMLDivElement;
    target.setPointerCapture(e.pointerId);

    // Pause audio during scrub to prevent stutter/hangs
    wasPlayingRef.current = engine.isPlaying;
    if (wasPlayingRef.current) {
        engine.pause();
    }

    const calculateTime = (clientX: number) => {
        const rect = containerRef.current!.getBoundingClientRect();
        const x = clientX - rect.left;
        const pct = Math.max(0, Math.min(1, x / rect.width));
        return pct * buffer.duration;
    };

    const time = calculateTime(e.clientX);
    engine.seek(time); // Sets offset without playing since we paused
    setCurrentTime(time); // Immediate UI update

    const onMove = (evt: PointerEvent) => {
        evt.preventDefault();
        const t = calculateTime(evt.clientX);
        engine.seek(t);
        setCurrentTime(t);
    };
    
    const onUp = (evt: PointerEvent) => {
        evt.preventDefault();
        target.releasePointerCapture(evt.pointerId);
        target.removeEventListener('pointermove', onMove);
        target.removeEventListener('pointerup', onUp);
        
        // Resume playback if it was playing before
        if (wasPlayingRef.current) {
             engine.play();
        }
    };
    
    target.addEventListener('pointermove', onMove);
    target.addEventListener('pointerup', onUp);
  };

  const openExportDialog = () => {
      if(!buffer) return;
      // Generate a default name with date
      const dateStr = new Date().toISOString().slice(0,10).replace(/-/g,'');
      setExportFilename(`bitey_tape_${dateStr}`);
      setShowExportModal(true);
  }

  const handleExportConfirm = async () => {
      if (!engine || !buffer) return;
      setShowExportModal(false);
      setIsExporting(true);
      
      // Short delay to allow React to render the loading state overlay
      setTimeout(async () => {
          try {
            const blob = await engine.exportProcessedAudio();
            if (blob) {
                const filename = `${exportFilename}.wav`;
                
                // Try to use File System Access API for "Save As" dialog if available
                if ('showSaveFilePicker' in window) {
                    try {
                        const handle = await (window as any).showSaveFilePicker({
                            suggestedName: filename,
                            types: [{
                                description: 'WAV Audio File',
                                accept: {'audio/wav': ['.wav']},
                            }],
                        });
                        const writable = await handle.createWritable();
                        await writable.write(blob);
                        await writable.close();
                        setIsExporting(false);
                        return;
                    } catch (err: any) {
                        if (err.name !== 'AbortError') console.error(err);
                        // If user aborted, stop exporting state
                        if (err.name === 'AbortError') {
                            setIsExporting(false);
                            return;
                        }
                        // If error (not abort), fall through to download method
                    }
                }

                // Fallback: Create generic download link
                const url = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                a.download = filename;
                a.click();
                URL.revokeObjectURL(url);
            }
          } catch (e) {
              console.error("Export failed", e);
          } finally {
              setIsExporting(false);
          }
      }, 100);
  };

  const formatTime = (t: number) => {
    const m = Math.floor(t / 60);
    const s = Math.floor(t % 60);
    const ms = Math.floor((t % 1) * 100);
    return `${m.toString().padStart(2,'0')}:${s.toString().padStart(2,'0')}.${ms.toString().padStart(2,'0')}`;
  };

  return (
    <>
    <div className="w-full h-[120px] bg-[#1a1a1a] border-[4px] border-[#333] rounded-[8px] shadow-[0_10px_20px_black] flex flex-row overflow-hidden relative mt-2 select-none">
       {/* Rack Ears/Sides */}
       <div className="w-[12px] h-full bg-[#111] border-r border-[#444] flex flex-col items-center justify-between py-2">
           <div className="w-1.5 h-1.5 rounded-full bg-[#000] border border-[#555] shadow-sm"></div>
           <div className="w-1.5 h-1.5 rounded-full bg-[#000] border border-[#555] shadow-sm"></div>
       </div>

       {/* Control Panel */}
       <div className="w-[140px] bg-[#222] flex flex-col p-3 gap-2 border-r border-[#000] shadow-[inset_-2px_0_5px_rgba(0,0,0,0.5)] z-10 relative">
            <div className="absolute inset-0 bg-[url('https://www.transparenttextures.com/patterns/stardust.png')] opacity-20 pointer-events-none"></div>
            
            {/* Display Time */}
            <div className="bg-[#050505] border border-[#444] rounded px-1 py-0.5 mb-1 shadow-[inset_0_2px_4px_rgba(0,0,0,0.9)] relative overflow-hidden group">
                <div className="font-['Michroma'] text-[#4ade80] text-[10px] text-center tracking-widest tabular-nums relative z-10 drop-shadow-[0_0_2px_rgba(74,222,128,0.5)]">
                    {formatTime(currentTime)}
                </div>
                <div className="absolute inset-0 bg-green-500/5 animate-pulse pointer-events-none"></div>
            </div>

            <button 
                onClick={onTogglePlay}
                disabled={isExporting}
                className={`flex-1 rounded-[3px] font-['Michroma'] text-[9px] font-bold tracking-wider transition-all border
                    ${isPlaying 
                        ? 'bg-orange-600 border-orange-800 text-black shadow-[inset_0_2px_5px_rgba(0,0,0,0.4)] translate-y-[1px]' 
                        : 'bg-[#333] border-[#444] text-[#ccc] shadow-[0_2px_0_#111] hover:bg-[#444] hover:text-white active:translate-y-[1px] active:shadow-none'}
                `}
            >
                {isPlaying ? 'STOP' : 'PLAY'}
            </button>
            
            <div className="flex gap-1.5">
                <button 
                    onClick={onLoadFile}
                    disabled={isExporting}
                    className="flex-1 h-[26px] bg-[#2a2a2a] text-[#aaa] text-[8px] font-['Michroma'] font-bold rounded-[2px] border border-[#3c3c3c] hover:text-white hover:border-[#555] hover:bg-[#333] shadow-sm whitespace-nowrap transition-colors"
                    title="Load Audio File"
                >
                    LOAD
                </button>
                <button 
                    onClick={openExportDialog}
                    disabled={isExporting || !buffer}
                    className={`flex-1 h-[26px] text-[8px] font-['Michroma'] font-bold rounded-[2px] border shadow-sm whitespace-nowrap transition-all
                        ${isExporting 
                            ? 'bg-red-900/40 text-red-200 border-red-800 animate-pulse' 
                            : 'bg-[#2a2a2a] text-[#aaa] border-[#3c3c3c] hover:text-white hover:border-[#555] hover:bg-[#333] disabled:opacity-50'}
                    `}
                    title="Export Mix"
                >
                    {isExporting ? 'BUSY' : 'WAV'}
                </button>
            </div>
       </div>

       {/* Waveform Area */}
       <div 
         ref={containerRef}
         className="flex-1 bg-[#050505] relative cursor-crosshair group overflow-hidden touch-none"
         onPointerDown={handlePointerDown}
         onDoubleClick={onTogglePlay}
       >
          <canvas ref={canvasRef} className="w-full h-full block opacity-90" />
          
          {/* Grid overlay for 'Science' look */}
          <div className="absolute inset-0 pointer-events-none" 
               style={{ backgroundImage: 'linear-gradient(rgba(255,255,255,0.03) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,0.03) 1px, transparent 1px)', backgroundSize: '20px 20px' }}>
          </div>
          
          {/* Glass Reflection/Vignette */}
          <div className="absolute inset-0 pointer-events-none bg-[radial-gradient(circle_at_center,transparent_50%,rgba(0,0,0,0.6)_100%)]"></div>
          
          {/* Playhead */}
          {buffer && (
              <div 
                className="absolute top-0 bottom-0 w-[1px] bg-[#ff3300] shadow-[0_0_6px_#ff3300] pointer-events-none z-20"
                style={{ 
                    left: 0,
                    transform: `translateX(${buffer ? (currentTime / buffer.duration) * (containerRef.current?.offsetWidth || 0) : 0}px)` 
                }}
              >
                  <div className="absolute top-0 -left-[3px] w-[7px] h-[4px] bg-[#ff3300]"></div>
                  <div className="absolute bottom-0 -left-[3px] w-[7px] h-[4px] bg-[#ff3300]"></div>
              </div>
          )}

          {/* Overlay Text when empty */}
          {!buffer && (
              <div className="absolute inset-0 flex items-center justify-center pointer-events-none z-30">
                  <div className="bg-black/60 px-3 py-1 border border-[#333] rounded backdrop-blur-sm">
                    <span className="text-[#666] font-['Michroma'] text-[10px] tracking-[0.2em] animate-pulse">NO TAPE MOUNTED</span>
                  </div>
              </div>
          )}
          
          {/* Overlay when Exporting */}
          {isExporting && (
              <div className="absolute inset-0 z-50 flex items-center justify-center bg-black/80 backdrop-blur-sm">
                  <span className="text-red-500 font-['Michroma'] text-xs tracking-[0.2em] animate-pulse drop-shadow-[0_0_5px_red]">RENDERING MIXDOWN...</span>
              </div>
          )}
       </div>

       {/* Rack Right Ear */}
       <div className="w-[12px] h-full bg-[#111] border-l border-[#444] flex flex-col items-center justify-between py-2">
           <div className="w-1.5 h-1.5 rounded-full bg-[#000] border border-[#555] shadow-sm"></div>
           <div className="w-1.5 h-1.5 rounded-full bg-[#000] border border-[#555] shadow-sm"></div>
       </div>
    </div>

    {/* EXPORT MODAL PORTAL (Fixed overlay) */}
    {showExportModal && (
        <div 
            className="fixed inset-0 z-[9999] flex items-center justify-center bg-black/80 backdrop-blur-sm"
            onClick={() => setShowExportModal(false)}
        >
             <div 
                className="w-[300px] bg-[#151515] border-[2px] border-[#333] shadow-[0_0_40px_rgba(0,0,0,0.9)] rounded overflow-hidden font-['Michroma'] transform scale-110"
                onClick={e => e.stopPropagation()}
             >
                 <div className="bg-[#222] px-3 py-2 border-b border-[#333] flex justify-between items-center">
                     <span className="text-white text-[9px] tracking-widest font-bold">EXPORT MASTER</span>
                     <div className="w-2 h-2 rounded-full bg-red-900 shadow-[0_0_4px_red] animate-pulse"></div>
                 </div>
                 
                 <div className="p-5 flex flex-col gap-4">
                     <div className="flex flex-col gap-1.5">
                         <label className="text-[#666] text-[8px] font-bold tracking-wider">FILENAME</label>
                         <input 
                            type="text" 
                            value={exportFilename} 
                            onChange={e => setExportFilename(e.target.value)}
                            className="bg-[#080808] border border-[#333] text-[#4ade80] text-[11px] p-2 rounded-sm outline-none focus:border-[#4ade80] focus:shadow-[0_0_5px_rgba(74,222,128,0.2)] transition-all font-mono"
                            autoFocus
                         />
                     </div>
                     
                     <div className="flex flex-col gap-1.5">
                         <label className="text-[#666] text-[8px] font-bold tracking-wider">FORMAT</label>
                         <div className="bg-[#080808] border border-[#333] p-2 rounded-sm flex justify-between items-center opacity-80">
                             <span className="text-[#ccc] text-[9px]">WAV (PCM)</span>
                             <span className="text-[#444] text-[8px]">16-BIT 44.1kHz</span>
                         </div>
                     </div>
                 </div>
                 
                 <div className="flex border-t border-[#333]">
                     <button 
                        onClick={() => setShowExportModal(false)}
                        className="flex-1 py-3 bg-[#1a1a1a] text-[#888] text-[9px] font-bold hover:bg-[#252525] hover:text-[#ccc] transition-colors border-r border-[#333]"
                     >
                         CANCEL
                     </button>
                     <button 
                        onClick={handleExportConfirm}
                        className="flex-1 py-3 bg-[#1a1a1a] text-[#4ade80] text-[9px] font-bold hover:bg-[#4ade80] hover:text-black transition-all shadow-[inset_0_0_10px_rgba(0,0,0,0.5)] hover:shadow-none"
                     >
                         EXPORT
                     </button>
                 </div>
             </div>
        </div>
    )}
    </>
  );
};

export default WaveformPlayer;
