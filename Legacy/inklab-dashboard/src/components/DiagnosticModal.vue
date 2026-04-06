<script setup>
import { ref, watch, nextTick, onMounted, onUnmounted } from 'vue'
import { useSystemStore } from '../stores/systemStore'
import { useSerial } from '../composables/useSerial'

const store = useSystemStore()
const { sendCommand, startUpload } = useSerial() 

const isRunning = ref(false)
const currentPhase = ref('Ready')
const promptMessage = ref('Click "Start Diagnostics" to begin the hardware verification suite.')
const requiresUserAction = ref(false)
const userResponse = ref(null)
const testResults = ref([])

const resultsListRef = ref(null)

const delay = (ms) => new Promise(res => setTimeout(res, ms))

const addResult = (name, pass, detail) => {
  testResults.value.push({ name, pass, detail })
  store.addLog(`DIAG:[${pass ? 'PASS' : 'FAIL'}] ${name} - ${detail}`, pass ? 'var(--success)' : 'var(--error)')
  
  nextTick(() => {
    if (resultsListRef.value) {
      resultsListRef.value.scrollTop = resultsListRef.value.scrollHeight
    }
  })
}

const checkRange = (name, val, min, max, unit) => {
  const v = parseFloat(val)
  const pass = (v >= min && v <= max)
  addResult(name, pass, `${v.toFixed(2)} ${unit} (Expected: ${min}-${max})`)
  return pass
}

const waitForUser = async (msg) => {
  promptMessage.value = msg
  requiresUserAction.value = true
  userResponse.value = null
  store.addLog(`DIAG PROMPT: ${msg}`, 'var(--warning)')
  
  return new Promise((resolve) => {
    const unwatch = watch(userResponse, (val) => {
      if (val !== null) {
        requiresUserAction.value = false
        unwatch()
        resolve(val)
      }
    })
  })
}

const runSDSequence = () => {
  return new Promise(async (resolve) => {
    store.sdData = { status: 'RUNNING', capacity: '--', free: '--', health: '--', fatWrite: '--', fatRead: '--', usbUplink: '--', usbDownlink: '--' }
    
    await sendCommand('SD_TEST')

    const unwatch = watch(() => store.diagSequence.count, async () => {
      const lastLog = store.diagSequence.text.trim()

      if (lastLog === '--- DIAGNOSTICS END ---' && store.sdData.status === 'RUNNING') {
        setTimeout(async () => await sendCommand('RAW_TEST'), 250)
      } 
      else if (lastLog === '=================' && store.sdData.status === 'RUNNING') {
        setTimeout(async () => await sendCommand('READ_TEST'), 250)
      } 
      else if (lastLog === '==============================' && store.sdData.status === 'RUNNING') {
        store.sdData.status = 'DOWNLINKING'
        const dummyFile = new Uint8Array(1024 * 1024).fill(0xAA)
        setTimeout(() => { startUpload(dummyFile, 1, 1, 'Diag_Test') }, 250)
      } 
      else if (lastLog === 'Transfer complete.' && store.sdData.status === 'DOWNLINKING') {
        store.sdData.usbDownlink = store.transferSpeed 
        store.sdData.status = 'DONE'
        unwatch()
        resolve()
      }
    })
  })
}

const startDiagnostics = async () => {
  if (!store.isConnected) {
    store.addLog("ERR: Connect hardware first.", "var(--error)")
    return
  }
  
  isRunning.value = true
  testResults.value =[]
  store.addLog(">>> Initiating Full System Diagnostic Factory Test...", "var(--info)")

  // PHASE 1: Storage
  currentPhase.value = 'Datapath & SD Card'
  promptMessage.value = 'Running automated storage benchmarks...'
  await runSDSequence()

  checkRange('SD Raw Write', store.sdData.fatWrite, 680, 700, 'KB/s')
  checkRange('SD Raw Read', store.sdData.fatRead, 800, 1090, 'KB/s')
  checkRange('USB Uplink', store.sdData.usbUplink, 350, 400, 'KB/s')
  checkRange('USB Downlink', store.sdData.usbDownlink, 200, 400, 'KB/s')

  // PHASE 2: Joystick
  currentPhase.value = 'Interactive IO: Joystick'
  const joySteps =[
    { name: 'UP', key: 'w' }, { name: 'DOWN', key: 's' },
    { name: 'LEFT', key: 'a' }, { name: 'RIGHT', key: 'd' }, { name: 'CENTER (Press)', key: 'c' }
  ]

  for (const dir of joySteps) {
    promptMessage.value = `Please push the joystick: ${dir.name}`
    store.addLog(`DIAG PROMPT: Push joystick ${dir.name}`, 'var(--warning)')
    
    await new Promise((resolve) => {
      const unwatch = watch(() => store.ioData, (io) => {
        if (io[dir.key] === 0) {
          unwatch()
          resolve()
        }
      }, { deep: true })
    })
    addResult(`Joystick ${dir.name}`, true, 'Registered')
  }

  // PHASE 3: LED Sweep
  currentPhase.value = 'Interactive IO: Indicators'

  // NEW: Save the user's previous LED states
  const prevR = store.ledState.r
  const prevG = store.ledState.g
  const prevB = store.ledState.b
  const prevMuted = store.ledState.muted

  store.ledState.muted = false
  await sendCommand('LED:MUTE:0')
  await sendCommand('LED:OVERRIDE:1') 

  const colors =[
    { name: 'RED', cmd: 'LED:PWM:VAL:0:0' },
    { name: 'GREEN', cmd: 'LED:PWM:0:VAL:0' },
    { name: 'BLUE', cmd: 'LED:PWM:0:0:VAL' }
  ]

  for (const c of colors) {
    promptMessage.value = `Sweeping ${c.name} LED... Look at the board.`
    await delay(1000)
    store.addLog(`DIAG: Sweeping ${c.name} LED`, 'var(--info)')
    
    for (let i = 0; i <= 100; i += 5) {
      await sendCommand(c.cmd.replace('VAL', i))
      await delay(200)
    }
    await sendCommand('LED:PWM:0:0:0')
    
    const passed = await waitForUser(`Did the ${c.name} LED smoothly sweep from off to full brightness?`)
    addResult(`${c.name} LED Sweep`, passed, passed ? 'Verified by User' : 'Failed User Verification')
  }

  // NEW: Restore the user's previous LED states!
  await sendCommand('LED:OVERRIDE:0') 
  await sendCommand(`LED:PWM:${prevR}:${prevG}:${prevB}`)
  await sendCommand(`LED:MUTE:${prevMuted ? 1 : 0}`)
  store.ledState.muted = prevMuted

  // PHASE 4: Power Rails
  currentPhase.value = 'Power Rails Verification'
  promptMessage.value = 'Checking active power rails against limits...'
  await delay(500)

  checkRange('1.2V Core Rail', store.powerData['1v2_core'].v, 1.15, 1.25, 'V')
  checkRange('3.3V FPGA Rail', store.powerData['3v3_fpga'].v, 3.20, 3.40, 'V')
  checkRange('3.3V MCU Rail', store.powerData['3v3_mcu'].v, 3.20, 3.40, 'V')
  checkRange('3.3V EXT Rail', store.powerData['3v3_ext'].v, 3.20, 3.40, 'V')

  // Finish
  currentPhase.value = 'Diagnostics Complete'
  promptMessage.value = 'All checks finished. You may close this window.'
  
  // NEW: Clear the console at the end to leave a clean slate
  store.logs =[] 
  store.addLog(">>> Diagnostic Sequence Complete.", "var(--success)")
  
  isRunning.value = false
}

// Close Modal Logic (Shared by button, backdrop, and Esc key)
const closeModal = () => {
  if (!isRunning.value) store.showDiagnosticModal = false;
}

// Handle Escape Key
const handleKeydown = (e) => {
  if (e.key === 'Escape') {
    closeModal()
  }
}

// Attach/Detach Event Listener for Escape Key
onMounted(() => {
  window.addEventListener('keydown', handleKeydown)
})

onUnmounted(() => {
  window.removeEventListener('keydown', handleKeydown)
})
</script>

<template>
  <!-- NEW: @click.self ensures clicking the dark background (not the modal itself) closes it -->
  <div class="modal-overlay" @click.self="closeModal">
    <div class="diag-modal">
      
      <!-- Sticky Header -->
      <div class="modal-header">
        <div class="header-title">🩺 Full System Diagnostics</div>
        <button class="close-btn" @click="closeModal" :disabled="isRunning">×</button>
      </div>
      
      <!-- 2-Column Body -->
      <div class="modal-body">
        
        <!-- Left Column: Prompts & Controls -->
        <div class="interactive-panel">
          <div class="phase-badge">{{ currentPhase }}</div>
          
          <div class="prompt-box" :class="{ 'needs-action': requiresUserAction, 'active': isRunning }">
            {{ promptMessage }}
          </div>

          <div v-if="requiresUserAction" class="action-buttons">
            <button class="btn-pass" @click="userResponse = true">YES (Pass)</button>
            <button class="btn-fail" @click="userResponse = false">NO (Fail)</button>
          </div>
          
          <button v-if="!isRunning && !requiresUserAction" class="start-btn" @click="startDiagnostics" :disabled="!store.isConnected">
            Start Diagnostics
          </button>
        </div>

        <!-- Right Column: Scrolling Results List -->
        <div class="results-panel">
          <div class="results-header">Verification Results</div>
          
          <div class="results-list" ref="resultsListRef">
            <div v-if="testResults.length === 0" class="empty-results">Waiting for test data...</div>
            
            <div class="result-row" v-for="(res, idx) in testResults" :key="idx">
              <span class="res-name">{{ res.name }}</span>
              <span class="res-detail">{{ res.detail }}</span>
              <span class="res-badge" :class="res.pass ? 'pass' : 'fail'">
                {{ res.pass ? 'PASS' : 'FAIL' }}
              </span>
            </div>
          </div>
        </div>

      </div>
    </div>
  </div>
</template>

<style scoped>
.modal-overlay {
  position: fixed; top: 0; left: 0; width: 100vw; height: 100vh;
  background: rgba(15, 23, 42, 0.8); backdrop-filter: blur(4px);
  display: flex; align-items: center; justify-content: center; z-index: 9999;
}

.diag-modal {
  background: var(--paper);
  width: 900px;
  max-width: 95vw;
  height: 80vh; 
  max-height: 800px;
  border-radius: 12px;
  border: 1px solid var(--edge);
  box-shadow: 0 10px 40px rgba(0,0,0,0.3);
  display: flex; 
  flex-direction: column;
  overflow: hidden;
}

/* --- FIXED HEADER STYLES --- */
.modal-header {
  background: #f8fafc;
  padding: 16px 24px;
  border-bottom: 1px solid var(--edge);
  display: flex; 
  justify-content: space-between; 
  align-items: center;
  flex-shrink: 0;
  width: 100%;             /* Force full width */
  box-sizing: border-box;  /* Keep padding contained inside 100% */
}

.header-title {
  font-weight: 800; 
  font-size: 1.1rem; 
  color: var(--ink);
  white-space: nowrap;     /* Forbid wrapping to second line */
}

.close-btn {
  background: transparent; 
  border: none; 
  font-size: 1.6rem; 
  color: #94a3b8; 
  cursor: pointer; 
  width: 32px;            /* Fix button size as a square */
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 6px;
  transition: 0.2s;
}
.close-btn:hover:not(:disabled) { background: #f1f5f9; color: var(--error); }
.close-btn:disabled { opacity: 0.3; cursor: not-allowed; }

/* --- BODY STYLES --- */
.modal-body {
  padding: 24px;
  display: grid; 
  grid-template-columns: 1fr 1.3fr;
  gap: 24px;
  flex-grow: 1;
  min-height: 0; 
  box-sizing: border-box;
}

.interactive-panel { display: flex; flex-direction: column; gap: 16px; align-items: center; }

.phase-badge {
  background: var(--ink); color: white; padding: 6px 16px; border-radius: 20px; 
  font-size: 0.75rem; font-weight: 800; text-transform: uppercase; letter-spacing: 0.05em;
}

.prompt-box { 
  background: white; border: 2px dashed var(--edge); padding: 32px 20px; 
  text-align: center; border-radius: 8px; font-weight: 700; color: #64748b; 
  font-size: 1.05rem; transition: 0.3s; width: 100%; line-height: 1.5;
}
.prompt-box.active { border-color: var(--info); color: var(--ink); background: #eff6ff; border-style: solid; }
.prompt-box.needs-action { border-color: var(--warning); background: #fffbeb; color: #d97706; border-style: solid; }

.action-buttons { display: flex; gap: 16px; width: 100%; }
.action-buttons button { flex: 1; padding: 16px; font-weight: 800; font-size: 1rem; border: none; border-radius: 8px; cursor: pointer; color: white; transition: 0.2s; box-shadow: 0 4px 6px rgba(0,0,0,0.1);}
.btn-pass { background: var(--success); }
.btn-pass:hover { background: #059669; transform: translateY(-2px); }
.btn-fail { background: var(--error); }
.btn-fail:hover { background: #dc2626; transform: translateY(-2px); }

.start-btn {
  width: 100%; padding: 16px; background: var(--info); color: white; border: none; 
  border-radius: 8px; font-weight: 800; font-size: 1rem; cursor: pointer; transition: 0.2s;
  margin-top: auto;
}
.start-btn:hover:not(:disabled) { background: #2563eb; }
.start-btn:disabled { background: var(--edge); color: #94a3b8; cursor: not-allowed; }

.results-panel {
  display: flex; flex-direction: column;
  background: var(--ghost); border: 1px solid var(--edge); border-radius: 8px;
  overflow: hidden; 
}

.results-header {
  padding: 12px 16px; background: #f1f5f9; border-bottom: 1px solid var(--edge);
  font-weight: 800; font-size: 0.8rem; color: var(--accent); text-transform: uppercase; flex-shrink: 0;
}

.results-list {
  padding: 16px; display: flex; flex-direction: column; gap: 8px;
  overflow-y: auto; flex-grow: 1; scrollbar-width: thin;
}

.empty-results { text-align: center; color: #94a3b8; font-size: 0.9rem; font-weight: 600; margin-top: 40px;}

.result-row { 
  display: flex; justify-content: space-between; align-items: center; 
  background: white; padding: 12px 16px; border: 1px solid var(--edge); border-radius: 6px; 
}
.res-name { font-weight: 700; font-size: 0.85rem; color: var(--ink); width: 140px; }
.res-detail { font-family: 'Fira Code', monospace; font-size: 0.8rem; color: #64748b; flex-grow: 1; text-align: center; }
.res-badge { font-weight: 800; font-size: 0.75rem; padding: 4px 12px; border-radius: 6px; }
.res-badge.pass { background: #dcfce7; color: #166534; }
.res-badge.fail { background: #fee2e2; color: #991b1b; }
</style>