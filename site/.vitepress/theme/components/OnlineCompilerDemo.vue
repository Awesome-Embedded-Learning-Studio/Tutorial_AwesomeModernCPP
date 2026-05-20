<template>
  <section class="online-compiler-demo">
    <div class="online-compiler-demo__header">
      <div>
        <p class="online-compiler-demo__eyebrow">Compiler Explorer</p>
        <h3>{{ title }}</h3>
        <p v-if="description" class="online-compiler-demo__description">
          {{ description }}
        </p>
      </div>
      <a
        class="online-compiler-demo__source"
        :href="sourceUrl"
        target="_blank"
        rel="noreferrer"
      >
        {{ sourcePath }}
      </a>
      <a
        v-if="armSourcePath"
        class="online-compiler-demo__source"
        :href="armSourceUrl"
        target="_blank"
        rel="noreferrer"
      >
        ARM: {{ armSourcePath }}
      </a>
    </div>

    <div class="online-compiler-demo__meta">
      <span v-for="action in actions" :key="action.id">
        {{ action.label }}: {{ action.compiler }} {{ action.options }}
        <template v-if="action.id === 'arm-asm' && armSourcePath"> / 精简 ARM 源码</template>
      </span>
    </div>

    <div class="online-compiler-demo__actions">
      <button
        v-for="action in actions"
        :key="action.id"
        class="online-compiler-demo__button"
        type="button"
        :disabled="Boolean(activeAction)"
        @click="compile(action)"
      >
        <span v-if="activeAction === action.id">处理中...</span>
        <span v-else>{{ action.label }}</span>
      </button>
      <button
        class="online-compiler-demo__button online-compiler-demo__button--secondary"
        type="button"
        :disabled="Boolean(activeAction)"
        @click="openEditor('default')"
      >
        编辑源码
      </button>
      <button
        v-if="armSourcePath"
        class="online-compiler-demo__button online-compiler-demo__button--secondary"
        type="button"
        :disabled="Boolean(activeAction)"
        @click="openEditor('arm')"
      >
        编辑 ARM 源码
      </button>
      <button
        class="online-compiler-demo__button online-compiler-demo__button--secondary"
        type="button"
        :disabled="Boolean(activeAction)"
        @click="openGodbolt"
      >
        打开 Godbolt
      </button>
    </div>

    <div v-if="editorOpen" class="online-compiler-demo__editor">
      <div class="online-compiler-demo__editor-header">
        <strong>{{ editorSourceKind === 'arm' ? '编辑 ARM 精简源码' : '编辑源码' }}</strong>
        <span>上方运行/汇编按钮会使用当前编辑内容</span>
      </div>
      <textarea
        v-model="editorSource"
        class="online-compiler-demo__textarea"
        spellcheck="false"
        autocomplete="off"
        autocorrect="off"
        autocapitalize="off"
      />
      <div class="online-compiler-demo__editor-actions">
        <button
          class="online-compiler-demo__button online-compiler-demo__button--secondary"
          type="button"
          :disabled="Boolean(activeAction)"
          @click="resetEditor"
        >
          还原源码
        </button>
        <button
          class="online-compiler-demo__button online-compiler-demo__button--secondary"
          type="button"
          @click="editorOpen = false"
        >
          收起编辑器
        </button>
      </div>
    </div>

    <p v-if="error" class="online-compiler-demo__error">
      {{ error }}
    </p>

    <div v-if="result" class="online-compiler-demo__result">
      <div class="online-compiler-demo__result-header">
        <strong>{{ result.title }}</strong>
        <span>{{ result.compiler }} {{ result.options }}</span>
      </div>
      <pre><code>{{ result.text }}</code></pre>
    </div>

    <noscript>
      <p class="online-compiler-demo__noscript">
        需要启用 JavaScript 才能运行示例或请求汇编输出；源码仍可通过上方链接查看。
      </p>
    </noscript>
  </section>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'

type ActionId = 'run' | 'x86-asm' | 'arm-asm'
type SourceKind = 'default' | 'arm'

interface DemoAction {
  id: ActionId
  label: string
  compiler: string
  options: string
  executorRequest: boolean
}

interface CompileResult {
  title: string
  compiler: string
  options: string
  text: string
}

const props = withDefaults(defineProps<{
  title: string
  sourcePath: string
  armSourcePath?: string
  description?: string
  allowRun?: boolean
  allowX86Asm?: boolean
  allowArmAsm?: boolean
  runCompiler?: string
  runOptions?: string
  x86Compiler?: string
  x86Options?: string
  armCompiler?: string
  armOptions?: string
  branch?: string
  rawBase?: string
}>(), {
  description: '',
  allowRun: false,
  allowX86Asm: false,
  allowArmAsm: false,
  runCompiler: 'g152',
  runOptions: '-O2 -std=c++20',
  x86Compiler: 'g152',
  x86Options: '-O2 -std=c++20',
  armCompiler: 'armg1520',
  armOptions: '-O2 -std=c++20 -mcpu=cortex-m3 -mthumb -ffreestanding -fno-exceptions -fno-rtti',
  branch: 'main',
  rawBase: 'https://raw.githubusercontent.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP',
})

const source = ref('')
const armSource = ref('')
const activeAction = ref<ActionId | 'godbolt' | 'source' | ''>('')
const error = ref('')
const result = ref<CompileResult | null>(null)
const editorOpen = ref(false)
const editorSourceKind = ref<SourceKind>('default')
const editorSource = ref('')

const normalizedSourcePath = computed(() => props.sourcePath.replace(/^\/+/, ''))
const normalizedArmSourcePath = computed(() => (props.armSourcePath || props.sourcePath).replace(/^\/+/, ''))
const sourceUrl = computed(() => `${props.rawBase}/${props.branch}/${normalizedSourcePath.value}`)
const armSourceUrl = computed(() => `${props.rawBase}/${props.branch}/${normalizedArmSourcePath.value}`)

const actions = computed<DemoAction[]>(() => {
  const available: DemoAction[] = []
  if (props.allowRun) {
    available.push({
      id: 'run',
      label: '运行',
      compiler: props.runCompiler,
      options: props.runOptions,
      executorRequest: true,
    })
  }
  if (props.allowX86Asm) {
    available.push({
      id: 'x86-asm',
      label: '看 x86-64 汇编',
      compiler: props.x86Compiler,
      options: props.x86Options,
      executorRequest: false,
    })
  }
  if (props.allowArmAsm) {
    available.push({
      id: 'arm-asm',
      label: '看 ARM 汇编',
      compiler: props.armCompiler,
      options: props.armOptions,
      executorRequest: false,
    })
  }
  return available
})

function sourceKindForAction(action?: DemoAction): SourceKind {
  return action?.id === 'arm-asm' && props.armSourcePath ? 'arm' : 'default'
}

async function loadSource(action?: DemoAction): Promise<string> {
  const kind = sourceKindForAction(action)
  if (editorOpen.value && editorSourceKind.value === kind) {
    return editorSource.value
  }
  return loadSourceForKind(kind)
}

async function loadSourceForKind(kind: SourceKind): Promise<string> {
  const useArmSource = kind === 'arm'
  const cachedSource = useArmSource ? armSource : source
  const url = useArmSource ? armSourceUrl.value : sourceUrl.value

  if (cachedSource.value) return cachedSource.value

  const response = await fetch(url)
  if (!response.ok) {
    const fallback = useArmSource ? builtInArmSources[normalizedArmSourcePath.value] : ''
    if (fallback) {
      cachedSource.value = fallback
      return cachedSource.value
    }
    throw new Error(`无法读取源码 (${response.status} ${response.statusText})`)
  }
  cachedSource.value = await response.text()
  return cachedSource.value
}

async function openEditor(kind: SourceKind): Promise<void> {
  activeAction.value = 'source'
  error.value = ''

  try {
    editorSourceKind.value = kind
    editorSource.value = await loadSourceForKind(kind)
    editorOpen.value = true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    activeAction.value = ''
  }
}

async function resetEditor(): Promise<void> {
  activeAction.value = 'source'
  error.value = ''

  try {
    const kind = editorSourceKind.value
    if (kind === 'arm') {
      armSource.value = ''
    } else {
      source.value = ''
    }
    editorSource.value = await loadSourceForKind(kind)
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    activeAction.value = ''
  }
}

function linesToText(value: unknown): string {
  if (!value) return ''
  if (typeof value === 'string') return stripAnsi(value)
  if (Array.isArray(value)) {
    return value.map((line) => {
      if (typeof line === 'string') return stripAnsi(line)
      if (line && typeof line === 'object' && 'text' in line) {
        return stripAnsi(String((line as { text: unknown }).text ?? ''))
      }
      return stripAnsi(String(line ?? ''))
    }).join('\n')
  }
  return stripAnsi(String(value))
}

function stripAnsi(value: string): string {
  return value.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, '')
}

function extractExecutionText(payload: any): string {
  const exec = payload.execResult ?? payload.executionResult ?? payload
  const chunks = [
    linesToText(exec.stdout),
    linesToText(exec.stderr),
    linesToText(payload.stdout),
    linesToText(payload.stderr),
    linesToText(payload.buildResult?.stdout),
    linesToText(payload.buildResult?.stderr),
  ].filter(Boolean)

  if (exec.code !== undefined) chunks.push(`exit code: ${exec.code}`)
  return chunks.join('\n').trim()
}

function extractAsmText(payload: any): string {
  const asm = linesToText(payload.asm)
  const diagnostics = [
    linesToText(payload.stdout),
    linesToText(payload.stderr),
    linesToText(payload.buildResult?.stdout),
    linesToText(payload.buildResult?.stderr),
  ].filter(Boolean).join('\n')

  if (isCompilationFailure(payload, asm)) {
    return (diagnostics || asm || '编译失败，但 Compiler Explorer 没有返回诊断信息。').trim()
  }

  return (asm || diagnostics || 'Compiler Explorer 没有返回可显示的输出。').trim()
}

function isCompilationFailure(payload: any, asm: string): boolean {
  return payload.code !== undefined && payload.code !== 0
    || payload.buildResult?.code !== undefined && payload.buildResult.code !== 0
    || asm.includes('<Compilation failed>')
}

async function compile(action: DemoAction): Promise<void> {
  activeAction.value = action.id
  error.value = ''
  result.value = null

  try {
    const currentSource = await loadSource(action)
    const response = await fetch(`https://godbolt.org/api/compiler/${action.compiler}/compile`, {
      method: 'POST',
      headers: {
        Accept: 'application/json',
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        source: currentSource,
        options: {
          userArguments: action.options,
          compilerOptions: {
            executorRequest: action.executorRequest,
          },
          filters: {
            binary: false,
            commentOnly: true,
            demangle: true,
            directives: true,
            execute: action.executorRequest,
            intel: action.id === 'x86-asm',
            labels: true,
            libraryCode: false,
            trim: false,
          },
          executeParameters: {
            args: '',
            stdin: '',
          },
        },
      }),
    })

    if (!response.ok) {
      throw new Error(`Compiler Explorer 请求失败 (${response.status} ${response.statusText})`)
    }

    const payload = await response.json()
    result.value = {
      title: action.label,
      compiler: action.compiler,
      options: action.options,
      text: action.executorRequest ? extractExecutionText(payload) : extractAsmText(payload),
    }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    activeAction.value = ''
  }
}

async function openGodbolt(): Promise<void> {
  activeAction.value = 'godbolt'
  error.value = ''

  try {
    const kind: SourceKind = editorOpen.value
      ? editorSourceKind.value
      : (props.allowArmAsm && !props.allowRun && props.armSourcePath ? 'arm' : 'default')
    const currentSource = editorOpen.value ? editorSource.value : await loadSourceForKind(kind)
    const state = buildClientState(currentSource, kind)
    const encoded = encodeURIComponent(toBase64(JSON.stringify(state)))
    window.open(`https://godbolt.org/clientstate/${encoded}`, '_blank', 'noopener,noreferrer')
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    activeAction.value = ''
  }
}

function buildClientState(currentSource: string, kind: SourceKind) {
  const compilers = actions.value
    .filter((action) => sourceKindForAction(action) === kind)
    .filter((action) => !action.executorRequest)
    .map((action, index) => ({
      id: index + 1,
      compiler: action.compiler,
      options: action.options,
      filters: {
        binary: false,
        commentOnly: true,
        demangle: true,
        directives: true,
        intel: action.id === 'x86-asm',
        labels: true,
        libraryCode: false,
        trim: false,
      },
    }))

  const executors = actions.value
    .filter((action) => sourceKindForAction(action) === kind)
    .filter((action) => action.executorRequest)
    .map((action, index) => ({
      id: index + 1,
      compiler: action.compiler,
      options: action.options,
      arguments: '',
      stdin: '',
    }))

  return {
    sessions: [{
      id: 1,
      language: 'c++',
      source: currentSource,
      filename: (kind === 'arm' ? normalizedArmSourcePath.value : normalizedSourcePath.value).split('/').pop() || 'demo.cpp',
      compilers,
      executors,
    }],
  }
}

function toBase64(value: string): string {
  const bytes = new TextEncoder().encode(value)
  let binary = ''
  const chunkSize = 0x8000
  for (let i = 0; i < bytes.length; i += chunkSize) {
    const chunk = bytes.subarray(i, i + chunkSize)
    binary += String.fromCharCode(...chunk)
  }
  return btoa(binary)
}

const builtInArmSources: Record<string, string> = {
  'code/examples/compiler_explorer/gpio_zero_overhead_arm.cpp': String.raw`#include <cstdint>

#define GPIO_PORT_A_C ((volatile std::uint32_t*)0x40020000u)
#define PIN_5_C (1u << 5)

void set_pin_c() {
    *GPIO_PORT_A_C |= PIN_5_C;
}

template<std::uint32_t Address>
class GPIO_Port {
    static volatile std::uint32_t& reg() {
        return *reinterpret_cast<volatile std::uint32_t*>(Address);
    }

public:
    static void set_pin(std::uint8_t pin) {
        reg() |= (1u << pin);
    }
};

using GPIOA = GPIO_Port<0x40020000u>;

void set_pin_cpp() {
    GPIOA::set_pin(5);
}
`,
  'code/examples/compiler_explorer/constexpr_baud_arm.cpp': String.raw`#include <cstdint>

std::uint32_t calculate_baud_divisor_runtime(std::uint32_t cpu_freq, std::uint32_t baud) {
    return cpu_freq / (16u * baud);
}

constexpr std::uint32_t calculate_baud_divisor_constexpr(std::uint32_t cpu_freq, std::uint32_t baud) {
    return cpu_freq / (16u * baud);
}

constexpr std::uint32_t divisor = calculate_baud_divisor_constexpr(72000000u, 115200u);
static_assert(divisor == 39u);

std::uint32_t runtime_divisor(std::uint32_t baud) {
    return calculate_baud_divisor_runtime(72000000u, baud);
}

std::uint32_t constexpr_divisor() {
    return divisor;
}
`,
  'code/examples/compiler_explorer/static_polymorphism_arm.cpp': String.raw`int read_adc_hw() {
    return 42;
}

struct ADCSensor {
    int read() {
        return read_adc_hw();
    }
};

struct TempSensor {
    int read() {
        return 25;
    }
};

template<typename Sensor>
int poll(Sensor& sensor) {
    return sensor.read();
}

int poll_adc() {
    ADCSensor adc;
    return poll(adc);
}

int poll_temp() {
    TempSensor temp;
    return poll(temp);
}
`,
  'code/examples/compiler_explorer/fixed_pool_arm.cpp': String.raw`#include <cstddef>
#include <cstdint>

template<std::size_t BlockSize, std::size_t BlockCount>
class FixedPool {
    struct Block {
        alignas(std::max_align_t) std::uint8_t data[BlockSize];
    };

    Block pool_[BlockCount];
    std::size_t free_list_[BlockCount];
    std::size_t free_head_;
    std::size_t used_count_;

public:
    FixedPool() : free_head_(0), used_count_(0) {
        for (std::size_t i = 0; i < BlockCount; ++i) {
            free_list_[i] = i + 1;
        }
        free_list_[BlockCount - 1] = static_cast<std::size_t>(-1);
    }

    void* allocate() {
        if (free_head_ == static_cast<std::size_t>(-1)) {
            return nullptr;
        }

        const std::size_t index = free_head_;
        free_head_ = free_list_[index];
        ++used_count_;
        return pool_[index].data;
    }

    void deallocate(void* ptr) {
        if (!ptr) {
            return;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(&pool_[0]);
        const auto current = reinterpret_cast<std::uintptr_t>(ptr);
        const std::size_t index = (current - base) / sizeof(Block);

        free_list_[index] = free_head_;
        free_head_ = index;
        --used_count_;
    }

    std::size_t used_count() const {
        return used_count_;
    }
};

FixedPool<32, 8> pool;

void* allocate_packet_buffer() {
    return pool.allocate();
}

void release_packet_buffer(void* buffer) {
    pool.deallocate(buffer);
}

std::size_t used_packet_buffers() {
    return pool.used_count();
}
`,
}
</script>
