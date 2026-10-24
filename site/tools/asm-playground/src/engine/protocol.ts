import type { PayloadV1 } from '../payload/types';
import type { Snapshot } from './types';

export type WorkerCommand = 'step' | 'run' | 'pause';

export type WorkerRequest =
  | { type: 'init'; payload: PayloadV1 }
  | { type: WorkerCommand };

export type WorkerResponse =
  | { type: 'snapshot'; snapshot: Snapshot }
  | { type: 'error'; error: string };
