<script>
  export let mouseX = 0.5;
  export let mouseY = 0.5;
</script>

<!-- Logo/Title -->
<div class="mb-8 text-center">
  <h1 class="text-5xl sm:text-6xl md:text-7xl lg:text-8xl font-mono font-bold mb-4">
    <span class="bg-clip-text text-transparent bg-gradient-to-r from-hypr-blue to-hypr-pink">
      hyprlax
    </span>
  </h1>
  <p class="text-gray-400 text-lg sm:text-xl md:text-2xl font-mono">
    smooth parallax wallpapers for hyprland
  </p>
</div>

<!-- Video/Screencast -->
<div class="w-full max-w-4xl aspect-video mb-12 relative group">
  <div class="absolute inset-0 bg-gradient-to-r from-hypr-blue/20 to-hypr-pink/20 rounded-2xl blur-3xl group-hover:blur-2xl transition-all duration-500"></div>
  <div class="relative bg-hypr-dark/80 backdrop-blur-xl rounded-2xl border border-white/10 overflow-hidden shadow-2xl">
    <img 
      src="/assets/hyprlax-demo.gif" 
      alt="Hyprlax parallax wallpaper animation demo" 
      class="w-full h-full object-cover"
      loading="lazy"
    />
  </div>
</div>