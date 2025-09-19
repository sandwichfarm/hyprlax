<script>
  const features = [
    {
      title: 'Smooth Animation',
      description: 'Buttery smooth 60+ FPS parallax with customizable easing functions',
      color: 'text-hypr-blue',
      icon: `<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z"></path>`
    },
    {
      title: 'Runtime Control',
      description: 'Live adjustment of layers, blur, and effects via IPC commands',
      color: 'text-hypr-pink',
      icon: `<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 6V4m0 2a2 2 0 100 4m0-4a2 2 0 110 4m-6 8a2 2 0 100-4m0 4a2 2 0 100 4m0-4v2m0-6V4m6 6v10m6-2a2 2 0 100-4m0 4a2 2 0 100 4m0-4v2m0-6V4"></path>`
    },
    {
      title: 'Multi-layer',
      description: 'Support for multiple wallpaper layers with independent parallax',
      color: 'text-green-400',
      icon: `<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 5a1 1 0 011-1h14a1 1 0 011 1v2a1 1 0 01-1 1H5a1 1 0 01-1-1V5zM4 13a1 1 0 011-1h6a1 1 0 011 1v6a1 1 0 01-1 1H5a1 1 0 01-1-1v-6zM16 13a1 1 0 011-1h2a1 1 0 011 1v6a1 1 0 01-1 1h-2a1 1 0 01-1-1v-6z"></path>`
    },
    {
      title: 'Native Wayland',
      description: 'Built for Wayland with layer-shell protocol support',
      color: 'text-blue-400',
      icon: `<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m18-6h-2m2 6h-2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z"></path>`
    },
    {
      title: 'GPU Accelerated',
      description: 'OpenGL ES 2.0 rendering with optimized blur shaders',
      color: 'text-purple-400',
      icon: `<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19.428 15.428a2 2 0 00-1.022-.547l-2.387-.477a6 6 0 00-3.86.517l-.318.158a6 6 0 01-3.86.517L6.05 15.21a2 2 0 00-1.806.547M8 4h8l-1 1v5.172a2 2 0 00.586 1.414l5 5c1.26 1.26.367 3.414-1.415 3.414H4.828c-1.782 0-2.674-2.154-1.414-3.414l5-5A2 2 0 009 10.172V5L8 4z"></path>`
    },
    {
      title: 'Lightweight',
      description: 'Minimal resource usage with efficient C implementation',
      color: 'text-yellow-400',
      icon: `<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z"></path>`
    }
  ];
</script>

<div class="w-full max-w-4xl mb-12">
  <h2 class="text-xl sm:text-2xl font-mono font-bold text-center mb-8 bg-clip-text text-transparent bg-gradient-to-r from-hypr-blue to-hypr-pink">
    Features
  </h2>
  <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
    {#each features as feature}
      <div class="bg-hypr-dark/30 backdrop-blur-xl rounded-xl border border-white/10 p-6">
        <div class="{feature.color} mb-3">
          <svg class="w-8 h-8" fill="none" stroke="currentColor" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            {@html feature.icon}
          </svg>
        </div>
        <h3 class="font-mono font-bold text-white mb-2">{feature.title}</h3>
        <p class="text-gray-400 text-sm font-mono">{feature.description}</p>
      </div>
    {/each}
  </div>
</div>