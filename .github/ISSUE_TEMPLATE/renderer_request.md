---
name: Renderer Backend Request
about: Request support for a new rendering backend or GPU API
title: '[RENDERER] Add support for '
labels: renderer, enhancement
assignees: ''
---

## Renderer Information
- **Renderer Type**: <!-- e.g., Vulkan, OpenGL 4.6, DirectX (via translation layer) -->
- **API Version**: <!-- Specific version required -->
- **Purpose**: <!-- Why is this renderer needed? -->

## Current Limitations
<!-- What problems does the current OpenGL ES renderer have for your use case? -->
- Performance issues:
- Feature limitations:
- Hardware compatibility:

## Hardware Support
- **GPU Vendor**: <!-- AMD, NVIDIA, Intel, ARM Mali, etc. -->
- **GPU Model**: 
- **Driver Version**:
- **Platform**: <!-- Linux, BSD, etc. -->

## Technical Requirements
<!-- Technical details about the renderer -->
- Shader language: <!-- e.g., SPIR-V, GLSL, HLSL -->
- Required extensions:
- Minimum feature set:
- Memory management requirements:

## Performance Benefits
<!-- Expected improvements with this renderer -->
- [ ] Better multi-GPU support
- [ ] Lower CPU overhead
- [ ] Better memory management
- [ ] Hardware-specific optimizations
- [ ] Power efficiency improvements
- Other: <!-- Specify -->

## Implementation Complexity
<!-- Help us understand the scope -->
- Estimated lines of code to change:
- Breaking changes required: Yes/No
- Can coexist with current renderer: Yes/No
- Shader rewrite required: Yes/No

## Testing
- [ ] I have hardware that requires this renderer
- [ ] I can help test the implementation
- [ ] I can contribute to the implementation
- [ ] I have experience with this rendering API

## Use Cases
<!-- Specific use cases that would benefit -->
1. 
2. 
3. 

## Alternative Solutions
<!-- Have you tried any workarounds? -->
- Current workaround (if any):
- Why workaround is insufficient:

## Additional Context
<!-- Any other information about the renderer request -->

## Checklist
- [ ] I have verified this renderer isn't already supported
- [ ] I have explained why the current OpenGL ES renderer is insufficient
- [ ] I have provided hardware/driver information
- [ ] I have searched for existing renderer requests