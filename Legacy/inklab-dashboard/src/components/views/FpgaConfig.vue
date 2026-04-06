<script setup>
import { ref, computed, onMounted, onActivated, watch } from 'vue'
import { useSystemStore } from '../../stores/systemStore'
import { useSerial } from '../../composables/useSerial'
import CustomSelect from '../CustomSelect.vue' 

const store = useSystemStore()
const { sendCommand, startUpload, scanSlots } = useSerial()

const fwName = ref('')
const fwVersion = ref('')

const slotAlias = ref('')
const selectedSlot = ref(1)
const clkDivider = ref(2)
const fileBuffer = ref(null)
const fileName = ref('Choose .bin')

// --- Advanced Operations State ---
const showSlotZeroModal = ref(false)
const showWipeModal = ref(false)
const showOtaModal = ref(false)

const sourceSlotForZero = ref(1)
const otaFileBuffer = ref(null)
const otaFileName = ref('Select Firmware .bin')
const otaSuccess = ref(false)

const isFpgaStopped = computed(() => {
  const status = store.clockStatus.toUpperCase();
  return status.includes('STOPPED') || status === 'IDLE' || status.includes('FAILED');
})
const allSlotOptions = computed(() => {
  return Array.from({ length: 15 }, (_, i) => {
    const val = i + 1;
    return {
      value: val,
      label: `Slot ${val.toString().padStart(2, '0')} (${store.slotAliases[val] || 'unconfigured'})`
    }
  })
})

watch(() => store.lastProgrammedSlot, (newVal) => {
  if (newVal !== 'NONE' && newVal !== '--') {
    const slotNum = parseInt(newVal);
    if (!isNaN(slotNum) && slotNum >= 1 && slotNum <= 15) {
      selectedSlot.value = slotNum;
    }
  }
});

watch(showOtaModal, (isShowing) => {
  if (!isShowing) {
    otaTerminalLogs.value = [];
    otaFileBuffer.value = null;
    otaFileName.value = 'Select Firmware .bin';
    otaSuccess.value = false;
  }
});

watch(() => store.isConnected, (connected) => {
  if (showOtaModal.value) {
    const time = new Date().toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
    if (!connected) {
      otaTerminalLogs.value.push({ id: otaLogIdCounter++, text: `[${time}] [SYS] Connection lost. MCU is rebooting...` });
    } else {
      otaTerminalLogs.value.push({ id: otaLogIdCounter++, text: `[${time}] [SYS] Reconnected! Awaiting firmware validation...` });
    }
  }
});

const incrementDivider = () => { if (clkDivider.value < 128) clkDivider.value *= 2 }
const decrementDivider = () => { if (clkDivider.value > 2) clkDivider.value /= 2 }
const freqPreview = computed(() => (64 / clkDivider.value).toFixed(2))

onActivated(() => { if (store.isConnected) scanSlots() })
onMounted(() => { if (store.isConnected) scanSlots() })

const handleFile = (e) => {
  const file = e.target.files[0]
  if (!file) return
  fileName.value = file.name
  const fr = new FileReader()
  fr.onload = (ev) => {
    fileBuffer.value = new Uint8Array(ev.target.result)
    store.addLog(`Prepared FPGA Bitstream: ${file.name} (${(fileBuffer.value.length / 1024).toFixed(1)} KB)`)
  }
  fr.readAsArrayBuffer(file)
}

const handleOtaFile = (e) => {
  const file = e.target.files[0]
  if (!file) return
  otaFileName.value = file.name
  const fr = new FileReader()
  fr.onload = (ev) => {
    otaFileBuffer.value = new Uint8Array(ev.target.result)
    store.addLog(`Prepared MCU Firmware: ${file.name} (${(otaFileBuffer.value.length / 1024).toFixed(1)} KB)`)
  }
  fr.readAsArrayBuffer(file)
}

const clockColor = computed(() => {
  const txt = store.clockStatus.toUpperCase()
  if (txt.includes('PULSING')) return 'var(--info)'
  if (txt.includes('STOPPED') || txt === 'IDLE') return 'var(--warning)'
  if (txt.includes('FAILED')) return 'var(--error)'
  return 'var(--success)'
})

const executeSlotZeroUpdate = () => {
  store.addLog(`Copying Slot ${sourceSlotForZero.value} to auto-boot Slot 0...`, 'var(--info)')
  sendCommand(`SYS:SETZERO:${sourceSlotForZero.value}`)
  setTimeout(() => {
    store.addLog("SUCCESS: Hard Power Cycling system in 3... 2... 1...", "var(--warning)")
    sendCommand('BQ:HIZ:1')
    sendCommand('BQ:DIS_IN:1')
    setTimeout(() => { sendCommand('BQ:SHIP:2') }, 150)
  }, 2500)
  showSlotZeroModal.value = false
}

// ==================================================
// VANISHING OTA TERMINAL LOGIC
// ==================================================
const otaTerminalLogs = ref([])
let otaLogIdCounter = 0 

const visibleOtaLogs = computed(() => {
  return otaTerminalLogs.value.slice(-6);
})

const getOpacity = (idx, total) => {
  const distance = total - 1 - idx; 
  if (distance === 0) return 1.0; 
  if (distance === 1) return 0.8; 
  if (distance === 2) return 0.6; 
  if (distance === 3) return 0.4;
  if (distance === 4) return 0.25; 
  return 0.15;                    
}

// Add this outside the watch function to track progress
let lastProcessedLength = 0;

// Replace your current watch(() => store.logs, ...) block with this:
watch(() => store.logs.length, (newLength) => {
  if (newLength === 0) {
    lastProcessedLength = 0;
    return;
  }
  
  // Process every single new log that arrived in this tick
  for (let i = lastProcessedLength; i < newLength; i++) {
    const log = store.logs[i];
    
    if (log.msg.includes("OTA_LOG:")) {
      const cleanMsg = log.msg.replace("OTA_LOG: ", "");
      otaTerminalLogs.value.push({ 
        id: otaLogIdCounter++, 
        text: `[${log.time}] ${cleanMsg}` 
      });
    }

    if (log.msg.includes("OTA SUCCESS")) {
      otaSuccess.value = true;
      otaTerminalLogs.value.push({ 
        id: otaLogIdCounter++, 
        text: `[${log.time}] 🌟 UPDATE COMPLETE AND VERIFIED 🌟` 
      });

      setTimeout(() => {
        store.addLog(">>> Update complete. Cleaning up background logs...", "var(--success)");
        setTimeout(() => store.clearLogs(), 50); 
      }, 2500);
    }
  }
  
  lastProcessedLength = newLength;
});

const executeOtaUpdate = async () => {
  if (!otaFileBuffer.value || !fwName.value || !fwVersion.value) return
  otaTerminalLogs.value = [] 
  otaTerminalLogs.value.push({ id: otaLogIdCounter++, text: `[SYS] Initializing OTA Upload for ${otaFileName.value}...` })
  
  // 1. AWAIT the command so it finishes acquiring/releasing the serial port lock
  await sendCommand(`SYS:SET_FW_INFO:${fwName.value}:${fwVersion.value}`)
  
  // 2. Give the MCU 100ms to open, write, and close the text file on the SD Card
  await new Promise(r => setTimeout(r, 100))

  // 3. Now the port is free, and the MCU is ready to receive the heavy bitstream
  await startUpload(otaFileBuffer.value, 99, 2, "OTA_FW")
  
  const unwatch = watch(() => store.isUploading, (isUploading) => {
    if (!isUploading) {
      otaTerminalLogs.value.push({ id: otaLogIdCounter++, text: "[SYS] Upload Complete. Triggering Dual-Bank Swap..." })
      setTimeout(() => sendCommand("SYS:OTA:ota.bin"), 500)
      unwatch() 
    }
  })
}

const executeWipeSd = () => {
  sendCommand('SYS:WIPE_SD')
  setTimeout(() => { scanSlots() }, 500)
  showWipeModal.value = false
}
</script>

<template>
  <div class="fpga-page">
    
    <div class="dashboard">
      <div class="stat"><div class="stat-label">Transfer</div><div class="stat-value">{{ store.transferSpeed }}</div></div>
      <div class="stat"><div class="stat-label">Progress</div><div class="stat-value">{{ store.uploadProgress }}%</div></div>
      <div class="stat"><div class="stat-label">Active Slot</div><div class="stat-value">{{ store.activeSlot }}</div></div>
      <div class="stat"><div class="stat-label">Clock Status</div><div class="stat-value" :style="{color: clockColor}">{{ store.clockStatus }}</div></div>
    </div>

    <div class="config-card">
      <div class="card-header"><span class="section-label">Hardware Configuration</span></div>
      <div class="controls-grid">
        <!-- NEW: FPGA Power Toggle -->
        <div class="control-group">
          <label>FPGA Power</label>
          <div class="segmented-control" style="height: 42px;">
            <button :class="{active: !isFpgaStopped}" @click="sendCommand('SYS:FPGA_PWR:1')" :disabled="!store.isConnected || store.isUploading">ON</button>
            <button :class="{active: isFpgaStopped}" @click="sendCommand('SYS:FPGA_PWR:0')" :disabled="!store.isConnected || store.isUploading">OFF</button>
          </div>
        </div>

        <div class="control-group">
          <label>Target Memory Slot</label>
          <CustomSelect v-model="selectedSlot" :options="allSlotOptions" />
        </div>
        
        <div class="control-group">
          <label>Clock Divider</label>
          <div class="step-input">
            <button @click="decrementDivider" :disabled="clkDivider <= 2">-</button>
            <div class="divider-value"><span>{{ clkDivider }}</span><small>{{ freqPreview }} MHz</small></div>
            <button @click="incrementDivider" :disabled="clkDivider >= 128">+</button>
          </div>
        </div>
        
        <div class="control-group">
          <label> </label>
          <!-- Disable Apply Clock button if FPGA is powered off -->
          <button class="btn-warning w-full" @click="sendCommand(`SETCLK:${selectedSlot}:${clkDivider}`)" :disabled="!store.isConnected || store.isUploading || isFpgaStopped">
            Apply Clock
          </button>
        </div>
      </div>
    </div>

    <div class="upload-card">
      <span class="section-label">Bitstream Upload (SD Card)</span>
      <div class="upload-row">
        <div class="control-group">
          <label>Slot Alias (Required)</label>
          <input type="text" v-model="slotAlias" placeholder="e.g. MyLogic_V1" maxlength="15">
        </div>
        <div class="control-group">
          <label>Select Binary File</label>
          <div class="file-wrapper">
            <input type="file" id="fileInput" accept=".bin" @change="handleFile" style="display:none;">
            <label for="fileInput" class="file-label">{{ fileName }}</label>
          </div>
        </div>
        <div class="control-group">
          <label>Upload Control</label>
          <button class="btn-primary" 
                  :disabled="!store.isConnected || !fileBuffer || !slotAlias || store.isUploading" 
                  @click="startUpload(fileBuffer, selectedSlot, clkDivider, slotAlias)">
            Start Upload
          </button>
        </div>
      </div>
    </div>

    <!-- UPDATED: ADVANCED SYSTEM MANAGEMENT -->
    <div class="config-card system-card">
      <div class="card-header"><span class="section-label" style="color: var(--accent);">Advanced System Management</span></div>
      
      <!-- Split into two distinct columns -->
      <div class="advanced-grid">
        
        <!-- Column 1: MCU Settings -->
        <div class="advanced-group">
          <div class="group-title">MCU Firmware Management</div>
          <div class="group-buttons">
            <button class="btn-secondary" @click="showOtaModal = true" :disabled="!store.isConnected">
              <span class="icon">⚡</span> Update MCU Firmware (OTA)
            </button>
            <button class="btn-secondary" @click="sendCommand('SYS:BANK')" :disabled="!store.isConnected">
              <span class="icon">🔍</span> Check Active Bank
            </button>
          </div>
        </div>

        <!-- Column 2: FPGA Settings -->
        <div class="advanced-group">
          <div class="group-title">FPGA / SD Storage Management</div>
          <div class="group-buttons">
            <button class="btn-secondary" @click="showSlotZeroModal = true" :disabled="!store.isConnected">
              <span class="icon">💾</span> Set Default Bitstream (Slot 0)
            </button>
            <button class="btn-danger-outline" @click="showWipeModal = true" :disabled="!store.isConnected">
              <span class="icon">🗑️</span> Format SD Bitstreams
            </button>
          </div>
        </div>

      </div>
    </div>

  </div>

  <!-- MODAL: Set Default Bitstream (Slot 0) -->
  <div v-if="showSlotZeroModal" class="custom-modal-overlay" @click.self="showSlotZeroModal = false">
    <div class="custom-modal-card">
      <div class="modal-header info-header">💾 Set Default Auto-Boot Bitstream</div>
      <div class="modal-body">
        <p>This will copy a bitstream into the dedicated auto-boot slot (Slot 0) on the SD Card. This logic will automatically load when the system powers on.</p>
        <div class="control-group" style="margin-top: 20px;">
          <label>Source Bitstream</label>
          <CustomSelect v-model="sourceSlotForZero" :options="allSlotOptions " />
        </div>
      </div>
      <div class="modal-actions">
        <button class="btn-text" @click="showSlotZeroModal = false">Cancel</button>
        <button class="btn-primary" @click="executeSlotZeroUpdate">Set as Auto-Boot</button>
      </div>
    </div>
  </div>

<div v-if="showOtaModal" class="custom-modal-overlay" @click.self="showOtaModal = false">
    <div class="custom-modal-card ota-modal-card">
      <div class="modal-header warning-header">⚡ Dual-Bank Firmware Update</div>
      <div class="modal-body">
        <p>Upload a new <code>.bin</code> firmware file for the STM32G0 MCU. The system will stage the update in the background and automatically reboot to apply it.</p>
        
        <div class="controls-grid" style="margin-top: 20px;">
          <div class="control-group">
            <label>Firmware Name</label>
            <input type="text" v-model="fwName" placeholder="e.g. InkLab Core" maxlength="20" :disabled="store.isUploading || otaSuccess">
          </div>
          <div class="control-group">
            <label>Version</label>
            <input type="text" v-model="fwVersion" placeholder="e.g. v2.4" maxlength="10" :disabled="store.isUploading || otaSuccess">
          </div>
        </div>

        <div class="control-group" style="margin-top: 16px;">
          <label>Select Firmware File</label>
          <div class="file-wrapper">
            <input type="file" id="otaFileInput" accept=".bin" @change="handleOtaFile" style="display:none;" :disabled="store.isUploading || otaSuccess">
            <label for="otaFileInput" class="file-label" :class="{ disabled: store.isUploading || otaSuccess }">{{ otaFileName }}</label>
          </div>
        </div>

        <div v-if="otaTerminalLogs.length > 0" class="mini-terminal">
          <div 
            v-for="(log, idx) in visibleOtaLogs" 
            :key="log.id" 
            class="mini-log-line"
            :class="{ 
              'text-success': log.text.includes('SUCCESS') || log.text.includes('COMPLETE'), 
              'text-error': log.text.includes('ERR') 
            }"
            :style="{ opacity: getOpacity(idx, visibleOtaLogs.length) }"
          >
            {{ log.text }}
          </div>
        </div>

      </div> <div class="modal-actions">
        <button class="btn-text" @click="showOtaModal = false">{{ otaSuccess ? 'Close' : 'Cancel' }}</button>
        <button v-if="!otaSuccess" class="btn-warning" @click="executeOtaUpdate" :disabled="!otaFileBuffer || !fwName || !fwVersion || store.isUploading">Start OTA Update</button>
        <button v-else class="btn-primary" @click="showOtaModal = false">Finish</button>
      </div>
    </div>
  </div>

  <!-- MODAL: Format SD Card -->
  <div v-if="showWipeModal" class="custom-modal-overlay" @click.self="showWipeModal = false">
    <div class="custom-modal-card">
      <div class="modal-header error-header">⚠️ Wipe SD Bitstreams</div>
      <div class="modal-body">
        <p>Are you sure you want to delete all bitstreams (Slots 1-15) from the SD Card? <b>The default auto-boot bitstream (Slot 0) will remain intact.</b></p>
      </div>
      <div class="modal-actions">
        <button class="btn-text" @click="showWipeModal = false">Cancel</button>
        <button class="btn-danger" @click="executeWipeSd">Yes, Wipe SD Card</button>
      </div>
    </div>
  </div>

</template>

<style scoped>
/* REDUCED GAP TO COMPRESS VERTICAL SPACE */
.fpga-page { display: flex; flex-direction: column; gap: 16px; padding: 10px; }

.dashboard { 
  display: grid; grid-template-columns: repeat(4, 1fr); 
  gap: 1px; background: var(--edge); border: 1px solid var(--edge); border-radius: 8px; overflow: hidden; 
}
.stat { background: var(--paper); padding: 12px 16px; text-align: center; } /* REDUCED TOP/BOTTOM PADDING */
.stat-label { font-size: 0.65rem; color: #64748b; text-transform: uppercase; font-weight: 700; margin-bottom: 4px; }
.stat-value { font-family: 'Fira Code', monospace; font-size: 1rem; font-weight: 600; }

.config-card, .upload-card {
  background: white; border: 1px solid var(--edge); border-radius: 12px; padding: 16px; /* REDUCED PADDING */
  box-shadow: 0 2px 4px rgba(0,0,0,0.02);
}

.card-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; } /* REDUCED MARGIN */
.section-label { font-weight: 800; font-size: 0.8rem; color: #1e293b; text-transform: uppercase; letter-spacing: 0.05em; display: block; } /* display:block ensures it anchors properly */
.upload-card .section-label { margin-bottom: 16px; } /* Added since we dropped .card-header wrapper on this one */

.controls-grid { display: grid; grid-template-columns: 1fr 1.5fr 1fr 1fr; gap: 16px; align-items: flex-end; }
.upload-row { display: grid; grid-template-columns: 1.2fr 1fr 1fr; gap: 16px; align-items: flex-end; } /* REDUCED GAP */

.control-group { display: flex; flex-direction: column; gap: 8px; }
label { font-size: 0.75rem; font-weight: 700; color: #64748b; }

select, input[type="text"] {
  padding: 10px; border-radius: 6px; border: 1px solid var(--edge); background: #f8fafc; font-size: 0.9rem;
}

.step-input {
  display: flex; align-items: center; background: #f1f5f9; border-radius: 8px; overflow: hidden; height: 42px;
  border: 1px solid var(--edge);
}
.step-input button {
  width: 40px; height: 100%; border: none; background: #e2e8f0; color: #475569; font-size: 1.2rem; cursor: pointer;
}
.step-input button:hover:not(:disabled) { background: #cbd5e1; }
.divider-value {
  flex: 1; display: flex; flex-direction: column; align-items: center; justify-content: center; line-height: 1;
}

/* --- SEGMENTED CONTROL --- */
.segmented-control { display: flex; background: var(--edge); border-radius: 8px; padding: 2px; width: 100%; }
.segmented-control button { flex: 1; border: none; background: transparent; padding: 0; font-size: 0.8rem; color: #64748b; border-radius: 6px; cursor: pointer; transition: 0.2s; font-weight: 700; height: 100%; }
.segmented-control button.active { background: white; color: var(--ink); box-shadow: 0 1px 3px rgba(0,0,0,0.1); }

.divider-value span { font-weight: 700; font-family: 'Fira Code'; font-size: 0.9rem; }
.divider-value small { font-size: 0.65rem; color: #94a3b8; }

.w-full { width: 100%; }

button { border-radius: 8px; font-weight: 600; cursor: pointer; transition: 0.2s; height: 42px; border: 1px solid var(--edge); font-family: inherit;}
.btn-primary { background: var(--ink); color: white; border: none; }
.btn-primary:hover:not(:disabled) { background: #0f172a; }
.btn-secondary { background: #f1f5f9; color: #475569; }
.btn-secondary:hover:not(:disabled) { background: #e2e8f0; }
.btn-warning { background: #f59e0b; color: white; border: none; }
.btn-warning:hover:not(:disabled) { background: #d97706; }

.file-label { 
  display: block; padding: 10px; background: #f8fafc; border: 2px dashed #cbd5e1; border-radius: 8px;
  text-align: center; font-size: 0.85rem; height: 42px; line-height: 20px; color: #64748b; cursor: pointer; transition: 0.2s;
}
.file-label:hover:not(.disabled) { background: #f1f5f9; border-color: #94a3b8; }
.file-label.disabled { opacity: 0.5; cursor: not-allowed; border-style: solid; }


/* --- NEW: Advanced System Management Grid --- */
.system-card { background: #f8fafc; border-style: dashed; }

.advanced-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 20px; /* REDUCED GAP */
}

.advanced-group {
  background: white;
  padding: 12px 16px; /* REDUCED PADDING */
  border-radius: 8px;
  border: 1px solid var(--edge);
  display: flex;
  flex-direction: column;
}

.group-title {
  font-size: 0.75rem;
  font-weight: 800;
  color: #64748b;
  text-transform: uppercase;
  margin-bottom: 12px; /* REDUCED MARGIN */
  border-bottom: 1px solid var(--edge);
  padding-bottom: 8px;
  letter-spacing: 0.05em;
}

.group-buttons {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.btn-danger-outline { background: white; color: var(--error); border-color: #fecaca; }
.btn-danger-outline:hover:not(:disabled) { background: #fef2f2; border-color: #fca5a5; }
.icon { margin-right: 6px; font-size: 0.9rem; }

/* REUSABLE MODAL CSS */
.custom-modal-overlay {
  position: fixed; top: 0; left: 0; width: 100vw; height: 100vh;
  background: rgba(15, 23, 42, 0.7); backdrop-filter: blur(4px);
  display: flex; align-items: center; justify-content: center; z-index: 10000;
}
.custom-modal-card {
  background: var(--paper);
  width: 480px; border-radius: 12px;
  border: 1px solid var(--edge); 
  box-shadow: 0 10px 25px rgba(0,0,0,0.2); 
  animation: slideIn 0.2s ease-out;
}

.ota-modal-card {
  width: 650px;
}

.modal-header { padding: 16px 20px; font-weight: 800; font-size: 1.05rem; border-bottom: 1px solid var(--edge); }
.info-header { background: #eff6ff; color: var(--info); border-bottom-color: #bfdbfe; }
.warning-header { background: #fffbeb; color: #d97706; border-bottom-color: #fde68a; }
.error-header { background: #fef2f2; color: var(--error); border-bottom-color: #fecaca; }

.modal-body { padding: 24px 20px; font-size: 0.9rem; color: var(--ink); line-height: 1.5; }
.modal-body p { margin: 0; }
.modal-body code { background: #f1f5f9; padding: 2px 6px; border-radius: 4px; font-family: monospace; font-size: 0.8rem;}
.modal-actions { display: flex; gap: 12px; padding: 16px 20px; background: var(--ghost); border-top: 1px solid var(--edge); justify-content: flex-end; }

.btn-text { background: transparent; border: none; color: #64748b; font-weight: 600; width: auto; padding: 0 16px; }
.btn-text:hover { color: var(--ink); background: #f1f5f9; }
.btn-danger { background: var(--error); color: white; border: none; width: auto; padding: 0 20px; }
.btn-danger:hover { background: #dc2626; }

.mini-terminal {
  background: #0f172a;
  border-radius: 6px;
  padding: 10px 12px;
  margin-top: 20px;
  font-family: 'Fira Code', monospace;
  font-size: 0.75rem;
  height: 130px; 
  display: flex;
  flex-direction: column;
  justify-content: flex-end; 
  border: 1px solid var(--edge);
  overflow: hidden;
  box-shadow: inset 0 2px 8px rgba(0,0,0,0.2);
}

.mini-log-line {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  line-height: 1.5;
  color: #e2e8f0;
  transition: opacity 0.3s ease;
}

.text-success { color: var(--success) !important; font-weight: 700; }
.text-error { color: var(--error) !important; font-weight: 700; }

@keyframes slideIn {
  from { transform: translateY(20px); opacity: 0; }
  to { transform: translateY(0); opacity: 1; }
}
</style>