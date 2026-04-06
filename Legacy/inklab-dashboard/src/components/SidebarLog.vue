<script setup>
import { ref, watch, nextTick, computed, onMounted } from 'vue'
import { useSystemStore } from '../stores/systemStore'

const store = useSystemStore()
const logContainer = ref(null)

const toggleAutoscroll = () => {
  store.sidebarAutoScroll = !store.sidebarAutoScroll
  if (store.sidebarAutoScroll && logContainer.value) {
    logContainer.value.scrollTop = logContainer.value.scrollHeight
  }
}

// Track user scrolling. If they scroll up, disable autoscroll and save location.
const onLogScroll = (e) => {
  const { scrollTop, scrollHeight, clientHeight } = e.target
  store.sidebarAutoScroll = scrollHeight - scrollTop <= clientHeight + 10
  store.sidebarScrollState = scrollTop
}

onMounted(async () => {
  await nextTick() // Give the DOM a frame to populate the log array
  if (logContainer.value) {
    if (store.sidebarAutoScroll) {
      logContainer.value.scrollTop = logContainer.value.scrollHeight
    } else {
      logContainer.value.scrollTop = store.sidebarScrollState
    }
  }
})

// Determine FPGA Status based on the clock and active slot
const fpgaState = computed(() => {
  const clk = store.clockStatus.toUpperCase()
  const slot = store.activeSlot

  if (clk.includes('FAILED')) return { label: 'ERROR', color: 'var(--error)' }
  if (slot === '--' || slot.includes('(00)')) return { label: 'ZERO BITSTREAM', color: 'var(--warning)' }
  if (clk.includes('PULSING')) return { label: 'PROGRAMMED', color: 'var(--success)' }
  
  return { label: clk === 'IDLE' ? 'STANDBY' : clk, color: '#94a3b8' }
})

watch(() => store.logs.length, async () => {
  if (store.sidebarAutoScroll && logContainer.value) {
    await nextTick()
    logContainer.value.scrollTop = logContainer.value.scrollHeight
  }
})
</script>

<template>
  <div class="content-right">
    
    <!-- Always-Visible System HUD (4 Columns) -->
    <div class="quick-hud">
      <div class="hud-item" title="SD Card Detection">
        <span>STORAGE</span>
        <strong :style="{ color: store.isSdInserted ? 'var(--success)' : 'var(--error)' }">
          {{ store.isSdInserted ? 'MEDIA OK' : 'NO MEDIA' }}
        </strong>
      </div>

    <div class="hud-item" title="Main Input / PMID Output Power">
        <span>{{ store.bqData.mode === 'OTG' ? 'PMID (BAT)' : 'PMID (USB)' }}</span>
        <strong>
          {{ store.bqData.vbusVolt.toFixed(1) }}V / 
          {{ Math.abs(store.bqData.mode === 'OTG' ? store.bqData.batCurr : store.bqData.vbusCurr).toFixed(0) }}mA
        </strong>
    </div>
      
      <div class="hud-item" title="Battery Pack Status">
        <span>BATTERY</span>
        <strong :style="{ color: store.bqData.soc < 20 ? 'var(--error)' : 'var(--success)' }">
          {{ store.bqData.soc.toFixed(0) }}%
        </strong>
      </div>

      <div class="hud-item" title="FPGA Configuration Status">
        <span>FPGA STAT</span>
        <strong :style="{ color: fpgaState.color }">{{ fpgaState.label }}</strong>
      </div>
    </div>

    <!-- Terminal Header -->
    <div id="log-header">
      <span>System Log</span>
      <div class="log-actions">
        <span class="autoscroll-toggle" :class="{active: store.sidebarAutoScroll}" @click="toggleAutoscroll">
          Autoscroll: {{ store.sidebarAutoScroll ? 'ON' : 'OFF' }}
        </span>
        <span class="clear-link" @click="store.clearLogs()">Clear</span>
      </div>
    </div>

    <!-- Terminal Output -->
    <div id="log" ref="logContainer" @scroll="onLogScroll">
      <div v-if="store.logs.length === 0" style="color: #94a3b8; font-style: italic;">
        {{ store.isConnected ? `${store.fwName} (${store.fwVersion}) - Terminal Ready.` : 'Console cleared. Listening for incoming telemetry...' }}
      </div>
      <div v-for="(log, idx) in store.logs" :key="idx">
        <span style="color:#64748b">[{{ log.time }}]</span>
        <span :style="{color: log.color}"> {{ log.msg }}</span>
      </div>
    </div>

  </div>
</template>

<style scoped>
.content-right { 
  background: #0f172a; 
  display: flex; 
  flex-direction: column; 
  height: 100%; 
  min-height: 0; 
}
/* HUD Styles */
.quick-hud { 
  display: grid; 
  /* Adjusted fractions to give the last column enough room */
  grid-template-columns: 0.8fr 1.1fr 0.8fr 1.3fr; 
  background: #0b1120; 
  border-bottom: 1px solid #1e293b; 
}

.hud-item { 
  padding: 12px 10px; /* Slightly reduced side padding to save space */
  display: flex; 
  flex-direction: column; /* This restores the vertical stacking! */
  border-right: 1px solid #1e293b; 
  justify-content: center;
  min-width: 0; /* Prevents flexbox from pushing past grid boundaries */
}

.hud-item:last-child { border-right: none; }

.hud-item span { 
  font-size: 0.6rem; 
  color: #64748b; 
  text-transform: uppercase; 
  font-weight: 800; 
  margin-bottom: 4px; 
  letter-spacing: 0.05em;
}

.hud-item strong { 
  font-family: 'Fira Code', monospace; 
  font-size: 0.75rem; /* Scaled down slightly to fit longer statuses */
  color: var(--success); 
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis; /* Adds '...' if the text is somehow still too long */
}

/* Log Styles */
#log-header { 
  padding: 12px 20px; 
  background: #1e293b; 
  color: #94a3b8; 
  font-size: 0.7rem; 
  font-weight: 700; 
  text-transform: uppercase; 
  border-bottom: 1px solid #334155; 
  display: flex; 
  justify-content: space-between; 
  align-items: center; 
}

#log { 
  flex-grow: 1; 
  padding: 20px; 
  font-family: 'Fira Code', monospace; 
  font-size: 0.75rem; 
  color: #e2e8f0; 
  overflow-y: auto; 
  white-space: pre-wrap; 
  line-height: 1.4; 
}

.log-actions { display: flex; gap: 15px; align-items: center; }

.clear-link, .autoscroll-toggle { 
  cursor: pointer; 
  text-transform: none; 
  font-weight: 400; 
  transition: color 0.2s; 
  color: #64748b;
}

.clear-link:hover { color: #e2e8f0; }
.autoscroll-toggle.active { color: var(--info); font-weight: 700; }
</style>