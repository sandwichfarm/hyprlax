# Hyprlax Examples

This directory contains self-contained example configurations demonstrating various parallax effects with hyprlax.

## Available Examples

Note: Some examples historically used `parallax.conf`. If `hyprlax.toml` is missing, convert once:

```
hyprlax ctl convert-config <example>/parallax.conf <example>/hyprlax.toml --yes
```

### 🏔️ Mountains
A natural landscape with layered mountains, clouds, and trees.
- 6 layers with varying depth
- Uses blur for atmospheric perspective
- Smooth, organic animations

```bash
hyprlax --config examples/mountains/hyprlax.toml
```

### 🌃 City
An urban nightscape with multiple skyline layers.
- Night sky with twinkling stars
- Multiple building layers with lit windows
- Street-level foreground

```bash
hyprlax --config examples/city/hyprlax.toml
```

### 🎨 Abstract
Colorful geometric shapes with dreamy motion.
- Gradient background
- Multiple shape layers
- Heavy blur for depth effect

```bash
hyprlax --config examples/abstract/hyprlax.toml
```

## Structure

Each example directory contains:
- **Layer images** (PNG with transparency)
- **hyprlax.toml** - Hyprlax TOML configuration file (convert from legacy if needed)
- **README.md** - Example-specific documentation

## Generating New Examples

Use the included Python script to regenerate or create new examples:

```bash
# Requires Python 3 and Pillow
pip install Pillow

# Generate examples
python3 generate-images.py
```

The script creates procedurally-generated images perfect for testing parallax effects.

## Using Examples

1. **Test an example (TOML):**
   ```bash
   cd /path/to/hyprlax
   ./hyprlax --config examples/mountains/hyprlax.toml
   ```

2. **Customize settings:**
   Edit the `hyprlax.toml` file in any example directory to adjust:
   - Shift multipliers (parallax intensity)
   - Opacity values (layer transparency)  
   - Blur amounts (depth perception)
   - Animation settings (duration, easing)

3. **Use as templates:**
   Copy an example directory and replace the images with your own artwork.

## Tips

- **Performance**: The mountains example is lightest, abstract is heaviest due to blur
- **Customization**: Adjust blur and opacity for different depth effects
- **Testing**: Switch between workspaces to see the parallax effect
- **Debugging**: Add `--debug` flag to see layer loading information

## Creating Your Own

To create a custom parallax wallpaper:

1. Prepare layered images (PNG with transparency)
2. Copy an example directory as a template
3. Replace the layer images
4. Update paths in `hyprlax.toml`
5. Adjust parameters to taste

## Layer Guidelines

- **Background layers**: Use shift 0.0-0.3, blur 3.0-5.0
- **Midground layers**: Use shift 0.4-0.7, blur 1.0-3.0
- **Foreground layers**: Use shift 0.8-1.2, blur 0.0-1.0

Higher shift values = faster movement
Higher blur values = more atmospheric depth
