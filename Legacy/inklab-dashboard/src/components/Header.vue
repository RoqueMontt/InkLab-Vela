<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { useSystemStore } from '../stores/systemStore'
import { useSerial } from '../composables/useSerial'

const store = useSystemStore()
const { connect, sendCommand } = useSerial()

const props = defineProps(['activeTab'])
const emit = defineEmits(['update:activeTab'])

const setTab = (tab) => emit('update:activeTab', tab)

const delay = (ms) => new Promise(res => setTimeout(res, ms))

const toggleAppMode = () => {
  store.appMode = store.appMode === 'HW' ? 'SW' : 'HW'
}

watch([() => store.bqData.mode, () => store.bqData.rawRegs, () => store.clockStatus], ([mode, regs, clk]) => {
  // Wait until we actually have the hardware register dump
  if (!store.isConnected || !regs || regs.length < 0x13) return;

  // Reconstruct the current OTG voltage setting from the BQ registers
  const votgWord = (regs[0x0B] << 8) | regs[0x0C];
  const currentVotg = (votgWord * 10) + 2800;

  // Deduce the macro state
  if (mode === 'OTG') {
    // If we are running on battery, check the voltage limit to determine Eco vs Max
    store.sysMode = currentVotg <= 3600 ? 'ECO_BACKUP' : 'HV_BACKUP';
  } else if (clk === 'IDLE' || clk === 'STANDBY') {
    // If not on battery and the clock is suspended, we are in Standby
    store.sysMode = 'STANDBY';
  } else {
    // Standard operation
    store.sysMode = 'ACTIVE';
  }
}, { deep: true });

// ==========================================
// CUSTOM DROPDOWN LOGIC
// ==========================================
const showSysMode = ref(false)
const sysModeWrapperRef = ref(null)

const sysModes = [
  { id: 'ACTIVE', label: 'Mode: Active' },
  { id: 'STANDBY', label: 'Mode: Standby' },
  { id: 'ECO_BACKUP', label: 'Eco Backup (3.6V)' },
  { id: 'HV_BACKUP', label: 'Max Backup (5.5V)' }
]

const currentModeLabel = computed(() => {
  const mode = sysModes.find(m => m.id === store.sysMode)
  return mode ? mode.label : 'Select Mode'
})

// Click-away listener to close the dropdown
const closeDropdown = (e) => {
  if (showSysMode.value && sysModeWrapperRef.value && !sysModeWrapperRef.value.contains(e.target)) {
    showSysMode.value = false
  }
}

onMounted(() => document.addEventListener('click', closeDropdown))
onUnmounted(() => document.removeEventListener('click', closeDropdown))

// Applies the selected mode to the hardware
const applySystemMode = async (modeId) => {
  showSysMode.value = false // Close the menu visually
  store.sysMode = modeId
  
  if (!store.isConnected) return

  if (modeId === 'ACTIVE') {
    await sendCommand('SYS:SLEEP:0')
    await sendCommand('BQ:TEST_BACKUP:0')
    store.ledState.muted = false
  } 
  else if (modeId === 'STANDBY') {
    await sendCommand('BQ:TEST_BACKUP:0') 
    await sendCommand('SYS:SLEEP:1')
    store.ledState.muted = true
  } 
  else if (modeId === 'ECO_BACKUP') {
    await sendCommand('SYS:SLEEP:0')
    await sendCommand('BQ:VOTG:3600') // 3.6V limit
    await delay(100)                  // Let the I2C register settle
    await sendCommand('BQ:TEST_BACKUP:1')
    store.ledState.muted = false
  } 
  else if (modeId === 'HV_BACKUP') {
    await sendCommand('SYS:SLEEP:0')
    await sendCommand('BQ:VOTG:5500') // 5.5V limit
    await delay(100)                  // Let the I2C register settle
    await sendCommand('BQ:TEST_BACKUP:1')
    store.ledState.muted = false
  }
}
</script>

<template>
  <header>
    <!-- Left Side: Brand & Navigation -->
    <div class="header-left">
      <div class="brand">
        <div class="logo-box">IL</div>
        <h1 style="margin:0; font-size:1.1rem;">InkLab <span style="font-weight: 300; color: #64748b;">DevConsole</span></h1>
      </div>
      
      <div class="nav-tabs">
        <button 
          v-for="tab in store.availableTabs[store.appMode]" 
          :key="tab.id"
          class="tab-btn" 
          :class="{ active: activeTab === tab.id }" 
          @click="setTab(tab.id)">
          {{ tab.label }}
        </button>
      </div>
    </div>

    <!-- Right Side: Control Panel Row -->
    <div class="header-actions">
      
      <!-- 1. One-Click HW / SW Segmented Toggle -->
      <div class="segmented-mode" title="Toggle App Mode">
        <div class="segment" :class="{active: store.appMode === 'CONFIG'}" @click="store.appMode = 'CONFIG'">CONFIG</div>
        <div class="segment" :class="{active: store.appMode === 'DEBUG'}" @click="store.appMode = 'DEBUG'">DEBUG</div>
        <div class="segment" :class="{active: store.appMode === 'APP'}" @click="store.appMode = 'APP'">APP</div>
      </div>

      <div class="divider"></div>

      <!-- 2. System Power Mode Selector (Custom Styled UI) -->
      <div class="sys-mode-wrapper" ref="sysModeWrapperRef">
        
        <!-- The visual trigger button -->
        <button class="custom-select-btn" @click="showSysMode = !showSysMode" :disabled="!store.isConnected">
          <span class="mode-icon">⚡</span>
          <span class="mode-label">{{ currentModeLabel }}</span>
          <span class="custom-caret">▼</span>
        </button>

        <!-- The beautiful, custom dropdown menu box -->
        <div v-if="showSysMode" class="custom-options-menu">
          <div 
            v-for="mode in sysModes" 
            :key="mode.id"
            class="custom-option"
            :class="{ selected: store.sysMode === mode.id }"
            @click="applySystemMode(mode.id)"
          >
            {{ mode.label }}
          </div>
        </div>

      </div>

      <div class="divider"></div>

      <!-- 3. Diagnostics Launcher -->
      <button class="diag-btn" @click="store.showDiagnosticModal = true" title="System Diagnostics">
        🩺
      </button>

      <!-- 4. Minimalist Connection Indicator -->
      <div class="conn-status" :class="{ linked: store.isConnected }" @click="!store.isConnected && connect()">
        <div class="dot"></div>
        <span>{{ store.isConnected ? 'Linked' : 'Connect' }}</span>
      </div>

    </div>
  </header>
</template>

<style scoped>
header { 
  background: var(--ghost); 
  padding: 16px 32px; 
  border-bottom: 1px solid var(--edge); 
  display: flex; 
  justify-content: space-between; 
  align-items: center; 
}

.header-left { display: flex; align-items: center; gap: 64px; }
.brand { display: flex; align-items: center; gap: 12px; }
.logo-box { background: var(--ink); color: white; padding: 6px 10px; font-weight: 800; border-radius: 6px; font-size: 0.9rem; }
.nav-tabs { display: flex; gap: 8px; }
.tab-btn { background: transparent; color: #64748b; border: none; border-bottom: 2px solid transparent; padding: 8px 16px; cursor: pointer; font-weight: 600; border-radius: 0;   white-space: nowrap; }
.tab-btn.active { color: var(--info); border-bottom-color: var(--info); }

/* --- RIGHT SIDE CONTROLS --- */
.header-actions { 
  display: flex; 
  align-items: center; 
  gap: 16px; 
}

.divider {
  width: 1px;
  height: 24px;
  background: var(--edge);
}

/* One-Click Segmented HW/SW Toggle */
.segmented-mode {
  display: flex;
  background: #e2e8f0;
  border-radius: 6px;
  padding: 3px;
  cursor: pointer;
  user-select: none;
}
.segment {
  padding: 4px 14px;
  font-size: 0.75rem; font-weight: 800; color: #64748b;
  border-radius: 4px; transition: 0.2s; text-align: center;
}
.segment.active {
  background: white; color: var(--ink); box-shadow: 0 1px 3px rgba(0,0,0,0.1);
}

/* --- CUSTOM SYSTEM MODE DROPDOWN --- */
.sys-mode-wrapper {
  position: relative; /* Anchor for the absolute positioned menu */
}

.custom-select-btn {
  display: flex;
  align-items: center;
  gap: 8px;
  background: white;
  border: 1px solid var(--edge);
  padding: 6px 12px;
  border-radius: 6px;
  font-family: inherit;
  font-weight: 700;
  font-size: 0.8rem;
  color: var(--ink);
  cursor: pointer;
  width: auto;
  min-width: 185px;
  white-space: nowrap;
  text-align: left;
  transition: 0.2s;
}
.custom-select-btn:hover:not(:disabled) { background: #f8fafc; border-color: #cbd5e1; }
.custom-select-btn:disabled { opacity: 0.5; cursor: not-allowed; }

.mode-label { flex-grow: 1; }
.mode-icon { font-size: 0.9rem; pointer-events: none; }
.custom-caret { font-size: 0.6rem; color: #94a3b8; pointer-events: none; }

/* The Opened Popover Menu */
.custom-options-menu {
  position: absolute;
  top: calc(100% + 6px); /* Drops it just below the button */
  left: 0;
  width: 100%;
  background: var(--paper);
  border: 1px solid var(--edge);
  border-radius: 6px;
  box-shadow: 0 10px 25px rgba(0,0,0,0.15);
  z-index: 100;
  overflow: hidden; 
  display: flex;
  flex-direction: column;
}

/* Individual Items in Menu */
.custom-option {
  padding: 10px 12px;
  font-size: 0.8rem;
  font-weight: 600;
  color: var(--ink);
  cursor: pointer;
  transition: background 0.1s, color 0.1s;
}
.custom-option:hover {
  background: var(--ghost);
  color: var(--info);
}

/* The Currently Active Item */
.custom-option.selected {
  background: #eff6ff;
  color: var(--info);
  font-weight: 800;
}

/* Diagnostics Button */
.diag-btn {
  display: flex; align-items: center; justify-content: center;
  background: white; border: 1px solid var(--edge);
  width: 34px; height: 34px; border-radius: 6px;
  cursor: pointer; transition: 0.2s; font-size: 1.1rem;
}
.diag-btn:hover { background: #f8fafc; border-color: #cbd5e1; transform: scale(1.05); }

/* Minimalist Connection Indicator */
.conn-status {
  display: flex; align-items: center; gap: 8px;
  padding: 6px 14px; border-radius: 20px;
  font-size: 0.75rem; font-weight: 800; text-transform: uppercase; letter-spacing: 0.05em;
  background: white; border: 1px solid var(--edge);
  color: var(--ink); cursor: pointer; transition: 0.2s;
}
.conn-status:hover:not(.linked) { background: #f8fafc; border-color: #cbd5e1; }
.conn-status.linked { 
  background: transparent; border-color: transparent; 
  cursor: default; color: #64748b; pointer-events: none;
}
.dot { 
  width: 8px; height: 8px; border-radius: 50%; 
  background: var(--error); box-shadow: 0 0 0 2px rgba(239,68,68,0.2); 
}
.conn-status.linked .dot { 
  background: var(--success); box-shadow: 0 0 0 2px rgba(16,185,129,0.2); 
}
</style>