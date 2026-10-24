import type { InjectionKey } from 'vue';
export const DEFAULT_WORKER_URL = '/asm-engine/executor-worker.js';
export interface AsmExecutorOptions { workerUrl?: string }
export const optionsKey: InjectionKey<AsmExecutorOptions> = Symbol('asm-executor-options');
