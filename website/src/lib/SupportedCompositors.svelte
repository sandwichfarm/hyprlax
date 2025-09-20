<script>
  const compositors = [
    { name: 'Hyprland', status: 'tested', statusText: '✓ Tested', statusColor: 'text-green-400' },
    { name: 'Sway', status: 'experimental', statusText: '◐ Experimental', statusColor: 'text-yellow-400' },
    { name: 'River', status: 'experimental', statusText: '◐ Experimental', statusColor: 'text-yellow-400' },
    { name: 'Wayfire', status: 'experimental', statusText: '◐ Experimental', statusColor: 'text-yellow-400' },
    { name: 'X11/EWMH', status: 'beta', statusText: '⚡ Beta', statusColor: 'text-blue-400' }
  ];
</script>

<div class="w-full max-w-3xl mb-8">
  <h2 class="text-xl sm:text-2xl font-mono font-bold text-center mb-6 bg-clip-text text-transparent bg-gradient-to-r from-hypr-blue to-hypr-pink">
    Supported Compositors
  </h2>
  <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-5 gap-4">
    {#each compositors as compositor}
      <div class="bg-hypr-dark/30 backdrop-blur-xl rounded-xl border border-white/10 p-4 text-center">
        <div class="{compositor.statusColor} font-mono text-sm mb-2">{compositor.statusText}</div>
        <div class="font-mono text-gray-300">{compositor.name}</div>
      </div>
    {/each}
  </div>
  <p class="text-gray-500 text-sm text-center mt-4 font-mono">
    More compositors coming soon • X11 window managers supported
  </p>
</div>