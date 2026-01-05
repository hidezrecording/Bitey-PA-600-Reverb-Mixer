
import React, { useEffect, useRef, memo } from 'react';

interface VUMeterProps {
  label?: string; 
  level?: number; 
  getValue?: () => number; 
  width?: number;
  height?: number;
}

const VUMeter: React.FC<VUMeterProps> = memo(({ level = 0, getValue, width = 300, height = 150 }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const currentLevelRef = useRef(level);
  const smoothedLevelRef = useRef(0);
  const getValueRef = useRef(getValue);
  const noiseCanvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
      getValueRef.current = getValue;
  }, [getValue]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Pre-generate noise texture once
    if (!noiseCanvasRef.current) {
        const nc = document.createElement('canvas');
        nc.width = width;
        nc.height = height;
        const nCtx = nc.getContext('2d');
        if (nCtx) {
            const imageData = nCtx.createImageData(width, height);
            const data = imageData.data;
            for (let i = 0; i < data.length; i += 4) {
                const noise = (Math.random() - 0.5) * 30;
                if (Math.random() > 0.5) {
                    data[i] = noise;
                    data[i+1] = noise;
                    data[i+2] = noise;
                    data[i+3] = 20; // low alpha baked in or handled via globalAlpha
                } else {
                    data[i+3] = 0;
                }
            }
            nCtx.putImageData(imageData, 0, 0);
            noiseCanvasRef.current = nc;
        }
    }

    let animId: number;

    const render = () => {
      if (getValueRef.current) {
        currentLevelRef.current = getValueRef.current();
      } else {
        currentLevelRef.current = level;
      }
      
      const target = currentLevelRef.current;
      const current = smoothedLevelRef.current;
      if (target > current) {
          smoothedLevelRef.current += (target - current) * 0.2;
      } else {
          smoothedLevelRef.current += (target - current) * 0.05;
      }
      
      const displayLevel = smoothedLevelRef.current;
      const w = width;
      const h = height;
      const s = w / 300; 

      ctx.clearRect(0,0,w,h);

      // Background
      const bgGrad = ctx.createLinearGradient(0, 0, 0, h);
      bgGrad.addColorStop(0, '#0088aa'); 
      bgGrad.addColorStop(0.5, '#005577'); 
      bgGrad.addColorStop(1, '#002233'); 
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, w, h);

      // Static Noise
      if (noiseCanvasRef.current) {
        ctx.save();
        ctx.globalCompositeOperation = 'overlay';
        ctx.globalAlpha = 0.5;
        ctx.drawImage(noiseCanvasRef.current, 0, 0);
        ctx.restore();
      }

      // Vignette
      const vignette = ctx.createRadialGradient(w/2, h/1.5, w*0.1, w/2, h/1.5, w*0.95);
      vignette.addColorStop(0, "rgba(255,255,255,0.3)"); 
      vignette.addColorStop(0.3, "rgba(100,220,255,0.1)");
      vignette.addColorStop(1, "rgba(0,0,0,0.5)"); 
      ctx.fillStyle = vignette;
      ctx.fillRect(0, 0, w, h);

      const cx = w / 2;
      const cy = h * 1.8; 
      const r = h * 1.55; 

      const startAngle = -Math.PI * 0.75; 
      const endAngle = -Math.PI * 0.25;
      
      const zeroPos = 0.72; 
      const totalAngle = endAngle - startAngle;
      
      const redStartAngle = startAngle + (zeroPos * totalAngle);
      
      // Arc Red
      ctx.beginPath();
      ctx.arc(cx, cy, r, redStartAngle, endAngle);
      ctx.strokeStyle = 'rgba(220, 50, 50, 0.9)'; 
      ctx.lineWidth = 6 * s;
      ctx.stroke();

      // Arc Black
      ctx.beginPath();
      ctx.arc(cx, cy, r, startAngle, redStartAngle);
      ctx.strokeStyle = 'rgba(20, 20, 20, 0.9)'; 
      ctx.lineWidth = 2 * s;
      ctx.stroke();

      // Ticks
      const scalePoints = [
          { val: -20, label: '20', pos: 0.0, big: true },
          { val: -10, label: '10', pos: 0.30, big: true },
          { val: -7,  label: '',   pos: 0.42, big: false },
          { val: -5,  label: '5',  pos: 0.52, big: true },
          { val: -3,  label: '3',  pos: 0.61, big: true },
          { val: -2,  label: '',   pos: 0.65, big: false },
          { val: -1,  label: '',   pos: 0.685, big: false },
          { val: 0,   label: '0',  pos: 0.72, big: true, isRed: true },
          { val: 1,   label: '',   pos: 0.79, big: false, isRed: true },
          { val: 2,   label: '',   pos: 0.86, big: false, isRed: true },
          { val: 3,   label: '3',  pos: 1.0,  big: true, isRed: true }, 
      ];

      scalePoints.forEach(pt => {
          const angle = startAngle + (pt.pos * totalAngle);
          const tickLen = (pt.big ? 12 : 7) * s;
          const x1 = cx + Math.cos(angle) * r;
          const y1 = cy + Math.sin(angle) * r;
          const x2 = cx + Math.cos(angle) * (r - tickLen);
          const y2 = cy + Math.sin(angle) * (r - tickLen);
          
          ctx.beginPath();
          ctx.moveTo(x1, y1);
          ctx.lineTo(x2, y2);
          ctx.strokeStyle = pt.isRed ? '#cc3333' : '#111111'; 
          ctx.lineWidth = (pt.big ? 2.5 : 1.5) * s;
          ctx.stroke();

          if (pt.label) {
            const textDist = 28 * s; 
            const tx = cx + Math.cos(angle) * (r - textDist);
            const ty = cy + Math.sin(angle) * (r - textDist);
            
            ctx.fillStyle = pt.isRed ? '#cc3333' : '#111111'; 
            ctx.font = `bold ${12 * s}px "Michroma", sans-serif`;
            ctx.textBaseline = 'middle';
            ctx.textAlign = 'center';
            ctx.fillText(pt.label, tx, ty);
          }
      });

      // Labels
      ctx.save();
      ctx.translate(cx, h * 0.65);
      ctx.font = `900 ${22 * s}px "Michroma", sans-serif`;
      ctx.fillStyle = 'rgba(255,255,255,0.95)';
      ctx.textAlign = 'center';
      ctx.fillText("BITEY", 0, 0);
      ctx.font = `500 ${8 * s}px "Michroma", sans-serif`;
      ctx.fillStyle = 'rgba(0,0,0,0.8)'; 
      ctx.fillText("MOD. 600", 0, 15 * s); 
      ctx.restore();

      ctx.font = `bold ${11 * s}px "Michroma", sans-serif`;
      ctx.fillStyle = '#111'; 
      ctx.fillText('VU', w * 0.12, h * 0.25);
      ctx.fillStyle = '#cc3333'; 
      ctx.textAlign = 'right';
      ctx.fillText('dB', w * 0.88, h * 0.25);

      // Needle
      const targetPos = Math.min(Math.max(displayLevel * 0.8, -0.05), 1.05);
      const needleAngle = startAngle + (targetPos * totalAngle);

      const shadowOffset = 4 * s;
      ctx.beginPath();
      ctx.moveTo(cx + shadowOffset, cy + shadowOffset);
      ctx.lineTo(cx + Math.cos(needleAngle)*(r-10) + shadowOffset, cy + Math.sin(needleAngle)*(r-10) + shadowOffset);
      ctx.strokeStyle = 'rgba(0,0,0,0.4)';
      ctx.lineWidth = 2 * s;
      ctx.stroke();

      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(cx + Math.cos(needleAngle)*(r-5), cy + Math.sin(needleAngle)*(r-5));
      ctx.strokeStyle = '#1a1a1a'; 
      ctx.lineWidth = 1.5 * s;
      ctx.stroke();
      
      const tipStartR = r - 25 * s;
      ctx.beginPath();
      ctx.moveTo(cx + Math.cos(needleAngle)*tipStartR, cy + Math.sin(needleAngle)*tipStartR);
      ctx.lineTo(cx + Math.cos(needleAngle)*(r-5), cy + Math.sin(needleAngle)*(r-5));
      ctx.strokeStyle = '#cc3333';
      ctx.lineWidth = 1.5 * s;
      ctx.stroke();

      // Pivot Cover
      ctx.beginPath();
      ctx.arc(cx, h + 10, 40 * s, Math.PI, 0); 
      ctx.fillStyle = '#111'; 
      ctx.fill();

      animId = requestAnimationFrame(render);
    };
    render();

    return () => cancelAnimationFrame(animId);
  }, [width, height]); 

  return (
    <div 
      className="relative rounded-[4px] overflow-hidden bg-[#111]"
      style={{ 
          width, 
          height,
          border: '2px solid #333',
          borderTopColor: '#555',
          borderLeftColor: '#444',
          borderBottomColor: '#222',
          borderRightColor: '#222',
          boxShadow: `0 10px 20px rgba(0,0,0,0.8), inset 0 1px 1px rgba(255,255,255,0.5), inset 0 0 20px rgba(0,0,0,0.9)`
      }}
    >
       <canvas ref={canvasRef} width={width} height={height} className="relative z-10" />
       <div className="absolute inset-0 pointer-events-none z-20 overflow-hidden rounded-[3px]">
           <div className="absolute top-0 left-0 right-0 h-[45%] bg-gradient-to-b from-white/10 to-transparent"></div>
           <div className="absolute inset-0 opacity-20 bg-[url('https://www.transparenttextures.com/patterns/dust.png')] mix-blend-overlay"></div>
           <div className="absolute inset-0 shadow-[inset_0_2px_8px_rgba(0,0,0,0.8)] border border-white/5"></div>
       </div>
    </div>
  );
});

export default VUMeter;
