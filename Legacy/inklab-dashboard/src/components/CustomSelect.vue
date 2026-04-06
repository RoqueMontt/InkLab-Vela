<script setup>
import { ref, onMounted, onUnmounted, computed } from 'vue'

const props = defineProps({
  modelValue: { required: true },
  options: { type: Array, required: true }, // Array of { value: Any, label: String }
  disabled: { type: Boolean, default: false },
  placeholder: { type: String, default: 'Select...' },
  size: { type: String, default: 'normal' } // 'normal' | 'small'
})

const emit = defineEmits(['update:modelValue', 'change'])

const isOpen = ref(false)
const wrapperRef = ref(null)

// Close dropdown when clicking outside
const closeDropdown = (e) => {
  if (isOpen.value && wrapperRef.value && !wrapperRef.value.contains(e.target)) {
    isOpen.value = false
  }
}

onMounted(() => document.addEventListener('click', closeDropdown))
onUnmounted(() => document.removeEventListener('click', closeDropdown))

const selectedLabel = computed(() => {
  const opt = props.options.find(o => o.value === props.modelValue)
  return opt ? opt.label : props.placeholder
})

const selectOption = (val) => {
  emit('update:modelValue', val)
  emit('change', val)
  isOpen.value = false
}
</script>

<template>
  <div class="custom-select-wrapper" ref="wrapperRef" :class="size">
    <button 
      type="button"
      class="custom-select-btn" 
      @click="isOpen = !isOpen" 
      :disabled="disabled"
      :class="{ 'is-open': isOpen }"
    >
      <span class="val-text">{{ selectedLabel }}</span>
      <span class="custom-caret">▼</span>
    </button>
    
    <div v-if="isOpen" class="custom-options-menu">
      <div 
        v-for="opt in options" 
        :key="opt.value"
        class="custom-option"
        :class="{ selected: modelValue === opt.value }"
        @click="selectOption(opt.value)"
      >
        {{ opt.label }}
      </div>
    </div>
  </div>
</template>

<style scoped>
.custom-select-wrapper { position: relative; width: 100%; }

.custom-select-btn {
  display: flex; align-items: center; justify-content: space-between; gap: 8px;
  width: 100%; background: white; border: 1px solid var(--edge);
  border-radius: 6px; font-family: inherit; color: var(--ink);
  cursor: pointer; text-align: left; transition: 0.2s; outline: none;
}
.custom-select-btn:hover:not(:disabled) { background: #f8fafc; border-color: #cbd5e1; }
.custom-select-btn:focus, .custom-select-btn.is-open { border-color: var(--info); box-shadow: 0 0 0 2px rgba(59,130,246,0.1); background: white; }
.custom-select-btn:disabled { opacity: 0.5; cursor: not-allowed; }

/* Sizing Variants */
.normal .custom-select-btn { padding: 10px 12px; font-size: 0.9rem; font-weight: 600; height: 42px; }
.small .custom-select-btn { padding: 4px 10px; font-size: 0.75rem; font-weight: 600; height: 28px; background: var(--ghost); }
.small .custom-select-btn:hover:not(:disabled) { background: #e2e8f0; }

.val-text { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; flex-grow: 1; }
.custom-caret { font-size: 0.6rem; color: #94a3b8; transition: transform 0.2s; flex-shrink: 0; }
.is-open .custom-caret { transform: rotate(180deg); color: var(--info); }

.custom-options-menu {
  position: absolute; top: calc(100% + 4px); left: 0; min-width: 100%;
  background: var(--paper); border: 1px solid var(--edge);
  border-radius: 6px; box-shadow: 0 10px 25px rgba(0,0,0,0.15);
  z-index: 1000; overflow-y: auto; max-height: 250px;
  display: flex; flex-direction: column;
}
.small .custom-options-menu { min-width: 160px; }

.custom-option {
  padding: 10px 12px; font-size: 0.85rem; font-weight: 500;
  color: var(--ink); cursor: pointer; transition: background 0.1s, color 0.1s;
  white-space: nowrap; text-overflow: ellipsis;
}
.small .custom-option { padding: 8px 10px; font-size: 0.75rem; }

.custom-option:hover { background: var(--ghost); color: var(--info); }
.custom-option.selected { background: #eff6ff; color: var(--info); font-weight: 700; }
</style>