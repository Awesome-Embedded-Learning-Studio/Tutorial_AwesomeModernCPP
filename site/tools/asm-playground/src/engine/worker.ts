import { Runtime } from './runtime';
import { x86Engine } from './interpreter/cpu';
import type { WorkerRequest, WorkerResponse } from './protocol';

const scope = globalThis as unknown as {
  postMessage(message: WorkerResponse): void;
  onmessage: (event: MessageEvent<WorkerRequest>) => void;
};
let runtime: Runtime | undefined;
let running = false;
let runSteps = 0;

function publish(): void {
  if (runtime) scope.postMessage({ type: 'snapshot', snapshot: runtime.snapshot() });
}

function batch(): void {
  if (!running || !runtime) return;
  const budget = Math.min(runtime.payload.execution.max_steps ?? 10000, 10000);
  for (let i = 0; i < 100 && running; i++) {
    const state = runtime.step();
    runSteps++;
    if (state.status === 'error' || state.status === 'finished') {
      running = false;
      scope.postMessage({ type: 'snapshot', snapshot: state });
      return;
    }
    if (runSteps >= budget) {
      running = false;
      runtime.status = 'paused';
      runtime.reason = `已达到本次 ${budget} 条指令预算，可继续或重置`;
      break;
    }
  }
  if (running) runtime.status = 'running';
  publish();
  if (running) setTimeout(batch, 0);
}
scope.onmessage = ({ data }) => {
  try {
    if (data.type === 'init') {
      runtime = new Runtime(x86Engine, data.payload);
      publish();
    } else if (runtime && data.type === 'step' && !running) {
      scope.postMessage({ type: 'snapshot', snapshot: runtime.step() });
    } else if (runtime && data.type === 'run' && !running && runtime.status !== 'finished' && runtime.status !== 'error') {
      running = true;
      runSteps = 0;
      runtime.reason = undefined;
      batch();
    } else if (runtime && data.type === 'pause') {
      running = false;
      if (runtime.status !== 'error' && runtime.status !== 'finished') {
        runtime.status = 'paused';
        runtime.reason = '已暂停';
      }
      publish();
    }
  } catch (error) {
    running = false;
    scope.postMessage({ type: 'error', error: String(error) });
  }
};
