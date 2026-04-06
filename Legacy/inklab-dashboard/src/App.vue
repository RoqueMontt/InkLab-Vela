<script setup>
import { ref, watch, onMounted } from 'vue'
import AppHeader from './components/Header.vue'
import SidebarLog from './components/SidebarLog.vue'
import FpgaConfig from './components/views/FpgaConfig.vue'
import PowerDashboard from './components/views/PowerDashboard.vue'
import DiagnosticModal from './components/DiagnosticModal.vue'
import SwTerminal from './components/views/SwTerminal.vue'
import JoystickView from './components/views/JoystickView.vue'
import SpiTester from './components/views/SpiTester.vue'
import { useSerial } from './composables/useSerial'
import { useSystemStore } from './stores/systemStore'
import AlgorithmDashboard from './components/views/AlgorithmDashboard.vue'
import PatientProfile from './components/views/PatientProfile.vue'
import PinoutConfig from './components/views/PinoutConfig.vue' 
import AutomationBench from './components/views/AutomationBench.vue'
import SignalGenerator from './components/views/SignalGenerator.vue'



const { attemptAutoConnect, sendCommand } = useSerial()
const store = useSystemStore()

const powerDownAndDismiss = () => {
  sendCommand('BQ:BACKUP:0')
  setTimeout(() => { sendCommand('BQ:OTG:0') }, 20)
  setTimeout(() => { sendCommand('BQ:DIS_IN:1') }, 40)
  setTimeout(() => { sendCommand('BQ:SHIP:2') }, 150)
  store.showSdWarning = false
  store.sdWarningDismissed = true
}

const ignoreWarning = () => {
  store.showSdWarning = false
  store.sdWarningDismissed = true
}

// Add our combined component 
const tabs = { FpgaConfig, PowerDashboard, JoystickView, PinoutConfig, SwTerminal, SpiTester, AlgorithmDashboard, PatientProfile, AutomationBench, SignalGenerator }
// 1. Create a memory object for the default starting tabs of each mode
const tabHistory = ref({
  CONFIG: 'FpgaConfig',
  DEBUG: 'SwTerminal',
  APP: 'AlgorithmDashboard'
})

// 2. Initialize the active tab based on the current mode
const activeTab = ref(tabHistory.value[store.appMode])

// 3. Whenever the user clicks a new tab, save it to the current mode's history
watch(activeTab, (newTab) => {
  tabHistory.value[store.appMode] = newTab
})

// 4. Whenever the mode changes, restore the saved tab from history
watch(() => store.appMode, (newMode) => {
  if (tabHistory.value[newMode]) {
    activeTab.value = tabHistory.value[newMode]
  }
})

// Dismiss single toast
const dismissToast = (id) => {
  store.notifications = store.notifications.filter(n => n.id !== id)
}



onMounted(() => {
  attemptAutoConnect()

  window.addEventListener('keydown', (e) => {
    
    // Check if the user is holding the Alt key 
    if (e.altKey) {
      
      // 1. App Mode Navigation (Alt + A / Alt + S)
      const isModeLeft = e.code === 'KeyA';
      const isModeRight = e.code === 'KeyS';

      if (isModeLeft || isModeRight) {
        e.preventDefault(); // Stop the browser from triggering default Alt menus
        
        const modes = ['CONFIG', 'DEBUG', 'APP'];
        const currentModeIdx = modes.indexOf(store.appMode);
        
        if (isModeLeft) {
          // Cycle backwards
          store.appMode = modes[(currentModeIdx - 1 + modes.length) % modes.length];
        } else if (isModeRight) {
          // Cycle forwards
          store.appMode = modes[(currentModeIdx + 1) % modes.length];
        }
        return; // Exit early
      }

      // 2. Tab Navigation (Alt + Q / Alt + W or Left/Right Arrows)
      const isTabLeft = e.code === 'KeyQ' || e.code === 'ArrowLeft'; 
      const isTabRight = e.code === 'KeyW' || e.code === 'ArrowRight';

      if (isTabLeft || isTabRight) {
        e.preventDefault(); // Stop the browser from triggering default Alt menus 

        const tabArray = store.currentTabsArray;
        const currentIndex = tabArray.indexOf(activeTab.value);

        if (currentIndex !== -1) {
          if (isTabLeft) {
            activeTab.value = tabArray[(currentIndex - 1 + tabArray.length) % tabArray.length];
          } else if (isTabRight) {
            activeTab.value = tabArray[(currentIndex + 1) % tabArray.length];
          }
        }
      }
    }
  })
})
</script>

<template>
  <div class="workspace">
    <AppHeader v-model:activeTab="activeTab" />
    
    <!-- Apply 'full-width' class if in SW mode -->
    <div class="main-layout" :class="{ 'full-width': store.appMode === 'APP' || activeTab === 'SwTerminal' }">
      <div class="content-left">
        <KeepAlive>
          <component :is="tabs[activeTab]" />
        </KeepAlive>
      </div>
      
      <SidebarLog v-if="store.appMode !== 'APP' && activeTab !== 'SwTerminal'" />
    </div>
  </div>
  
    <DiagnosticModal v-if="store.showDiagnosticModal" />

  <!-- SD Card Safety Modal -->
    <div v-if="store.showSdWarning" class="modal-overlay">
      <div class="modal-card">
        <div class="modal-header">⚠️ Missing Storage Media</div>
        <div class="modal-body">
          <p>The MicroSD card is not detected. <b>Do NOT hot-plug the SD card while the system is live</b>, as the sudden inrush current will crash the 3.3V power rails.</p>
          <p>Would you like to safely disconnect the input power before inserting the card?</p>
        </div>
        <div class="modal-actions">
          <button class="btn-warning" @click="powerDownAndDismiss">Power Down Inputs (Safe)</button>
          <button class="btn-secondary" @click="ignoreWarning">Continue Without SD (Testing)</button>
        </div>
      </div>
    </div>

      <!-- NEW: Global Toast Notification Container -->
  <div class="toast-container">
    <div v-for="toast in store.notifications" :key="toast.id" class="toast-card" :class="toast.type">
      <div class="toast-content">{{ toast.msg }}</div>
      <button class="toast-close" @click="dismissToast(toast.id)">×</button>
    </div>
  </div>
</template> 

<style scoped>
.workspace { 
  width: 100vw;
  height: 100vh;
  display: flex;
  flex-direction: column;
  background: var(--paper); 
}

.main-layout { 
  display: grid; 
  grid-template-columns: 1fr 440px; 
  flex-grow: 1; 
  min-height: 0; 
}

/* Forces the left side to take up the whole screen when active */
.main-layout.full-width {
  grid-template-columns: 1fr;
}

.content-left { 
  padding: 32px; 
  overflow-y: auto;
  height: 100%;
  display: flex;
  flex-direction: column;
  gap: 32px;
  min-width: 0; 
  scrollbar-width: thin;
}

/* Modal Overlay Styles */
.modal-overlay {
  position: fixed;
  top: 0; left: 0; width: 100vw; height: 100vh;
  background: rgba(15, 23, 42, 0.8);
  backdrop-filter: blur(4px);
  display: flex; align-items: center; justify-content: center;
  z-index: 9999;
}
.modal-card {
  background: var(--paper);
  width: 480px;
  border-radius: 12px;
  border: 1px solid var(--edge);
  box-shadow: 0 10px 25px rgba(0,0,0,0.2);
  overflow: hidden;
  display: flex; flex-direction: column;
}
.modal-header {
  background: #fffbeb;
  color: #d97706;
  padding: 16px 20px;
  font-weight: 800;
  font-size: 1.1rem;
  border-bottom: 1px solid #fde68a;
}
.modal-body {
  padding: 24px 20px;
  font-size: 0.9rem;
  color: var(--ink);
  line-height: 1.6;
}
.modal-body p { margin-top: 0; margin-bottom: 12px; }
.modal-body b { color: var(--error); }
.modal-actions {
  display: flex; gap: 12px;
  padding: 16px 20px;
  background: var(--ghost);
  border-top: 1px solid var(--edge);
  justify-content: flex-end;
}
.btn-warning { background: var(--error); color: white; border: none; font-size: 0.85rem;}
.btn-secondary { background: var(--edge); color: var(--ink); font-size: 0.85rem;}

.toast-container {
  position: fixed;
  bottom: 24px;
  right: 24px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  z-index: 10000;
  pointer-events: none; /* Let clicks pass through gaps */
}

.toast-card {
  pointer-events: auto;
  min-width: 300px;
  max-width: 400px;
  background: var(--paper);
  border: 1px solid var(--edge);
  border-left: 6px solid var(--info);
  border-radius: 8px;
  box-shadow: 0 10px 25px rgba(0,0,0,0.15);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  animation: slideIn 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
}

.toast-card.error { border-left-color: var(--error); background: #fef2f2; }
.toast-card.success { border-left-color: var(--success); }
.toast-card.warning { border-left-color: var(--warning); }

.toast-content {
  font-size: 0.85rem;
  font-weight: 600;
  color: var(--ink);
  line-height: 1.4;
}

.toast-close {
  background: transparent;
  border: none;
  font-size: 1.5rem;
  line-height: 1;
  color: #94a3b8;
  cursor: pointer;
  padding: 0 0 0 12px;
}
.toast-close:hover { color: var(--ink); }

@keyframes slideIn {
  from { transform: translateX(120%); opacity: 0; }
  to { transform: translateX(0); opacity: 1; }
}
</style>