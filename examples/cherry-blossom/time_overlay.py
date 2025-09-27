#!/usr/bin/env python3

import os
import sys
import subprocess
import time
import tempfile
from PIL import Image, ImageDraw, ImageFont
from datetime import datetime
import argparse

# Configuration
FONT_SIZE = 120  # Base font size (pre-scale)
FONT_SCALE = 11.0  # Default scale multiplier (~5x larger)
FONT_COLOR = (255, 255, 255, 255)  # Pure white
SHADOW_COLOR = (0, 0, 0, 200)  # Darker shadow for readability
SHADOW_OFFSET = 4
UPDATE_INTERVAL = 30  # Update every 30 seconds

# Layer configuration
LAYER_Z = 5  # Position between bg (default 0) and fg (higher z)
LAYER_OPACITY = 1.0
LAYER_SHIFT_MULTIPLIER = 0.0  # Parallax shift multiplier (0.1=minimal, 1=normal)

def create_time_image(font_size=FONT_SIZE, position='center', scale=FONT_SCALE):
    """Generate transparent PNG with current time"""
    current_time = datetime.now().strftime("%H:%M")
    
    # Create transparent image matching screen resolution
    width, height = 3840, 2160
    img = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    # Apply scale to requested font size
    try:
        effective_font_size = max(1, int(font_size * float(scale)))
    except Exception:
        effective_font_size = max(1, int(font_size))

    # Try to use a nice font, fallback to default if not available
    font = None
    try:
        # Try system fonts (common paths on different distributions)
        font_paths = [
            "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
            "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
            "/usr/share/fonts/noto/NotoSans-Bold.ttf",
            "/usr/share/fonts/gsfonts/NimbusSans-Bold.otf",
        ]
        for path in font_paths:
            if os.path.exists(path):
                font = ImageFont.truetype(path, effective_font_size)
                print(f"Using font: {path}")
                break
        if not font:
            # Try to use Pillow's default but with size
            # Note: load_default ignores size on some Pillow versions
            font = ImageFont.load_default()
            print(
                f"Warning: Using default font (requested size {effective_font_size})"
            )
    except Exception as e:
        print(f"Font error: {e}")
        font = ImageFont.load_default()
    
    # Calculate text dimensions using textbbox
    bbox = draw.textbbox((0, 0), current_time, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    
    # Account for the text baseline offset
    # textbbox returns the actual bounding box, including ascent/descent
    # We need to adjust for the baseline position
    text_offset_x = -bbox[0]  # Left bearing compensation
    text_offset_y = -bbox[1]  # Top bearing compensation
    
    # Position the text
    margin = 150  # Increased margin (unused for center)
    
    if position == 'bottom-right':
        x = width - text_width - margin
        y = height - text_height - margin
    elif position == 'top-right':
        x = width - text_width - margin
        y = margin
    elif position == 'center':
        # True center accounting for text metrics
        x = (width - text_width) // 2
        y = (height - text_height) // 2
    else:  # bottom-left
        x = margin
        y = height - text_height - margin
    
    # Apply the offset corrections
    x += text_offset_x
    y += text_offset_y
    
    # Draw shadow for better visibility
    draw.text((x + SHADOW_OFFSET, y + SHADOW_OFFSET), current_time, 
              font=font, fill=SHADOW_COLOR)
    
    # Draw main text
    draw.text((x, y), current_time, font=font, fill=FONT_COLOR)
    
    # Debug: Draw a visible border to confirm image is being rendered
    # draw.rectanglehyprlax([0, 0, width-1, height-1], outline=(255, 0, 0, 100), width=5)
    
    print(f"Time '{current_time}' positioned at ({x}, {y}), size: {text_width}x{text_height}")
    
    return img

def create_layer(image_path):
    """Create a new time layer"""
    cmd = [
        "hyprlax", "ctl", "add", image_path,
        f"z={LAYER_Z}",
        f"opacity={LAYER_OPACITY}",
        f"shift_multiplier={LAYER_SHIFT_MULTIPLIER}",
        f"fit=cover"
    ]
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"Error adding layer: {result.stderr}", file=sys.stderr)
        return None
    
    # Extract layer ID from output
    # Expected format: "Layer added with ID: X"
    if "Layer added with ID:" in result.stdout:
        try:
            new_id = int(result.stdout.split("ID:")[1].strip())
            return new_id
        except:
            print(f"Could not extract layer ID from: {result.stdout}", file=sys.stderr)
    
    return None

def refresh_layer(layer_id, image_path):
    """Refresh layer by updating its path (forces reload)"""
    cmd = ["hyprlax", "ctl", "modify", str(layer_id), "path", image_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"Error refreshing layer: {result.stderr}", file=sys.stderr)
        return False
    
    return True

def get_layer_id():
    """Try to find our time layer ID from the list"""
    import json
    result = subprocess.run(["hyprlax", "ctl", "list", "--json"], 
                           capture_output=True, text=True)
    if result.returncode == 0:
        try:
            layers = json.loads(result.stdout)
            for layer in layers:
                if 'path' in layer and 'time_overlay' in layer['path']:
                    return layer.get('id')
        except json.JSONDecodeError:
            pass
    return None

def main():
    parser = argparse.ArgumentParser(description='Add time overlay to hyprlax')
    parser.add_argument('--font-size', type=int, default=FONT_SIZE,
                       help=f'Font size for the time display (default: {FONT_SIZE})')
    parser.add_argument('--position', type=str, default='center',
                       choices=['bottom-right', 'top-right', 'center', 'bottom-left'],
                       help='Position of the time display (default: center)')
    parser.add_argument('--scale', type=float, default=FONT_SCALE,
                       help='Scale multiplier applied to --font-size '
                            '(default: 5.0)')
    parser.add_argument('--once', action='store_true',
                       help='Run once and exit (for cron)')
    parser.add_argument('--interval', type=int, default=UPDATE_INTERVAL,
                       help=f'Update interval in seconds (default: {UPDATE_INTERVAL})')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Verbose output')
    parser.add_argument('--out-dir', type=str, default=None,
                       help='Directory to write the overlay image. '
                            'If omitted, uses a persistent ./tmp when --once, '
                            'or a temporary directory otherwise.')
    parser.add_argument('--only-image', action='store_true',
                       help='Only generate the image and skip creating/updating the layer via IPC')
    args = parser.parse_args()
    
    # Decide output directory behavior
    # - If --out-dir provided: use it (absolute or relative to this script)
    # - Else if --once: use persistent ./tmp next to this script
    # - Else: use a temporary directory that will be cleaned up
    script_dir = os.path.dirname(os.path.abspath(__file__))
    cleanup = False

    if args.out_dir:
        out_dir = args.out_dir
        if not os.path.isabs(out_dir):
            out_dir = os.path.join(script_dir, out_dir)
        os.makedirs(out_dir, exist_ok=True)
    elif args.once:
        out_dir = os.path.join(script_dir, 'tmp')
        os.makedirs(out_dir, exist_ok=True)
    else:
        out_dir = tempfile.mkdtemp(prefix='hyprlax_time_')
        cleanup = True

    image_path = os.path.join(out_dir, 'time_overlay.png')
    
    if args.verbose:
        print(f"Starting time overlay with font size {args.font_size}")
        print(f"Overlay image path: {image_path}")
    
    try:
        layer_id = None
        iteration = 0
        last_time = None
        
        while True:
            # Get current time
            current_time = datetime.now().strftime("%H:%M")
            
            # Skip update if time hasn't changed (unless first iteration)
            if iteration > 0 and current_time == last_time:
                if args.verbose:
                    print(f"Time unchanged ({current_time}), skipping update")
            else:
                # Time has changed, generate new image
                img = create_time_image(args.font_size, args.position, args.scale)
                img.save(image_path)
                
                if args.verbose or (iteration == 0 and args.once):
                    print(f"Generated time image: {current_time}")
                
                if not args.only_image:
                    # On first iteration, check for existing layer or create new one
                    if iteration == 0:
                        layer_id = get_layer_id()
                        if layer_id:
                            if args.verbose:
                                print(f"Found existing time layer (ID: {layer_id})")
                        else:
                            # Create new layer
                            layer_id = create_layer(image_path)
                            if layer_id:
                                print(f"Created time overlay layer (ID: {layer_id})")
                            else:
                                print("Failed to create time layer", file=sys.stderr)
                                break
                    
                    # Refresh the layer (this forces hyprlax to reload the image)
                    if iteration > 0 and layer_id:
                        if refresh_layer(layer_id, image_path):
                            if args.verbose:
                                print(f"Refreshed time overlay to {current_time}")
                        else:
                            print(f"Failed to refresh layer for {current_time}", file=sys.stderr)
                
                # Update last_time after successful update
                last_time = current_time
            
            if args.once:
                break
            
            iteration += 1
            
            # Wait for next update
            if args.verbose:
                print(f"Waiting {args.interval} seconds...")
            time.sleep(args.interval)
            
    except KeyboardInterrupt:
        print("\nStopping time overlay...")
        if (not args.only_image) and layer_id:
            subprocess.run(["hyprlax", "ctl", "remove", str(layer_id)])
            print(f"Removed time overlay layer (ID: {layer_id})")
    finally:
        # Cleanup only when using a temporary directory (looping mode)
        if cleanup:
            try:
                if os.path.exists(image_path):
                    os.remove(image_path)
            except Exception:
                pass
            try:
                if os.path.exists(out_dir):
                    os.rmdir(out_dir)
            except Exception:
                pass

if __name__ == "__main__":
    main()
