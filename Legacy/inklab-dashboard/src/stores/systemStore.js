import { defineStore } from 'pinia'

// Helper to decode BQ Faults from REG20 and REG21
function decodeBQFaults(f0, f1) {
  const faults = []
  if (f0 & (1 << 7)) faults.push('IBAT Regulation Mode')
  if (f0 & (1 << 6)) faults.push('VBUS Over-Voltage')
  if (f0 & (1 << 5)) faults.push('VBAT Over-Voltage')
  if (f0 & (1 << 4)) faults.push('IBUS Over-Current')
  if (f0 & (1 << 3)) faults.push('IBAT Over-Current')
  if (f0 & (1 << 2)) faults.push('Converter Over-Current')
  if (f0 & (1 << 1)) faults.push('VAC2 Over-Voltage')
  if (f0 & (1 << 0)) faults.push('VAC1 Over-Voltage')
  if (f1 & (1 << 7)) faults.push('SYS Short Circuit')
  if (f1 & (1 << 6)) faults.push('SYS Over-Voltage')
  if (f1 & (1 << 5)) faults.push('OTG Over-Voltage')
  if (f1 & (1 << 4)) faults.push('OTG Under-Voltage')
  if (f1 & (1 << 2)) faults.push('Thermal Shutdown')
  return faults
}

const ERROR_CODES = {
  0: "ERR_NONE",
  1: "ERR_SD_MOUNT_FAIL",
  2: "ERR_SD_FILE_NOT_FOUND",
  3: "ERR_FPGA_PROGRAM_TIMEOUT",
  4: "ERR_FPGA_DONE_LOW",
  5: "ERR_I2C_BUS_STUCK",
  6: "ERR_BQ25798_FAULT",
  7: "ERR_SPI_DMA_TIMEOUT"
};

// Dictionary of handlers for incoming JSON telemetry. 
// Keys map to the "type" field emitted by the hardware MCU.
const telemetryHandlers = {
  'ina': (data, state) => {
    const rail = state.powerData[data.id]
    if (!rail) return
    const pwr = data.p || 0
    rail.v = data.v || 0
    rail.i = data.i || 0
    rail.pwr = pwr
    rail.ema = rail.ema === 0 ? pwr : (pwr * 0.7) + (rail.ema * 0.3)
    if (pwr > rail.peak) rail.peak = pwr
    if (pwr < rail.min) rail.min = pwr
  },
    'adc_res': (data, state) => {
    state.addLog(`ADC Measurement: ${data.v.toFixed(3)} V`, 'var(--warning)')
    window.dispatchEvent(new CustomEvent('adc-update', { detail: data.v }))
  },
'bq_dump': (data, state) => {
    state.bqData.rawRegs = data.regs;
    if (data.rearm !== undefined) state.bqData.autoRearm = data.rearm;
    if (data.ema !== undefined) state.bqData.ema = data.ema;
  },
  'bq': (data, state) => {
    state.bqData.vbusVolt = data.vbus
    state.bqData.vbusCurr = data.ibus
    state.bqData.vbusPwr = data.vbus_pwr
    state.bqData.batVolt = data.vbat
    state.bqData.batCurr = data.ibat
    state.bqData.batPwr = data.pwr
    state.bqData.mode = data.stat
    state.bqData.fault0 = data.flt0
    state.bqData.fault1 = data.flt1
    state.bqData.soc = data.soc || 0 

    // Extract faults and trigger notifications for NEW faults
    const newFaults = decodeBQFaults(data.flt0, data.flt1)
    newFaults.forEach(fault => {
      if (!state.activeFaults.includes(fault)) {
        state.addNotification(`Hardware Alert: ${fault}`, 'error')
      }
    })
    state.activeFaults = newFaults
  },
    'sys_err': (data, state) => {
    const severities = ['INFO', 'WARNING', 'CRITICAL', 'FATAL'];
    const sevStr = severities[data.sev] || 'UNKNOWN';
    const codeStr = ERROR_CODES[data.code] || `CODE_${data.code}`;
    
    // Determine Terminal Text Color
    const logColor = data.sev >= 2 ? 'var(--error)' : (data.sev === 1 ? 'var(--warning)' : 'var(--info)');
    
    // Determine Toast UI Type
    const toastType = data.sev >= 2 ? 'error' : (data.sev === 1 ? 'warning' : 'info');

    const fullMsg = `[${sevStr}] ${codeStr}: ${data.msg}`;
    
    // 1. Post to the terminal log history
    state.addLog(fullMsg, logColor);
    
    // 2. Pop a toast notification for anything WARNING or above
    if (data.sev >= 1) {
      state.addNotification(fullMsg, toastType);
    }
  },
  'io': (data, state) => {
    state.ioData.w = data.w; state.ioData.a = data.a; state.ioData.s = data.s; state.ioData.d = data.d; state.ioData.c = data.c
  },
  'io_stat': (data, state) => {
    state.ledState.r = data.r; state.ledState.g = data.g; state.ledState.b = data.b;
    state.ledState.muted = data.m === 1;
    state.joyMap.ud = data.ud; state.joyMap.lr = data.lr; state.joyMap.cen = data.cen;
  },
'spi': (data, state) => {
    state.spiData.lastCmd = '0x' + data.cmd.toString(16).toUpperCase().padStart(2, '0')
    state.spiData.payload0 = '0x' + data.p0.toString(16).toUpperCase().padStart(2, '0')
    state.spiData.payload1 = '0x' + data.p1.toString(16).toUpperCase().padStart(2, '0')
    state.spiData.rxCount++

    if (data.cmd === 0x07) {
      state.fpgaGpio.states = (data.p0 << 8) | data.p1;
    }
    if (data.cmd === 0x09) {
      state.ramExplorer.lastAddr = data.p0.toString(16).padStart(2, '0') + data.p1.toString(16).padStart(2, '0');
      state.ramExplorer.lastVal = data.p2.toString(16).toUpperCase().padStart(2, '0');
      state.ramExplorer.history.unshift({ addr: state.ramExplorer.lastAddr, val: state.ramExplorer.lastVal });
      if(state.ramExplorer.history.length > 10) state.ramExplorer.history.pop();
    }
    
    // ---> ADD THIS BLOCK TO FIX THE UART TERMINAL OUTPUT <---
    if (data.cmd === 0x0B) {
      if (data.p0 === 0) {
         state.addLog(`SPI FIFO Read: Empty (0x00)`, 'var(--warning)');
      } else {
         const charStr = String.fromCharCode(data.p0);
         state.addLog(`SPI FIFO Read: 0x${data.p0.toString(16).padStart(2,'0').toUpperCase()} (ASCII: '${charStr}')`, 'var(--success)');
      }
    }
    // ---------------------------------------------------------
  },
  'sys': (data, state) => {
    state.isSdInserted = data.sd === 1
    state.hasReceivedSysTelem = true
    if (!state.isSdInserted && !state.sdWarningDismissed) {
      state.showSdWarning = true
    } else if (state.isSdInserted) {
      state.showSdWarning = false
      state.sdWarningDismissed = false 
    }
  },
  'mcu_gpio': (data, state) => {
    // Copy the JSON data into our state, removing the "type" key
    const cleanData = { ...data };
    delete cleanData.type;
    state.mcuGpioInputs = cleanData;
  }
}

export const useSystemStore = defineStore('system', {
  state: () => ({
    appMode: 'CONFIG', // Default mode
    availableTabs: {
      CONFIG: [
        { id: 'FpgaConfig', label: 'Programmer' },
        { id: 'PowerDashboard', label: 'Power Profiler' },
        { id: 'JoystickView', label: 'Device IO' },
        { id: 'PinoutConfig', label: 'Pinout & Routing' } 
      ],
      DEBUG: [
        { id: 'SwTerminal', label: 'Terminal' },
        { id: 'SpiTester', label: 'SPI DMA Tester' },
        { id: 'AutomationBench', label: 'JS Automation' },
        { id: 'SignalGenerator', label: 'Signal Gen' }
      ],
      APP: [
        { id: 'AlgorithmDashboard', label: 'Analysis' },
        { id: 'PatientProfile', label: 'Patient' }
      ]
    },
    fwName: 'InkLab OS',
    fwVersion: 'v1.0',
    isConnected: false,
    isUploading: false,
    logs:[],
    diagSequence: { text: '', count: 0 },
    
    transferSpeed: '0.0 KB/s', uploadProgress: 0, activeSlot: '--', clockStatus: 'IDLE',
    
    sdData: { status: 'IDLE', capacity: '--', free: '--', health: '--', fatWrite: '--', fatRead: '--', usbUplink: '--', usbDownlink: '--' },
    
    powerData: {
      '1v2_core': { title: '1.2V Core', v: 0, i: 0, pwr: 0, ema: 0, min: 99999, peak: 0 },
      '3v3_ext':  { title: '3.3V Ext', v: 0, i: 0, pwr: 0, ema: 0, min: 99999, peak: 0 },
      '3v3_fpga': { title: '3.3V FPGA', v: 0, i: 0, pwr: 0, ema: 0, min: 99999, peak: 0 },
      '3v3_mcu':  { title: '3.3V MCU', v: 0, i: 0, pwr: 0, ema: 0, min: 99999, peak: 0 }
    },
    
    activeFaults: [],

    slotAliases: Array(16).fill('unconfigured'),
    isSdInserted: false,
    joyMap: { ud: 0, lr: 0, cen: 0 },
    ioData: { w: 1, a: 1, s: 1, d: 1, c: 1 },
    mcuGpioInputs: {},
    ledState: { r: 50, g: 100, b: 100, muted: false },
    fpgaLedState: { r: 25, g: 10, b: 10, muted: false }, // <-- ADD THIS
    spiData: { lastCmd: '--', payload0: '--', payload1: '--', rxCount: 0 },
    
    sidebarScrollState: 0, sidebarAutoScroll: true, termScrollState: 0, termAutoScroll: true,
    hasReceivedSysTelem: false, showSdWarning: false, sdWarningDismissed: false, sysMode: 'ACTIVE', 
    showDiagnosticModal: false, 
    bqData: { vbusVolt: 0, vbusCurr: 0, vbusPwr: 0, batVolt: 0, batCurr: 0, batPwr: 0, mode: '--', fault0: 0, fault1: 0, soc: 0, rawRegs:[], autoRearm: 1, ema: 0.2 },
    lastProgrammedSlot: '--',

    // Notifications State
    notifications: [],
    toastIdCounter: 0,
    fpgaGpio: {
      dir: 0x0000,   // 16-bit mask (1=Out, 0=In)
      states: 0x0000 // 16-bit mask (High/Low)
    },
    ramExplorer: {
      lastAddr: '0000',
      lastVal: '00',
      history: [] // Last 10 reads
    },
filterSettings: {
      ina: false, // Default: Hide them so you see the terminal clean
      bq: false,
      sys: false,
      spi: true,
      log: true,
      mcu_gpio: true
    }
  }),
  getters: {
    isSleeping: (state) => state.sysMode === 'STANDBY',
    
    // NEW: Dynamically grab the active array based on current mode
    currentTabsArray: (state) => {
      const tabs = state.availableTabs[state.appMode]
      return tabs ? tabs.map(tab => tab.id) : []
    }
  },
  actions: {
    clearLogs() {
      this.logs = []
      this.diagSequence = { text: '', count: 0 }
    },
addLog(msg, color = null, type = 'log') {
  // Check if we should ignore this based on filter settings
  if (this.filterSettings[type] === false) return; 

  const time = new Date().toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
  
  // CHANGE THIS LINE: Drop the limit from 3000 to 100
  if (this.logs.length >= 100) this.logs.shift();
  
  this.logs.push({ time, msg, color, type });
  this.diagSequence = { text: msg, count: this.diagSequence.count + 1 };
},
    
    // New global notification dispatcher
    addNotification(msg, type = 'info') {
      const id = this.toastIdCounter++
      this.notifications.push({ id, msg, type })
      setTimeout(() => {
        this.notifications = this.notifications.filter(n => n.id !== id)
      }, 6000)
    },

    resetPowerMetrics() {
      for (const key in this.powerData) {
        this.powerData[key].ema = 0; this.powerData[key].min = 99999; this.powerData[key].peak = 0
      }
    },

    processIncomingData(line) {
      if (line.startsWith('{')) {
        try {
          const data = JSON.parse(line)
          if (telemetryHandlers[data.type]) {
            telemetryHandlers[data.type](data, this)
            
            // --- NEW: Map raw hardware JSON types to your UI filter categories ---
            let filterCategory = data.type;
            if (data.type === 'bq_dump') filterCategory = 'bq';
            if (data.type === 'io_stat' || data.type === 'io') filterCategory = 'sys'; // or map to 'mcu_gpio'
            if (data.type === 'adc_res') filterCategory = 'log'; 
            // ---------------------------------------------------------------------

            // Now log it using the mapped category
            this.addLog(line, '#94a3b8', filterCategory);
          }
        } catch (e) { }
        return
      }
      if (line.startsWith('LAST_SLOT:')) {
        const slot = line.split(':')[1];
        this.lastProgrammedSlot = slot;
        if (slot !== 'NONE') {
          this.addLog(`Recovered last active slot from MCU SRAM: ${slot}`, 'var(--info)');
        }
        return;
      }
      if (line.startsWith('FW_INFO:')) {
        const parts = line.split(':');
        if (parts.length >= 3) {
          this.fwName = parts[1];
          this.fwVersion = parts[2];
        }
        return;
      }

      if (line.startsWith('BENCH_RES:JS_END:')) {
        window.dispatchEvent(new CustomEvent('bench-js-done'));
        return;
      }

      if (line.startsWith('BENCH_RES:MCU:')) {
        const timeMs = parseInt(line.split(':')[2]);
        window.dispatchEvent(new CustomEvent('bench-mcu-done', { detail: timeMs }));
        return;
      }

      // ADD THIS NEW BLOCK:
      if (line.startsWith('BENCH_RES:MACRO:')) {
        const timeMs = parseInt(line.split(':')[2]);
        window.dispatchEvent(new CustomEvent('bench-macro-done', { detail: timeMs }));
        return;
      }
      
      if (line.startsWith('LIVE_CLK:')) {
        const parts = line.split(':')
        this.activeSlot = `${parts[3] || 'Unnamed'} (${parts[1].padStart(2, '0')})`
        this.clockStatus = parts[2]
      } else {
        if (line.includes("LOG: SD Slot scan complete.")) return; 

        let isMuted = false
        if (this.sdData.status === 'RUNNING' || this.sdData.status === 'DOWNLINKING') {
          isMuted = true
          this.diagSequence = { text: line, count: this.diagSequence.count + 1 }
        }

        if (line.includes('CAPACITY:')) {
          const m = line.match(/CAPACITY:\s+(\d+)\s+MB Total,\s+(\d+)\s+MB Free/)
          if (m) { this.sdData.capacity = m[1] + ' MB'; this.sdData.free = m[2] + ' MB' }
        } else if (line.includes('WRITE: OK')) { this.sdData.health = 'Write OK'
        } else if (line.includes('READ: OK')) { this.sdData.health += ' / Read OK'
        } else if (line.includes('Write Speed:')) {
          const m = line.match(/Write Speed:\s+([\d.]+)\s+KB\/s/)
          if (m) this.sdData.fatWrite = m[1] + ' KB/s'
        } else if (line.includes('Read Speed :')) {
          const m = line.match(/Read Speed :\s+([\d.]+)\s+KB\/s/)
          if (m) this.sdData.fatRead = m[1] + ' KB/s'
        } else if (line.match(/^Speed :\s+([\d.]+)\s+KB\/s/)) { 
          const m = line.match(/^Speed :\s+([\d.]+)\s+KB\/s/)
          if (m) this.sdData.usbUplink = m[1] + ' KB/s'
        }

        if (line.startsWith('SLOT_NAME:')) {
          const parts = line.split(':');
          const slotIdx = parseInt(parts[1]);
          if (!isNaN(slotIdx)) this.slotAliases[slotIdx] = parts[2];
          return;
        }
        if (!isMuted || line.startsWith('ERR:')) this.addLog(line)
      }
    }
  }
})