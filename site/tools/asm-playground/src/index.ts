import type { App, Plugin } from 'vue';
import AsmExecutor from './component/AsmExecutor.vue';
import { optionsKey, type AsmExecutorOptions } from './component/options';
export { AsmExecutor };
export const AsmExecutorPlugin: Plugin<[AsmExecutorOptions?]> = {
  install(app: App, options?: AsmExecutorOptions) {
    app.provide(optionsKey, options ?? {});
    app.component('AsmExecutor', AsmExecutor);
  },
};
export { validatePayload, PayloadError } from './payload/validate';
export type { PayloadV1, Hex } from './payload/types';
export type { AsmExecutorOptions } from './component/options';
export type { Snapshot, ExecutorStatus } from './engine/types';
