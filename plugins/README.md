# Hyprlax Compositor Plugins

This directory contains optional compositor-specific plugins that provide enhanced integration with certain window managers.

## Purpose

Some compositors have limitations with the layer-shell protocol that prevent hyprlax from functioning correctly. These plugins run inside the compositor and provide native integration to work around these limitations.

## Available Plugins

### Wayfire Plugin (`wayfire/`)
- **Problem**: Wayfire's vswitch plugin includes all layer-shell surfaces in workspace switching animations
- **Solution**: Native Wayfire plugin that creates compositor views directly and handles workspace changes
- **Status**: In development

## Building

Plugins are built separately from the main hyprlax binary:

```bash
cd plugins/wayfire
make
```

## Installation

Plugins should be installed to your compositor's plugin directory. See individual plugin READMEs for details.

## Architecture

When a plugin is available, hyprlax operates in "plugin mode":
1. Hyprlax detects the plugin is loaded in the compositor
2. Renders frames to shared memory instead of creating a layer-shell surface  
3. Plugin reads frames from shared memory and displays them as native compositor views
4. Plugin handles all compositor-specific behaviors (workspace persistence, etc.)

Without the plugin, hyprlax falls back to standard layer-shell protocol (with limitations).