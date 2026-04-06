<template>
  <div class="algo-page">
    <div class="header-banner">
      <span class="section-label">Glucose Analysis Engine</span>
    </div>
    
    <div class="grid-layout">
      <!-- The Big Chart -->
      <div class="chart-card large">
        <div class="card-title">Real-time Glucose Trend (mg/dL)</div>
        <div class="chart-placeholder">
          <!-- You can re-use the Chart.js logic from PowerDashboard here -->
          <p class="empty-msg">Algorithm Engine: Standby</p>
        </div>
      </div>

      <!-- Stats Cards -->
      <div class="stats-sidebar">
        <div class="stat-card" v-for="stat in ['Mean', 'GMI', 'CV', 'TIR']" :key="stat">
          <label>{{ stat }}</label>
          <div class="val">--</div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.algo-page { display: flex; flex-direction: column; gap: 20px; padding: 20px; }
.grid-layout { display: grid; grid-template-columns: 3fr 1fr; gap: 20px; }
.chart-card { background: white; border: 1px solid var(--edge); border-radius: 12px; padding: 20px; height: 400px; }
.chart-placeholder { border: 2px dashed var(--edge); height: 80%; display: flex; align-items: center; justify-content: center; border-radius: 8px; }
.stat-card { background: var(--ghost); padding: 20px; border-radius: 8px; border: 1px solid var(--edge); text-align: center; }
.stat-card label { display: block; font-size: 0.7rem; color: #64748b; text-transform: uppercase; margin-bottom: 8px; }
.stat-card .val { font-size: 1.5rem; font-weight: 800; font-family: monospace; }
</style>