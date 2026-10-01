
import React from 'react';

interface BiteyLogoProps {
  className?: string;
  hideBg?: boolean;
}

const BiteyLogo: React.FC<BiteyLogoProps> = ({ className, hideBg = false }) => {
  return (
    <div className={`relative flex items-center justify-center select-none overflow-hidden ${!hideBg ? 'bg-gradient-to-b from-[#0044cc] to-[#002288] shadow-[0_5px_15px_rgba(0,0,0,0.7),inset_0_1px_5px_rgba(255,255,255,0.3)] border-[3px] border-[#a0a0a0] rounded-sm py-2 px-8' : ''} ${className}`}>
      {!hideBg && (
        <>
          {/* Subtle noise texture instead of heavy brushed alum to reduce jumble */}
          <div className="absolute inset-0 opacity-10 bg-black" style={{ backgroundImage: 'url("data:image/svg+xml,%3Csvg width=\'100\' height=\'100\' viewBox=\'0 0 100 100\' xmlns=\'http://www.w3.org/2000/svg\'%3E%3Cfilter id=\'noise\'%3E%3CfeTurbulence type=\'fractalNoise\' baseFrequency=\'0.8\' numOctaves=\'4\' stitchTiles=\'stitch\'/%3E%3C/filter%3E%3Crect width=\'100\' height=\'100\' filter=\'url(%23noise)\' opacity=\'0.5\'/%3E%3C/svg%3E")' }}></div>
          
          {/* Hardware Fasteners (Simpler) */}
          <div className="absolute top-1.5 left-1.5 w-2 h-2 rounded-full bg-[#888] shadow-[1px_1px_2px_black] border border-black/30"></div>
          <div className="absolute top-1.5 right-1.5 w-2 h-2 rounded-full bg-[#888] shadow-[1px_1px_2px_black] border border-black/30"></div>
          <div className="absolute bottom-1.5 left-1.5 w-2 h-2 rounded-full bg-[#888] shadow-[1px_1px_2px_black] border border-black/30"></div>
          <div className="absolute bottom-1.5 right-1.5 w-2 h-2 rounded-full bg-[#888] shadow-[1px_1px_2px_black] border border-black/30"></div>
        </>
      )}
      
      <div className="relative z-10 flex flex-col items-center justify-center transform translate-y-[-2px]">
        <h1 
          className="text-[#f0f0f0] leading-none text-center"
          style={{ 
            fontFamily: '"Metal Mania", cursive', 
            fontSize: '5rem', 
            textShadow: '0 3px 0 #000, 2px 2px 5px rgba(0,0,0,0.8)',
            letterSpacing: '0.02em',
            transform: 'scaleY(0.9)' // Slightly squat look typical of vintage logos
          }}
        >
          BITEY
        </h1>
        <div className="border-t border-white/30 w-full mt-1 mb-1"></div>
        <div className="text-[#ccc] text-[8px] font-bold tracking-[0.4em] font-sans">
            SERIES 600
        </div>
      </div>
    </div>
  );
};

export default BiteyLogo;
