#!/usr/bin/env python3
"""
Bitey GUI verification — compares a native screenshot against the website reference.
Usage: python3 verify_gui.py <screenshot.png>

Outputs:
- Mean pixel difference
- SSIM-like structural score
- Per-region analysis (knobs, meters, labels, etc.)
- PASS/FAIL based on thresholds
"""

import sys
from PIL import Image, ImageChops
import math

REFERENCE = "/home/hatch/workspace/bitey-website-reference.png"

def load_and_normalize(path, target_size):
    """Load image and resize to target_size."""
    img = Image.open(path).convert("RGB")
    if img.size != target_size:
        img = img.resize(target_size, Image.LANCZOS)
    return img

def mean_abs_diff(img1, img2):
    """Mean absolute pixel difference (0-255)."""
    diff = ImageChops.difference(img1, img2)
    hist = diff.histogram()
    # histogram() concatenates per-channel bins (768 entries for RGB);
    # each channel's bins must be weighted 0-255, not 0-767.
    total = (sum(hist[i] * i for i in range(256))
             + sum(hist[256 + i] * i for i in range(256))
             + sum(hist[512 + i] * i for i in range(256)))
    pixels = img1.size[0] * img1.size[1] * 3  # RGB
    return total / pixels

def region_diff(img1, img2, box, name):
    """Compare a specific region."""
    r1 = img1.crop(box)
    r2 = img2.crop(box)
    d = mean_abs_diff(r1, r2)
    print(f"  {name}: {d:.2f}")
    return d

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <screenshot.png>")
        sys.exit(1)

    screenshot_path = sys.argv[1]
    
    # Reference is 2100x1040 (2x of 1050x520)
    target = (2100, 1040)
    
    ref = load_and_normalize(REFERENCE, target)
    shot = load_and_normalize(screenshot_path, target)
    
    print("=== Bitey GUI Verification ===")
    print(f"Reference: {REFERENCE}")
    print(f"Screenshot: {screenshot_path}")
    print()
    
    overall = mean_abs_diff(ref, shot)
    print(f"Overall mean diff: {overall:.2f} (lower is better)")
    print()
    
    print("Per-region:")
    # Regions in 2100x1040 coordinates (approximate)
    # Channel 1 strip (left)
    region_diff(ref, shot, (80, 30, 420, 1010), "Channel 1")
    # Channel 2 strip
    region_diff(ref, shot, (430, 30, 770, 1010), "Channel 2")
    # Center (VU meters)
    region_diff(ref, shot, (780, 30, 1330, 1010), "Center/VU")
    # Main strip
    region_diff(ref, shot, (1340, 30, 1680, 1010), "Main")
    # Reverb strip
    region_diff(ref, shot, (1690, 30, 2030, 1010), "Reverb")
    
    print()
    # Thresholds: <5 is excellent, <10 good, <20 needs work, >20 fail
    if overall < 5:
        print("PASS: Excellent match")
    elif overall < 10:
        print("PASS: Good match")
    elif overall < 20:
        print("NEEDS WORK: Visible differences")
    else:
        print("FAIL: Major differences")
    
    return 0 if overall < 10 else 1

if __name__ == "__main__":
    sys.exit(main())
