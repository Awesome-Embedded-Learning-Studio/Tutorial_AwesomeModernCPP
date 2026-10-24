import type { PayloadV1 } from '../payload/types';
import type { Snapshot } from './types';
import type { WorkerCommand, WorkerRequest, WorkerResponse } from './protocol';

export interface ClientOptions {
  workerUrl: string;
  onSnapshot(snapshot: Snapshot): void;
  onError(message: string): void;
}
export class ExecutorClient {
  private worker: Worker | undefined;
  private timer: ReturnType<typeof setTimeout> | undefined;
  private busy = false;
  constructor(private options: ClientOptions) { }
  private arm(ms = 2000): void {
    clearTimeout(this.timer);
    this.timer = setTimeout(() => this.fail('执行器无响应，已停止；请重置恢复'), ms);
  }
  private fail(message: string): void {
    this.destroy();
    this.options.onError(message);
  }
  load(payload: PayloadV1): void {
    this.destroy();
    const worker = new Worker(new URL(this.options.workerUrl, document.baseURI));
    this.worker = worker;
    this.busy = true;
    // Worker 加载使用独立超时，执行阶段仍使用两秒看门狗。
    this.arm(15000);
    worker.onmessage = ({ data }: MessageEvent<WorkerResponse>) => {
      if (worker !== this.worker) return;
      clearTimeout(this.timer);
      if (data.type === 'error') {
        this.fail(data.error);
        return;
      }
      if (data.type === 'snapshot') {
        this.busy = data.snapshot.status === 'running';
        if (this.busy) this.arm();
        this.options.onSnapshot(data.snapshot);
        if (data.snapshot.status === 'error') this.destroy();
      }
    };
    worker.onerror = event => {
      event.preventDefault();
      if (worker === this.worker) this.fail(event.message || '执行器加载失败');
    };
    worker.postMessage({ type: 'init', payload } satisfies WorkerRequest);
  }
  step(): void {
    this.command('step');
  }
  run(): void {
    this.command('run');
  }
  pause(): void {
    this.command('pause');
  }
  private command(type: WorkerCommand): void {
    if (!this.worker || (this.busy && type !== 'pause')) return;
    this.busy = true;
    this.arm();
    this.worker.postMessage({ type } satisfies WorkerRequest);
  }
  destroy(): void {
    clearTimeout(this.timer);
    this.worker?.terminate();
    this.worker = undefined;
    this.busy = false;
  }
}
