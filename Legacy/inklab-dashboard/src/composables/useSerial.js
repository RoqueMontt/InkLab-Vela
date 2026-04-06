import { ref } from 'vue'
import { useSystemStore } from '../stores/systemStore'

let port = null
let writer = null
let fileBuffer = null
let bytesSent = 0
let totalBytes = 0
let uploadStartTime = 0

// NEW: Global resolver for awaiting the BQ dump
let snapshotResolver = null;

export function useSerial() {
  const store = useSystemStore()

  // Wrapper to convert the asynchronous JSON event into a Promise
  const waitForSnapshotResponse = () => {
    return new Promise((resolve) => {
      snapshotResolver = resolve;
      setTimeout(() => {
        if (snapshotResolver === resolve) {
          snapshotResolver = null;
          resolve(null); // Resolve null on timeout
        }
      }, 150); 
    });
  };

  async function attemptAutoConnect() {
    if (!navigator.serial) return;
    try {
      const ports = await navigator.serial.getPorts();
      if (ports.length > 0) {
        port = ports[0];
        if (port.readable || port.writable) return;
        
        await openPort(true); // <--- Pass true here
        // Removed the store.addLog from here!
      }
    } catch (e) {
      console.error("Auto-connect failed:", e);
    }
  }

    async function openPort(isAuto = false) {
    await port.open({ baudRate: 115200 });
    store.isConnected = true;
    
    // Clear the terminal on every fresh connection
    store.clearLogs();
    
    // Log the connection immediately after clearing!
    if (isAuto) {
      store.addLog("Auto-reconnected to hardware.", "var(--info)");
    }

    // Reset safety states
    store.sdWarningDismissed = false;
    store.showSdWarning = false;

    // Listen for MCU reset/unplug
    port.addEventListener('disconnect', () => {
      store.isConnected = false;
      store.addLog("Hardware disconnected (MCU Reset).", "var(--warning)");
    });

    // FIX: Start readLoop FIRST so the port is actively pulling data 
    // before we start awaiting the handshake!
    readLoop();

    // Handshake loop
    const verifyPower = async () => {
      for (let i = 0; i < 20; i++) {
        // The store's processIncomingData normally logs everything.
        // We want this specific handshake to be silent.
        await write(new TextEncoder().encode('BQ:STATUS\n')); 
        const dump = await waitForSnapshotResponse();

        if (dump && dump.regs && dump.regs.length > 0x1C) {
          const reg1B = dump.regs[0x1B]; // REG1B_Charger_Status_0
          const reg1C = dump.regs[0x1C]; // REG1C_Charger_Status_1

          const vbusPresent = (reg1B & 0x01) !== 0; // Bit 0: VBUS_PRESENT_STAT
          const vbusStat = (reg1C >> 1) & 0x0F;     // Bits 4:1: VBUS_STAT_3:0

          // Check if VBUS is physically present and NOT in Backup Mode (0x0C/12)
          if (vbusPresent && vbusStat !== 12) {
            store.addLog("Hardware Linked: Power rails stable.", "var(--success)");
            return true;
          }
        }
        await new Promise(r => setTimeout(r, 50));
      }
      return false;
    };

    const isStable = await verifyPower();
    
    // Give the MCU a tiny breather after the heavy BQ polling
    await new Promise(r => setTimeout(r, 20));

    // Space out the initialization commands so the MCU buffer doesn't drop them
    await scanSlots(); 
    await new Promise(r => setTimeout(r, 20));
    
    await sendCommand("GET_SLOT");
    await new Promise(r => setTimeout(r, 20));
    
    await sendCommand("SYS:GET_FW_INFO"); // This will now successfully return your firmware version!
    await new Promise(r => setTimeout(r, 20));
    
    await sendCommand("IO:STATUS");



    if (isStable) {
      sendCommand(store.isSleeping ? 'SYS:SLEEP:1' : 'SYS:SLEEP:0');
    }
  }

  async function connect() {
    try {
      port = await navigator.serial.requestPort();
      await openPort(false);
      store.addLog("Serial link established.", "var(--success)");
    } catch (err) {
      store.addLog("ERR: Connection failed.", "var(--error)");
    }
  }

  async function write(data) {
    if (!port || !port.writable) return;
    writer = port.writable.getWriter();
    await writer.write(data);
    writer.releaseLock();
  }

async function sendCommand(cmd, ending = '\n') {
    if (store.isUploading) return;
    await write(new TextEncoder().encode(cmd + ending));

    if (cmd.startsWith('BQ:') && !cmd.startsWith('BQ:STATUS')) {
      setTimeout(() => write(new TextEncoder().encode('BQ:STATUS\n')), 150);
    }
  }

  async function scanSlots() { await sendCommand("SCAN"); }

  async function startUpload(buffer, targetSlot, clkDivider, slotName) {
    fileBuffer = buffer;
    totalBytes = buffer.length;
    bytesSent = 0;
    store.isUploading = true;
    store.uploadProgress = 0;
    uploadStartTime = performance.now();
    
    const safeName = slotName.trim().replace(/[:\s]/g, '_');
    const cmd = `START:${targetSlot}:${totalBytes}:${clkDivider}:${safeName}\n`;
    await write(new TextEncoder().encode(cmd));
  }

  async function sendNextChunk() {
    if (bytesSent >= totalBytes) return;
    const chunk = fileBuffer.slice(bytesSent, bytesSent + 8192); 
    await write(chunk);
    bytesSent += chunk.length;
    
    store.uploadProgress = Math.floor((bytesSent / totalBytes) * 100);
    const elapsed = (performance.now() - uploadStartTime) / 1000;
    store.transferSpeed = ((bytesSent / 1024) / elapsed).toFixed(1) + ' KB/s';
  }

  async function readLoop() {
    const textDecoder = new TextDecoderStream();
    const readableStreamClosed = port.readable.pipeTo(textDecoder.writable);
    const reader = textDecoder.readable.getReader();
    let buffer = "";

    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        buffer += value;

        // Split by newline. The last element is either an empty string (if it ended exactly on \n)
        // or an incomplete JSON/text string. We pop it off and keep it in the buffer for the next chunk.
        let lines = buffer.split('\n');
        buffer = lines.pop();
        
        for (let line of lines) {
          const clean = line.trim();
          if (!clean) continue;
          
          if (clean === "ACK_START" || clean === "ACK_CHUNK") {
            await sendNextChunk();
          } else if (clean === "ACK_DONE") {
            store.isUploading = false;
            store.addLog("Transfer complete.", "var(--success)");
            await scanSlots(); 

          // --- REPLACE THE ERROR BLOCK WITH THIS ---
          } else if (clean === "ERR_TIMEOUT") {
            store.isUploading = false;
            store.addLog("Upload Aborted: Timeout", "var(--error)");
            
          } else if (clean.startsWith("ERR:")) {
            // Only flag as an upload abort if an upload was actively running
            if (store.isUploading) {
              store.isUploading = false;
              store.addLog("Upload Aborted: " + clean, "var(--error)");
            } else {
              // Otherwise, just print the error normally
              store.addLog(clean, "var(--error)");
            }
          // -----------------------------------------

          }
          // Direct oscilloscope pass-through
          else if (clean.startsWith('{"t":"f"')) {
              try {
                  const fastData = JSON.parse(clean);
                  window.dispatchEvent(new CustomEvent('fast-telemetry', { detail: fastData }));
              } catch(e) {}
          }
          // JSON Processing Block
          else if (clean.startsWith('{')) {
              try {
                  const data = JSON.parse(clean);
                  // Intercept the dump for the handshake promise
                  if (data.type === 'bq_dump' && snapshotResolver) {
                      snapshotResolver(data);
                      snapshotResolver = null;
                  }
                  // Still forward to the Pinia store to update state/faults/raw metrics
                  store.processIncomingData(clean);
              } catch(e) {}
          }
          else {
            store.processIncomingData(clean);
          }
        }
      }
    } catch (err) {
      console.error("Read loop error:", err);
    } finally {
      reader.releaseLock();
    }
  }

  if (navigator.serial) {
    navigator.serial.addEventListener('connect', (e) => {
      setTimeout(() => attemptAutoConnect(), 500);
    });
  }

  return { connect, sendCommand, startUpload, scanSlots, attemptAutoConnect }
}