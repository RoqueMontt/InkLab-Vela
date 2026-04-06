<script setup>
import { ref } from 'vue'
import { useSystemStore } from '../../stores/systemStore'
import { useSerial } from '../../composables/useSerial'

const store = useSystemStore()
const { sendCommand, startUpload } = useSerial()

const mcuTime = ref(0)
const jsTime = ref(0)
const macroTime = ref(0)
const isRunning = ref(false)
const isSweeping = ref(false) // <-- Add this line right here

const ITERATIONS = 10;
const workload = [
  'SPI:PING',
  'ADC:READ',
  'UART:TX:B',
  'EXT:GPIO:PB0:1',
  'EXT:GPIO:PB0:0'
];

const runMcuNativeAsync = () => {
  return new Promise((resolve) => {
    const handler = (e) => { window.removeEventListener('bench-mcu-done', handler); resolve(e.detail) }
    window.addEventListener('bench-mcu-done', handler)
    sendCommand('BENCHMARK:NATIVE')
    setTimeout(() => { window.removeEventListener('bench-mcu-done', handler); resolve("TIMEOUT") }, 2000)
  })
}

const runComparison = async () => {
  if (!store.isConnected) { store.addNotification("Connect hardware first!", "error"); return }
  isRunning.value = true; mcuTime.value = 0; jsTime.value = 0; macroTime.value = 0;

  store.addLog(">>> Starting Mixed-Signal Benchmark", "var(--info)")

  // ----------------------------------------------------------------
  // 1. JAVASCRIPT USB BENCHMARK
  // ----------------------------------------------------------------
  store.addLog(`Running Phase 1: Browser JS over USB (${ITERATIONS * workload.length} ops)...`, "var(--warning)")
  
  const startJs = performance.now()
  const jsPromise = new Promise((resolve) => {
    const handler = () => { window.removeEventListener('bench-js-done', handler); resolve(performance.now() - startJs) }
    window.addEventListener('bench-js-done', handler)
  })

  // Blast the mixed workload into the USB queue
  for (let i = 0; i < ITERATIONS; i++) {
    for (const cmd of workload) {
      await sendCommand(cmd)
    }
  }
  await sendCommand('BENCHMARK:JS_END') // Mark the end of the queue
  
  // Race against a timeout to prevent UI freezes
  jsTime.value = await Promise.race([jsPromise, new Promise(r => setTimeout(() => r("TIMEOUT"), 3000))])

  // ----------------------------------------------------------------
  // 2. SD CARD MACRO BENCHMARK (The "Virtual Computer")
  // ----------------------------------------------------------------
  store.addLog("Running Phase 2: Uploading Macro to SD Card...", "var(--info)")
  
  let macroText = "# Mixed-Signal Validation Script\n"
  for (let i = 0; i < ITERATIONS; i++) {
    macroText += workload.join('\n') + '\n'
  }
  
  const encoder = new TextEncoder()
  const macroBytes = encoder.encode(macroText)
  
  await startUpload(macroBytes, 15, 2, "bench_script")
  await new Promise(r => setTimeout(r, 1000)) // Wait for file closure
  
store.addLog("Executing SD Macro Engine...", "var(--warning)")
  
  // REPLACED: Use exact same bulletproof listener as the other phases
  const macroPromise = new Promise((resolve) => {
    const handler = (e) => { 
      window.removeEventListener('bench-macro-done', handler); 
      resolve(e.detail === 0 ? "< 1" : e.detail);
    }
    window.addEventListener('bench-macro-done', handler)
  })
  
  sendCommand('MACRO:RUN:slot15.bin')
  
  macroTime.value = await Promise.race([macroPromise, new Promise(r => setTimeout(() => r("TIMEOUT"), 3000))])

  // ----------------------------------------------------------------
  // 3. NATIVE C BENCHMARK
  // ----------------------------------------------------------------
  store.addLog("Running Phase 3: Native C (Edge)...", "var(--warning)")
  let cTime = await runMcuNativeAsync()
  mcuTime.value = cTime === 0 ? "< 1" : cTime

  store.addLog(">>> Architecture Benchmark Complete.", "var(--success)")
  isRunning.value = false
}

// --- UPDATED FAST TELEMETRY GATHERER ---
const gatherFastAverage = (numSamples) => {
  return new Promise((resolve) => {
    let count = 0;
    let sumCoreI = 0, sumCoreV = 0;
    let sumFpgaI = 0, sumFpgaV = 0;
    
    const handler = (e) => {
      const data = e.detail;
      // data["1"] is Core INA [V, mA], data["2"] is FPGA INA [V, mA]
      if (data["1"] && data["2"]) {
        sumCoreV += data["1"][0];
        sumCoreI += data["1"][1];
        sumFpgaV += data["2"][0];
        sumFpgaI += data["2"][1];
        count++;
        
        if (count >= numSamples) {
          window.removeEventListener('fast-telemetry', handler);
          resolve({ 
            core_v: sumCoreV / count, core_i: sumCoreI / count, 
            fpga_v: sumFpgaV / count, fpga_i: sumFpgaI / count 
          });
        }
      }
    };
    
    window.addEventListener('fast-telemetry', handler);
    
    // Safety timeout in case stream drops
    setTimeout(() => {
      window.removeEventListener('fast-telemetry', handler);
      resolve({ 
        core_v: sumCoreV / (count || 1), core_i: sumCoreI / (count || 1), 
        fpga_v: sumFpgaV / (count || 1), fpga_i: sumFpgaI / (count || 1) 
      });
    }, 2000);
  });
}

// --- HELPER TO DOWNLOAD CSV ---
const downloadCsv = (content, filename) => {
  const blob = new Blob([content], { type: 'text/csv' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  a.click()
  URL.revokeObjectURL(url)
}

// --- NEW SWEEP METHODOLOGY ---
const runCapacitanceSweep = async () => {
  if (!store.isConnected) { store.addNotification("Connect hardware first!", "error"); return }
  isSweeping.value = true
  store.addLog(">>> Starting Automated Power-Capacitance Matrix Sweep...", "var(--info)")

  // 1. Enable Fast Mode
  await sendCommand('TELEM:FAST:1:1v2_core')
  await sendCommand('TELEM:FAST:2:3v3_fpga')
  await new Promise(r => setTimeout(r, 200)) // Let stream start

  // 2. Safely figure out the current slot so SETCLK applies immediately
  let activeSlotInt = 1;
  const slotMatch = store.activeSlot.match(/\((\d+)\)/);
  if (slotMatch) activeSlotInt = parseInt(slotMatch[1], 10);
  else if (store.activeSlot !== '--') activeSlotInt = parseInt(store.activeSlot, 10) || 1;

  let rawCsv = "Frequency_MHz,Active_Pins,1.2V_Core_mA,3.3V_FPGA_mA\n"
  let capCsv = "Frequency_MHz,Pin_Index,Delta_I_mA,Voltage_V,Calculated_Capacitance_pF\n"
  const dividers = [128, 64, 32, 16, 8, 4, 2] // 0.5MHz to 32MHz

  for (let div of dividers) {
    const freq_MHz = 64 / div
    const toggleFreq_MHz = freq_MHz / 2; // Pin toggle frequency is half the MCU clock
    store.addLog(`Sweeping Frequency: ${freq_MHz} MHz`, "var(--warning)")
    
  // Turn off previous sweep's pins first
  await sendCommand(`SPI:GPIO_DIR:0`)
  await sendCommand(`SPI:GPIO_WR:0`)
  await new Promise(r => setTimeout(r, 100)) 

  // Now it is safe to change the clock without power spikes
  await sendCommand(`SETCLK:${activeSlotInt}:${div}`)
  await new Promise(r => setTimeout(r, 400))

    // Reset mask to zero for baseline Quiescent Current
    let currentMask = 0;
    await sendCommand(`SPI:GPIO_DIR:0`)
    await sendCommand(`SPI:GPIO_WR:0`)
    await new Promise(r => setTimeout(r, 100)) // Let power stabilize

    // Capture Baseline
    const quiescentAvg = await gatherFastAverage(50); // Take 50 samples for solid baseline
    let prevFpgaCurrent = quiescentAvg.fpga_i;
    let currentVoltage = quiescentAvg.fpga_v;

    rawCsv += `${freq_MHz},0,${quiescentAvg.core_i.toFixed(3)},${quiescentAvg.fpga_i.toFixed(3)}\n`

    // Cumulatively turn on one pin at a time
    for (let pin = 0; pin <= 30; pin++) {
      currentMask |= (1 << pin);

      // JS bitwise operations become signed at 32-bits, so we split it safely
      const maskLower = currentMask & 0xFFFF;
      const maskUpper = (currentMask >>> 16) & 0xFFFF;

      await sendCommand(`SPI:GPIO_DIR:${maskLower.toString(16)}`)
      await sendCommand(`SPI:GPIO_WR:${maskUpper.toString(16)}`)

      // Wait 50ms for hardware capacitance to physically settle
      await new Promise(r => setTimeout(r, 50))

      // Gather exactly 20 samples & average them
      const avg = await gatherFastAverage(20)
      rawCsv += `${freq_MHz},${pin + 1},${avg.core_i.toFixed(3)},${avg.fpga_i.toFixed(3)}\n`

      // --- Capacitance Calculation ---
      // C = I / (V * f). To get pF from mA, V, and MHz: (mA * 1000) / (V * MHz)
      const delta_I = avg.fpga_i - prevFpgaCurrent;
      prevFpgaCurrent = avg.fpga_i; // Update for the next pin

      let capacitance_pF = 0;
      if (delta_I > 0 && currentVoltage > 0) {
        capacitance_pF = (delta_I * 1000) / (currentVoltage * toggleFreq_MHz);
      }

      capCsv += `${freq_MHz},${pin},${delta_I.toFixed(4)},${currentVoltage.toFixed(3)},${capacitance_pF.toFixed(2)}\n`
    }
  }

  // Cleanup & Return to normal
  await sendCommand(`SPI:GPIO_DIR:0`)
  await sendCommand(`SPI:GPIO_WR:0`)
  await sendCommand('TELEM:FAST:1:OFF')
  await sendCommand('TELEM:FAST:2:OFF')

  store.addLog(">>> Sweep Complete! Generating CSV Data...", "var(--success)")

  downloadCsv(rawCsv, `Raw_Sweep_${new Date().getTime()}.csv`)
  downloadCsv(capCsv, `Pin_Capacitance_${new Date().getTime()}.csv`)

  isSweeping.value = false
}
</script>

<template>
  <div class="automation-page">
    <div class="header-banner">
      <span class="section-label" style="border: none; margin: 0; padding: 0;">Mixed-Signal Latency Benchmark</span>
    </div>

    <div class="split-layout">
      <div class="left-panel">
        <div class="info-card">
          <h3>The "Hardware-in-the-Loop" Test</h3>
          <p>We are executing a sequence of 5 mixed-signal operations (SPI Ping, ADC Read, UART Transmit, and 2x GPIO states) repeated 10 times across three computing architectures.</p>
          <ul>
            <li><b>Browser JS:</b> Text parsed packet-by-packet over USB CDC.</li>
            <li><b>SD Macro Interpreter:</b> Text read from local SD Card and parsed dynamically.</li>
            <li><b>Native C:</b> Direct register/HAL calls compiled into machine code.</li>
          </ul>
        </div>

        <button class="btn-primary start-btn" @click="runComparison" :disabled="isRunning || !store.isConnected">
          {{ isRunning ? 'Running Benchmark Sequence...' : '▶ RUN ARCHITECTURE BENCHMARK' }}
        </button>

        <div class="results-card" v-if="jsTime > 0">
          <div class="bar-chart">
            <div class="bar-row">
              <span class="label">Browser JS (USB)</span>
              <div class="bar js-bar" style="width: 100%">{{ typeof jsTime === 'number' ? jsTime.toFixed(1) : jsTime }} ms</div>
            </div>
            
            <div class="bar-row">
              <span class="label">SD Card Interpreter (VM)</span>
              <div class="bar macro-bar" :style="{ width: Math.max((macroTime / jsTime) * 100, 2) + '%' }">{{ macroTime }} ms</div>
            </div>

            <div class="bar-row">
              <span class="label">Native STM32 (C)</span>
              <div class="bar mcu-bar" :style="{ width: Math.max((mcuTime / jsTime) * 100, 1) + '%' }">{{ mcuTime }} ms</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>

  <button class="btn-primary start-btn" @click="runCapacitanceSweep" :disabled="isSweeping || isRunning || !store.isConnected" style="margin-top: 10px; background: var(--info);">
    {{ isSweeping ? 'Sweeping Power Matrix...' : '📊 RUN CAPACITANCE MATRIX SWEEP (CSV)' }}
</button>
</template>

<style scoped>
.automation-page { display: flex; flex-direction: column; gap: 20px; height: 100%; padding: 20px; }
.header-banner { border-bottom: 1px solid var(--edge); padding-bottom: 12px; }
.section-label { font-weight: 800; font-size: 0.9rem; color: var(--ink); text-transform: uppercase; letter-spacing: 0.05em; }
.split-layout { display: grid; grid-template-columns: 1fr; gap: 24px; max-width: 800px; }
.left-panel { display: flex; flex-direction: column; gap: 16px; }

.info-card, .results-card { background: white; border: 1px solid var(--edge); border-radius: 8px; padding: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.02); }
.info-card h3 { margin-top: 0; font-size: 1rem; color: var(--info); margin-bottom: 12px;}
.info-card p, .info-card li { font-size: 0.85rem; color: #475569; line-height: 1.5; }

.start-btn { padding: 16px; font-size: 0.9rem; font-weight: 800; border-radius: 8px; border: none; background: var(--ink); color: white; cursor: pointer; transition: 0.2s;}
.start-btn:hover:not(:disabled) { background: #334155; }

.bar-chart { display: flex; flex-direction: column; gap: 16px; }
.bar-row { display: flex; flex-direction: column; gap: 4px; }
.bar-row .label { font-size: 0.75rem; font-weight: 800; color: #64748b; text-transform: uppercase; }
.bar { height: 28px; border-radius: 4px; display: flex; align-items: center; padding: 0 12px; font-family: 'Fira Code', monospace; font-size: 0.8rem; font-weight: 700; color: white; transition: width 0.5s;}
.js-bar { background: var(--error); }
.macro-bar { background: #f59e0b; min-width: fit-content;}
.mcu-bar { background: var(--success); min-width: fit-content; }
</style>