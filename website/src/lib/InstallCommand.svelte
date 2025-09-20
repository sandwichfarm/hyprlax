<script>
  let copied = false;
  
  const copyCommand = () => {
    navigator.clipboard.writeText('curl -sSL https://hyprlax.com/install.sh | bash');
    copied = true;
    setTimeout(() => copied = false, 2000);
  };
</script>

<div class="w-full max-w-3xl mb-8">
  <button 
    on:click={copyCommand}
    class="group w-full bg-hypr-dark/50 backdrop-blur-xl rounded-xl border border-white/10 p-4 sm:p-6 hover:border-hypr-blue/50 transition-all duration-300 relative overflow-hidden"
  >
    <div class="absolute inset-0 bg-gradient-to-r from-hypr-blue/10 to-hypr-pink/10 opacity-0 group-hover:opacity-100 transition-opacity duration-300"></div>
    <div class="relative flex items-center justify-between">
      <code class="text-sm sm:text-base md:text-lg font-mono text-gray-300">
        <span class="text-hypr-blue">$</span> curl -sSL https://hyprlax.com/install.sh <span class="text-gray-500">|</span> bash
      </code>
      <div class="ml-4 flex-shrink-0">
        {#if copied}
          <span class="text-green-400 text-sm font-mono">copied!</span>
        {:else}
          <svg class="w-5 h-5 text-gray-400 group-hover:text-hypr-blue transition-colors" fill="none" stroke="currentColor" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 5H6a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2v-1M8 5a2 2 0 002 2h2a2 2 0 002-2M8 5a2 2 0 012-2h2a2 2 0 012 2m0 0h2a2 2 0 012 2v3m2 4H10m0 0l3-3m-3 3l3 3"></path>
          </svg>
        {/if}
      </div>
    </div>
  </button>
</div>