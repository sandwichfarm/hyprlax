<script>
  import { onMount } from 'svelte';
  import Hero from './lib/Hero.svelte';
  import InstallCommand from './lib/InstallCommand.svelte';
  import SupportedCompositors from './lib/SupportedCompositors.svelte';
  import Features from './lib/Features.svelte';
  import SocialLinks from './lib/SocialLinks.svelte';
  
  let mouseX = 0.5;
  let mouseY = 0.5;
  let targetX = 0.5;
  let targetY = 0.5;
  let animationFrame;
  
  const handleMouseMove = (e) => {
    targetX = e.clientX / window.innerWidth;
    targetY = e.clientY / window.innerHeight;
  };
  
  const animate = () => {
    // Smooth easing with momentum - the gradient "follows" the mouse with delay
    const easing = 0.08; // Lower = more delay/smoother
    
    // Add subtle idle animation
    const time = Date.now() * 0.0001; // Very slow time factor
    const idleX = Math.sin(time) * 0.02; // Subtle horizontal drift
    const idleY = Math.cos(time * 0.7) * 0.02; // Subtle vertical drift
    
    // Combine mouse influence with idle animation
    mouseX += (targetX + idleX - mouseX) * easing;
    mouseY += (targetY + idleY - mouseY) * easing;
    
    animationFrame = requestAnimationFrame(animate);
  };
  
  onMount(() => {
    window.addEventListener('mousemove', handleMouseMove);
    animate();
    
    return () => {
      window.removeEventListener('mousemove', handleMouseMove);
      if (animationFrame) cancelAnimationFrame(animationFrame);
    };
  });
</script>

<main class="h-screen w-screen flex flex-col items-center justify-center relative overflow-hidden">
  <!-- Interactive gradient background - subtle dark gradient -->
  <div 
    class="absolute inset-0"
    style="background: linear-gradient(135deg, 
           #0A0E1B, 
           #050810 {45 + (mouseX - 0.5) * 10}%, 
           #0D1220 {55 + (mouseY - 0.5) * 10}%, 
           #070B15);
           background-size: 130% 130%;
           background-position: {50 - (mouseX - 0.5) * 15}% {50 - (mouseY - 0.5) * 15}%"
  ></div>
  
  <!-- Noise texture overlay -->
  <div class="absolute inset-0 opacity-10" style="background-image: url('data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzMDAiIGhlaWdodD0iMzAwIj48ZmlsdGVyIGlkPSJhIj48ZmVUdXJidWxlbmNlIHR5cGU9ImZyYWN0YWxOb2lzZSIgYmFzZUZyZXF1ZW5jeT0iLjc1IiBudW1PY3RhdmVzPSIxMCIvPjwvZmlsdGVyPjxyZWN0IHdpZHRoPSIxMDAlIiBoZWlnaHQ9IjEwMCUiIGZpbHRlcj0idXJsKCNhKSIgb3BhY2l0eT0iMC4wNSIvPjwvc3ZnPg==');"></div>
  
  <!-- Content container -->
  <div class="relative z-10 w-full max-w-6xl mx-auto px-4 sm:px-6 lg:px-8 flex flex-col items-center">
    <Hero {mouseX} {mouseY} />
    <InstallCommand />
    <SupportedCompositors />
    <Features />
    <SocialLinks />
  </div>
</main>

<style>
  :global(html) {
    scroll-behavior: smooth;
  }
</style>