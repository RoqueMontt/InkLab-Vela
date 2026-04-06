<script setup>
import { ref, watch, nextTick, onMounted, onActivated, onUnmounted, computed } from 'vue'
import { useSystemStore } from '../../stores/systemStore'
import { useSerial } from '../../composables/useSerial'

const store = useSystemStore()
const { sendCommand } = useSerial()

const cmd = ref('')
const outputRef = ref(null)
const inputRef = ref(null) 

const showTimestamps = ref(true)
const lineEnding = ref('\n') 

// Command History
const cmdHistory = ref([])
const historyIdx = ref(-1)

// --- NEW: Autocomplete State ---
const searchPrefix = ref('')
const autoCompleteIdx = ref(-1)

const commandGroups =[
  {
    name: 'System Utilities',
    cmds:[
      { label: 'SCAN', cmd: 'SCAN', desc: 'Scan SD Card for bitstreams' },
      { label: 'SETCLK', cmd: 'SETCLK:1:2', desc: 'Apply Clock Divider (Slot:Divider)' },
      { label: 'SYS:RESET', cmd: 'SYS:RESET', desc: 'Software Reboot the MCU' },
      { label: 'SYS:SLEEP', cmd: 'SYS:SLEEP:1', desc: 'Enter Active Sleep Mode (1=Sleep, 0=Wake)' },
      { label: 'TELEM:MUTE', cmd: 'TELEM:MUTE:1', desc: 'Mute JSON Telemetry (1/0)' },
      { label: 'TELEM:RATE', cmd: 'TELEM:RATE:50', desc: 'Set Telemetry Update Rate (50-5000ms)' },    
    ]
  },
  {
    name: 'SPI Diagnostics',
    cmds:[
      { label: 'SPI:PING', cmd: 'SPI:PING', desc: 'Full-Duplex Ping Test' },
      { label: 'SPI:ID', cmd: 'SPI:ID', desc: 'Read FPGA/SPI ID' },
      { label: 'SPI:WR', cmd: 'SPI:WR:00:FF', desc: 'Write Register (Addr:Val)' },
      { label: 'SPI:RD', cmd: 'SPI:RD:00', desc: 'Read Register (Addr)' },
      { label: 'SPI:BENCH', cmd: 'SPI:BENCH', desc: '10k Packet DMA Benchmark' },
      { label: 'SPI:BULK', cmd: 'SPI:BULK', desc: '400KB Bulk DMA Transfer' },
    ]
  },
  {
    name: 'BQ Power (Basics)',
    cmds:[
      { label: 'BQ:STATUS', cmd: 'BQ:STATUS', desc: 'Dump all BQ25798 Registers' },
      { label: 'BQ:CHG', cmd: 'BQ:CHG:1', desc: 'Enable/Disable Charging (1/0)' },
      { label: 'BQ:ICHG', cmd: 'BQ:ICHG:1000', desc: 'Set Charge Current (mA)' },
      { label: 'BQ:IIN', cmd: 'BQ:IIN:2000', desc: 'Set Input Limit (mA)' },
      { label: 'BQ:VREG', cmd: 'BQ:VREG:8400', desc: 'Set Charge Voltage (mV)' },
      { label: 'BQ:ITERM', cmd: 'BQ:ITERM:120', desc: 'Set Charge Termination Current (mA)' },
    ]
  },
  {
    name: 'BQ Power (Advanced)',
    cmds:[
      { label: 'BQ:TEST_BACKUP (Unplug)', cmd: 'BQ:TEST_BACKUP:1', desc: '[MACRO] Simulates a power loss safely' },
      { label: 'BQ:TEST_BACKUP (Restore)', cmd: 'BQ:TEST_BACKUP:0', desc: '[MACRO] Safely transitions back to USB power' },
      { label: 'BQ:BACKUP', cmd: 'BQ:BACKUP:1', desc: 'Arm/Disarm Auto-Backup Handoff (1/0)' },
      { label: 'BQ:DIS_IN', cmd: 'BQ:DIS_IN:1', desc: 'Force Disconnect Input MUX (1/0)' },
      { label: 'BQ:VBUS_OUT', cmd: 'BQ:VBUS_OUT:1', desc: 'Force ACFET1 Closed in OTG (Power VBUS from BAT)' },
      { label: 'BQ:SHIP', cmd: 'BQ:SHIP:2', desc: 'Enter Ship Mode (Kills Battery Output)' },
      { label: 'BQ:OTG', cmd: 'BQ:OTG:1', desc: 'Force OTG/Boost Mode ON/OFF (1/0)' },
      { label: 'BQ:VOTG', cmd: 'BQ:VOTG:5000', desc: 'Set OTG/Backup Output Voltage (mV)' },
      { label: 'BQ:IOTG', cmd: 'BQ:IOTG:2000', desc: 'Set OTG/Backup Output Current (mA)' },
      { label: 'BQ:HIZ', cmd: 'BQ:HIZ:1', desc: 'Force High-Z / Stop Switching (1/0)' },
      { label: 'BQ:VINDPM', cmd: 'BQ:VINDPM:4500', desc: 'Set Input Voltage Drop Limit (mV)' },
      { label: 'BQ:VSYS', cmd: 'BQ:VSYS:7000', desc: 'Set Min System Voltage (mV)' },
      { label: 'BQ:PFM_FWD', cmd: 'BQ:PFM_FWD:0', desc: 'Enable/Disable PFM mode for charge (1/0)' },
      { label: 'BQ:PFM_OTG', cmd: 'BQ:PFM_OTG:0', desc: 'Enable/Disable PFM mode for OTG mode (1/0)' },
      { label: 'BQ:WD', cmd: 'BQ:WD:0', desc: 'Enable/Disable Hardware Watchdog (1/0)' },
      { label: 'BQ:EMA', cmd: 'BQ:EMA:0.2', desc: 'Set ADC Filter EMA Coeff (0.1 to 1.0)' },
      { label: 'BQ:DETECT', cmd: 'BQ:DETECT', desc: 'Force Adapter Auto-Detection to Run' },
    ]
  }
]

const showFilterMenu = ref(false)
const filterWrapperRef = ref(null)

const closeFilterMenu = (e) => {
  if (showFilterMenu.value && filterWrapperRef.value && !filterWrapperRef.value.contains(e.target)) {
    showFilterMenu.value = false
  }
}

// --- NEW: Autocomplete Computed Property ---
const allAvailableCmds = commandGroups.flatMap(group => group.cmds.map(c => c.cmd))

const matchedCmds = computed(() => {
  if (!searchPrefix.value) return[]
  return allAvailableCmds.filter(c => c.toUpperCase().startsWith(searchPrefix.value.toUpperCase()))
})

// --- NEW: Triggered ONLY when user physically types, not when arrows change the value
const handleInput = (e) => {
  searchPrefix.value = e.target.value
  autoCompleteIdx.value = -1 // Reset the cycle index when typing changes
}

const handleCommandSelect = (event) => {
  const val = event.target.value
  if (val) {
    cmd.value = val
    searchPrefix.value = val
    autoCompleteIdx.value = -1
    inputRef.value?.focus()
    event.target.value = "" // Reset dropdown visually to the placeholder
  }
}

const fillCommand = (cmdText) => {
  cmd.value = cmdText
  searchPrefix.value = cmdText
  autoCompleteIdx.value = -1
  inputRef.value?.focus()
}

const onTerminalScroll = (e) => {
  const { scrollTop, scrollHeight, clientHeight } = e.target
  store.termAutoScroll = scrollHeight - scrollTop <= clientHeight + 10
  store.termScrollState = scrollTop
}

const forceScrollBottom = () => {
  store.termAutoScroll = true
  if (outputRef.value) outputRef.value.scrollTop = outputRef.value.scrollHeight
}

const restoreScroll = async () => {
  await nextTick()
  if (outputRef.value) {
    if (store.termAutoScroll) {
      outputRef.value.scrollTop = outputRef.value.scrollHeight
    } else {
      outputRef.value.scrollTop = store.termScrollState
    }
  }
  inputRef.value?.focus()
}

onMounted(() => {
  restoreScroll()
  document.addEventListener('click', closeFilterMenu)
})

onUnmounted(() => {
  document.removeEventListener('click', closeFilterMenu)
})

onActivated(restoreScroll)

watch(() => store.logs.length, async () => {
  if (store.termAutoScroll && outputRef.value) {
    await nextTick()
    outputRef.value.scrollTop = outputRef.value.scrollHeight
  }
})

const submitCmd = () => {
  if (!cmd.value.trim() || !store.isConnected) return
  sendCommand(cmd.value, lineEnding.value)
  store.addLog(`[TX]  ${cmd.value}`, '#f59e0b')
  
  if (cmdHistory.value[cmdHistory.value.length - 1] !== cmd.value) {
    cmdHistory.value.push(cmd.value)
  }
  historyIdx.value = cmdHistory.value.length
  
  // Clean up
  cmd.value = ''
  searchPrefix.value = ''
  autoCompleteIdx.value = -1
}

// --- UPDATED: Smart Arrow Keys ---
const onUpArrow = () => {
  if (!searchPrefix.value) {
    // BEHAVIOR A: Empty Input -> Standard History
    if (historyIdx.value > 0) {
      historyIdx.value--
      cmd.value = cmdHistory.value[historyIdx.value]
    }
  } else {
    // BEHAVIOR B: Has Prefix -> Cycle Autocomplete Matches
    if (matchedCmds.value.length > 0) {
      if (autoCompleteIdx.value <= 0) {
        autoCompleteIdx.value = matchedCmds.value.length - 1 // Wrap to bottom
      } else {
        autoCompleteIdx.value--
      }
      cmd.value = matchedCmds.value[autoCompleteIdx.value]
    }
  }
}

const onDownArrow = () => {
  if (!searchPrefix.value) {
    // BEHAVIOR A: Empty Input -> Standard History
    if (historyIdx.value < cmdHistory.value.length - 1) {
      historyIdx.value++
      cmd.value = cmdHistory.value[historyIdx.value]
    } else {
      historyIdx.value = cmdHistory.value.length
      cmd.value = '' 
    }
  } else {
    // BEHAVIOR B: Has Prefix -> Cycle Autocomplete Matches
    if (matchedCmds.value.length > 0) {
      if (autoCompleteIdx.value >= matchedCmds.value.length - 1) {
        autoCompleteIdx.value = 0 // Wrap to top
      } else {
        autoCompleteIdx.value++
      }
      cmd.value = matchedCmds.value[autoCompleteIdx.value]
    }
  }
}

const exportLog = () => {
  if (store.logs.length === 0) return
  const text = store.logs.map(l => showTimestamps.value ? `[${l.time}] ${l.msg}` : l.msg).join('\n')
  const blob = new Blob([text], { type: 'text/plain' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `inklab_session_${new Date().getTime()}.txt`
  a.click()
  URL.revokeObjectURL(url)
}
</script>

<template>
  <div class="sw-page">
    
    <div class="header-banner">
      <div style="flex-grow: 1;">
        <div style="display: flex; justify-content: space-between; align-items: center; width: 100%;">
          <span class="section-label" style="border: none; margin: 0; padding: 0;">Interactive IDE Terminal</span>
          <div class="header-actions">
            <button class="action-btn" @click="showTimestamps = !showTimestamps" :class="{ 'toggle-active': showTimestamps }">⏱️ Timestamps</button>
            <button class="action-btn" @click="exportLog" :disabled="store.logs.length === 0">💾 Export</button>
            <button class="action-btn" @click="store.clearLogs()">🗑️ Clear</button>
          </div>
        </div>
        
      </div>
    </div>
<!-- Replace the old cmd-dropdown and filter-bar with this: -->
        <div class="toolbar-bottom-row">
          <select class="cmd-dropdown" @change="handleCommandSelect($event)">
            <option value="" disabled selected>📦 Quick Command Library...</option>
            <optgroup v-for="group in commandGroups" :key="group.name" :label="group.name">
              <option v-for="c in group.cmds" :key="c.cmd" :value="c.cmd">
                {{ c.label }} &mdash; {{ c.desc }}
              </option>
            </optgroup>
          </select>

          <!-- NEW: Filter Dropdown -->
          <div class="filter-dropdown-wrapper" ref="filterWrapperRef">
            <button class="action-btn filter-btn" @click="showFilterMenu = !showFilterMenu" :class="{ 'toggle-active': showFilterMenu }">
              ⚙️ Filters
            </button>
            
            <div v-if="showFilterMenu" class="filter-menu">
              <div class="filter-header">Terminal Filters</div>
              <label v-for="(enabled, type) in store.filterSettings" :key="type" class="filter-menu-item">
                <input type="checkbox" v-model="store.filterSettings[type]"> 
                <span class="filter-label-text">{{ type.toUpperCase() }}</span>
              </label>
            </div>
          </div>
        </div>

    <div class="terminal-container">
      <div class="terminal-hud">
        <div class="hud-item"><span>STORAGE</span><strong :style="{ color: store.isSdInserted ? 'var(--success)' : 'var(--error)' }">{{ store.isSdInserted ? 'MEDIA OK' : 'NO MEDIA' }}</strong></div>
        <div class="hud-item">
          <span>{{ store.bqData.mode === 'OTG' ? 'PMID (BAT)' : 'PMID (USB)' }}</span>
          <strong>
            {{ store.bqData.vbusVolt.toFixed(1) }}V / 
            {{ Math.abs(store.bqData.mode === 'OTG' ? store.bqData.batCurr : store.bqData.vbusCurr).toFixed(0) }}mA
          </strong>
        </div>
        <div class="hud-item"><span>BATTERY</span><strong :style="{ color: store.bqData.soc < 20 ? 'var(--error)' : 'var(--success)' }">{{ store.bqData.soc.toFixed(0) }}%</strong></div>
        <div class="hud-item"><span>1.2V CORE</span><strong style="color: var(--info)">{{ store.powerData['1v2_core']?.v.toFixed(2) }}V / {{ store.powerData['1v2_core']?.i.toFixed(0) }}mA</strong></div>
        <div class="hud-item"><span>3.3V FPGA</span><strong style="color: var(--info)">{{ store.powerData['3v3_fpga']?.v.toFixed(2) }}V / {{ store.powerData['3v3_fpga']?.i.toFixed(0) }}mA</strong></div>
        <div class="hud-item"><span>3.3V MCU</span><strong style="color: var(--info)">{{ store.powerData['3v3_mcu']?.v.toFixed(2) }}V / {{ store.powerData['3v3_mcu']?.i.toFixed(0) }}mA</strong></div>
        <div class="hud-item"><span>3.3V EXT</span><strong style="color: var(--info)">{{ store.powerData['3v3_ext']?.v.toFixed(2) }}V / {{ store.powerData['3v3_ext']?.i.toFixed(0) }}mA</strong></div>
        <div class="hud-item"><span>FPGA SLOT</span><strong style="color: #e2e8f0">{{ store.activeSlot !== '--' ? store.activeSlot : 'UNCONFIGURED' }}</strong></div>
        <div class="hud-item"><span>SYS CLOCK</span><strong :style="{ color: store.clockStatus.includes('PULSING') ? 'var(--info)' : 'var(--warning)' }">{{ store.clockStatus }}</strong></div>
      </div>

      <div class="term-output" ref="outputRef" @scroll="onTerminalScroll">
        <div v-if="store.logs.length === 0" class="placeholder">
          {{ store.isConnected ? `${store.fwName} (${store.fwVersion}) - Terminal Ready.` : 'Connect hardware to begin terminal session...' }}
        </div>
       <div v-for="(log, i) in store.logs" :key="i" class="log-line">
          <span class="time" v-if="showTimestamps">[{{ log.time }}]</span>
          <span :style="{color: log.color || '#e2e8f0'}"> {{ log.msg }}</span>
        </div>
      </div>
      
      <div v-if="!store.termAutoScroll" class="scroll-alert" @click="forceScrollBottom">More messages below ↓</div>
      
      <div class="term-input-row">
        <span class="prompt">TX &gt;</span>
        <input 
          ref="inputRef"
          type="text" 
          v-model="cmd" 
          @input="handleInput"
          @keyup.enter="submitCmd"
          @keydown.up.prevent="onUpArrow"
          @keydown.down.prevent="onDownArrow"
          placeholder="Enter command..."
          :disabled="!store.isConnected"
          autocomplete="off"
        />
        
        <select v-model="lineEnding" class="line-ending-select">
          <option :value="''">None</option>
          <option :value="'\n'">LF</option>
          <option :value="'\r'">CR</option>
          <option :value="'\r\n'">CRLF</option>
        </select>

        <button class="btn-primary" @click="submitCmd" :disabled="!store.isConnected || !cmd">Send</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.sw-page { display: flex; flex-direction: column; gap: 20px; height: 100%; }
.header-banner { display: flex; justify-content: space-between; align-items: flex-start; border-bottom: 1px solid var(--edge); padding-bottom: 12px; }

/* --- NEW TOOLBAR ROW --- */
.toolbar-bottom-row {
  display: flex;
  gap: 12px;
  align-items: stretch; /* Forces children to match heights */
  height: 38px;         /* Fixed height for perfect alignment */
}

.cmd-dropdown {
  flex-grow: 1; 
  width: 100%;
  max-width: 400px;
  padding: 0 14px;
  border-radius: 8px;
  background: var(--ghost);
  border: 1px solid var(--edge);
  font-family: 'Inter', sans-serif;
  font-size: 0.8rem;
  font-weight: 600;
  color: var(--ink);
  cursor: pointer;
  outline: none;
  transition: border-color 0.2s;
}
.cmd-dropdown:focus { border-color: var(--info); }
.cmd-dropdown optgroup { font-weight: 800; color: var(--accent); }
.cmd-dropdown option { font-weight: 500; color: var(--ink); font-family: 'Fira Code', monospace; }

/* --- FILTER DROPDOWN --- */
.filter-dropdown-wrapper {
  position: relative;
  height: 100%;
}

.filter-btn {
  height: 100%;
  padding: 0 16px;
  display: flex;
  align-items: center;
}

.filter-menu {
  position: absolute;
  top: calc(100% + 8px);
  right: 0;
  background: var(--paper);
  border: 1px solid var(--edge);
  border-radius: 8px;
  box-shadow: 0 10px 25px rgba(0,0,0,0.15);
  width: 180px;
  z-index: 100;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.filter-header {
  padding: 10px 12px;
  background: var(--ghost);
  font-size: 0.7rem;
  font-weight: 800;
  color: var(--accent);
  text-transform: uppercase;
  border-bottom: 1px solid var(--edge);
}

.filter-menu-item {
  padding: 10px 12px;
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 0.8rem;
  font-weight: 600;
  color: var(--ink);
  cursor: pointer;
  transition: background 0.2s;
}

.filter-menu-item:hover {
  background: #f1f5f9;
}

.filter-label-text {
  margin-top: 2px; /* Visual alignment fix for checkboxes */
}

/* --- TERMINAL ACTIONS --- */
.header-actions { display: flex; gap: 8px; }
.action-btn { width: auto; padding: 6px 12px; font-size: 0.75rem; background: var(--paper); color: var(--ink); border: 1px solid var(--edge); transition: all 0.2s; cursor: pointer; border-radius: 6px; font-weight: 600; }
.action-btn:hover:not(:disabled) { background: #f1f5f9; }
.action-btn.toggle-active { color: var(--info); border-color: var(--info); background: #eff6ff; }
.action-btn:disabled { opacity: 0.5; cursor: not-allowed; }

/* --- TERMINAL HUD & CONTAINER --- */
.terminal-container { position: relative; display: flex; flex-direction: column; flex-grow: 1; background: #0f172a; border-radius: 8px; overflow: hidden; border: 1px solid var(--edge); min-height: 400px; }
.terminal-hud { display: grid; grid-template-columns: repeat(9, 1fr); background: #0b1120; border-bottom: 1px solid #1e293b; }
.hud-item { padding: 12px 10px; display: flex; flex-direction: column; border-right: 1px solid #1e293b; justify-content: center; }
.hud-item:last-child { border-right: none; }
.hud-item span { font-size: 0.55rem; color: #64748b; text-transform: uppercase; font-weight: 800; margin-bottom: 4px; letter-spacing: 0.05em; white-space: nowrap; }
.hud-item strong { font-family: 'Fira Code', monospace; font-size: 0.75rem; color: var(--success); font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }

/* --- TERMINAL OUTPUT --- */
.term-output { flex-grow: 1; padding: 16px; overflow-y: auto; font-family: 'Fira Code', monospace; font-size: 0.85rem; line-height: 1.5; color: #e2e8f0; display: flex; flex-direction: column; white-space: pre-wrap; scrollbar-width: thin; scrollbar-color: #334155 #0f172a; }
.placeholder { color: #64748b; font-style: italic; }
.log-line .time { color: #64748b; margin-right: 8px; }
.scroll-alert { position: absolute; bottom: 80px; right: 20px; background: rgba(30, 41, 59, 0.9); color: #93c5fd; border: 1px solid #3b82f6; padding: 8px 16px; border-radius: 20px; font-size: 0.75rem; font-weight: 700; cursor: pointer; backdrop-filter: blur(4px); box-shadow: 0 4px 6px rgba(0,0,0,0.3); transition: background 0.2s; z-index: 10; }
.scroll-alert:hover { background: #1e293b; color: #bfdbfe; }

/* --- TERMINAL INPUT BAR --- */
.term-input-row { display: flex; align-items: center; padding: 12px; background: #1e293b; gap: 12px; border-top: 1px solid #334155; }
.prompt { color: var(--info); font-family: 'Fira Code', monospace; font-weight: 700; font-size: 0.9rem; }
input[type="text"] { flex-grow: 1; background: #0f172a; border: 1px solid #334155; color: #e2e8f0; font-family: 'Fira Code', monospace; padding: 10px 16px; border-radius: 6px; outline: none; font-size: 0.85rem; }
input[type="text"]:focus { border-color: var(--info); }
input[type="text"]:disabled { opacity: 0.5; cursor: not-allowed; }

.line-ending-select { background: #0f172a; color: #94a3b8; border: 1px solid #334155; padding: 10px; border-radius: 6px; font-family: 'Fira Code', monospace; font-size: 0.8rem; font-weight: 600; outline: none; cursor: pointer; }
.line-ending-select:focus { border-color: var(--info); }

.btn-primary { background: var(--info); color: white; border: none; padding: 10px 24px; border-radius: 6px; font-weight: 600; cursor: pointer; transition: 0.2s; width: auto; }
.btn-primary:hover:not(:disabled) { background: #2563eb; }
.btn-primary:disabled { background: #334155; color: #64748b; cursor: not-allowed; }
</style>