
import React, { useRef, useEffect } from 'react';

interface KnobProps {
  value: number;
  min: number;
  max: number;
  onChange: (val: number) => void;
  size?: number;
  variant?: 'standard' | 'skirted';
  scaleType?: 'none' | '0-10' | 'eq' | 'level' | 'micro';
}

const Knob: React.FC<KnobProps> = ({ value, min, max, onChange, size = 60, variant = 'standard', scaleType = 'none' }) => {
  const startY = useRef(0);
  const startVal = useRef(0);
  const onChangeRef = useRef(onChange);

  useEffect(() => { onChangeRef.current = onChange; }, [onChange]);

  const handlePointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    e.preventDefault();
    e.stopPropagation(); 
    
    startY.current = e.clientY;
    startVal.current = value;
    document.body.style.cursor = 'ns-resize';

    const handleMove = (ev: PointerEvent) => {
      ev.preventDefault();
      const deltaY = startY.current - ev.clientY;
      const range = max - min;
      // Increased sensitivity: 100px for full range instead of 200px
      const sensitivity = 1 / 100; 
      const change = deltaY * range * sensitivity;
      const newVal = Math.min(max, Math.max(min, startVal.current + change));
      
      onChangeRef.current(newVal);
    };

    const handleUp = () => {
      document.body.style.cursor = '';
      window.removeEventListener('pointermove', handleMove);
      window.removeEventListener('pointerup', handleUp);
      window.removeEventListener('pointercancel', handleUp);
      document.removeEventListener('mouseleave', handleUp);
    };

    // Attach robust listeners
    window.addEventListener('pointermove', handleMove, { passive: false });
    window.addEventListener('pointerup', handleUp);
    // Safety valves for lost capture or iframe exits
    window.addEventListener('pointercancel', handleUp);
    document.addEventListener('mouseleave', handleUp);
  };

  const pct = (value - min) / (max - min);
  const rotation = -135 + pct * 270;

  const renderPanelScale = () => {
    if (scaleType === 'none') return null;
    
    const ticks = [];
    const count = 10;
    const radius = size / 2;
    const tickDistance = variant === 'skirted' ? radius * 1.15 : radius * 1.25;
    const textDistance = variant === 'skirted' ? radius * 1.30 : radius * 1.45;

    for (let i = 0; i <= count; i++) {
      const angleDeg = -135 + (i / count) * 270;
      const angleRad = (angleDeg - 90) * (Math.PI / 180);
      
      let tx = Math.cos(angleRad) * tickDistance;
      let ty = Math.sin(angleRad) * tickDistance;
      let tickH = (scaleType === 'eq' && i === 5) ? 5 : 3;
      
      // Micro scale adjustments
      if (scaleType === 'micro') {
          tx = Math.cos(angleRad) * (radius * 1.3);
          ty = Math.sin(angleRad) * (radius * 1.3);
          tickH = 2;
      }
      
      ticks.push(
        <div 
          key={`tick-${i}`} 
          className="absolute bg-white shadow-[0_0_2px_white]"
          style={{ 
            left: '50%', top: '50%', width: 1.5, height: tickH,
            transform: `translate(${tx}px, ${ty}px) rotate(${angleDeg + 180}deg) translate(-50%, -50%)`,
            transformOrigin: 'top left', opacity: scaleType === 'micro' ? 0.7 : 1.0, pointerEvents: 'none'
          }}
        />
      );

      let showNum = false;
      let label = i.toString();
      if (scaleType === '0-10' || scaleType === 'level') showNum = (i % 2 === 0);
      else if (scaleType === 'eq') {
          if (i === 0) { label = '-15'; showNum = true; }
          if (i === 5) { label = '0'; showNum = true; }
          if (i === 10) { label = '+15'; showNum = true; }
      }

      if (showNum && scaleType !== 'micro') {
        const lx = Math.cos(angleRad) * textDistance;
        const ly = Math.sin(angleRad) * textDistance;
        ticks.push(
          <div key={`lbl-${i}`}
            className="absolute text-[7px] text-white font-bold flex items-center justify-center leading-none drop-shadow-[0_0_2px_rgba(255,255,255,0.8)] font-['Michroma'] pointer-events-none"
            style={{ left: `calc(50% + ${lx}px)`, top: `calc(50% + ${ly}px)`, transform: 'translate(-50%, -50%)' }}
          >
            {label}
          </div>
        );
      }
    }
    return <div className="absolute inset-0 pointer-events-none">{ticks}</div>;
  };

  return (
    <div className="flex flex-col items-center justify-center relative select-none z-30">
      <div className="relative flex items-center justify-center" style={{ width: size, height: size }}>
        {renderPanelScale()}
        
        {/* Shadow */}
        <div className="absolute rounded-full bg-black opacity-60 blur-[4px] pointer-events-none"
             style={{ width: size * 0.85, height: size * 0.85, top: size * 0.1, left: size * 0.05 }}></div>

        {/* Skirt */}
        {variant === 'skirted' && (
             <div className="absolute rounded-full pointer-events-none"
                style={{ width: size, height: size, zIndex: 5,
                    background: 'conic-gradient(from 180deg, #181818 0%, #3a3a3a 45%, #000 50%, #3a3a3a 55%, #181818 100%)',
                    boxShadow: '0 4px 6px rgba(0,0,0,0.9), inset 0 1px 1px rgba(255,255,255,0.2)'
                }}>
                 <div className="absolute inset-[5%] rounded-full bg-[#111]"></div>
             </div>
        )}

        {/* Visual Rotating Body */}
        <div 
          className="absolute rounded-full z-10 pointer-events-none"
          style={{ 
            width: variant === 'skirted' ? size * 0.7 : size * 0.85, 
            height: variant === 'skirted' ? size * 0.7 : size * 0.85,
            transform: `rotate(${rotation}deg)`,
            background: 'radial-gradient(circle at 35% 25%, #777 0%, #222 35%, #050505 85%, #000 100%)', 
            boxShadow: '0 5px 15px rgba(0,0,0,0.8), inset 0 2px 4px rgba(255,255,255,0.2)',
            transition: 'transform 0.05s linear'
          }}
        >
          <div className="absolute inset-0 rounded-full opacity-10 mix-blend-overlay"
            style={{ backgroundImage: 'repeating-conic-gradient(#000 0deg, #555 15deg, #000 30deg, #555 45deg, #000 60deg)' }}></div>
          <div className="absolute inset-[15%] rounded-full bg-gradient-to-br from-[#333] to-[#000] shadow-[inset_0_1px_2px_rgba(255,255,255,0.1),0_1px_2px_rgba(0,0,0,0.8)]"></div>
          <div className="absolute top-0 left-1/2 -translate-x-1/2 w-[3px] h-full">
               <div className="w-full h-[45%] bg-[#ddd] shadow-[inset_0_0_1px_rgba(0,0,0,0.5),0_0_2px_rgba(0,0,0,0.5)] rounded-[1px] opacity-90"></div>
          </div>
          <div className="absolute top-[8%] left-[22%] w-[40%] h-[25%] bg-white opacity-20 rounded-full blur-[3px] transform -rotate-12"></div>
        </div>

        {/* --- INTERACTION SENSOR LAYER --- */}
        <div 
            className="absolute z-50 rounded-full"
            onPointerDown={handlePointerDown}
            style={{
                width: size,
                height: size,
                top: 0,
                left: 0,
                cursor: 'ns-resize',
                touchAction: 'none',
                opacity: 0
            }}
            title={`Value: ${value.toFixed(1)}`}
        />

      </div>
    </div>
  );
};

export default Knob;
