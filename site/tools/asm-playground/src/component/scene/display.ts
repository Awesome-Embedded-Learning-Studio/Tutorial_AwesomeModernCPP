// 显示策略：三种预设、逐项开关与固定关注对象。

export type Preset = 'follow' | 'custom' | 'all';
export type Speed = 'slow' | 'normal' | 'fast';

export interface DisplayToggles {
  /** 代码列表面板 */
  code: boolean;
  cpuDetail: boolean;
  alu: boolean;
  flags: boolean;
  memory: boolean;
  address: boolean;
  bytes: boolean;
  bits: boolean;
  note: boolean;
  motion: boolean;
}

export const DEFAULT_DISPLAY: DisplayToggles = {
  code: true,
  cpuDetail: true,
  alu: true,
  flags: false,
  memory: true,
  address: false,
  bytes: false,
  bits: false,
  note: true,
  motion: true,
};

export const ALL_DISPLAY: DisplayToggles = {
  code: true,
  cpuDetail: true,
  alu: true,
  flags: true,
  memory: true,
  address: true,
  bytes: true,
  bits: true,
  note: true,
  motion: true,
};

export const SPEED_MS: Record<Speed, { move: number; dwell: number }> = {
  slow: { move: 900, dwell: 900 },
  normal: { move: 520, dwell: 520 },
  fast: { move: 220, dwell: 180 },
};

export interface DisplayState {
  preset: Preset;
  toggles: DisplayToggles;
  pins: string[];
  hidden: string[];
}

export function resolveToggles(preset: Preset, current: DisplayToggles): DisplayToggles {
  if (preset === 'all') return { ...ALL_DISPLAY };
  return { ...current };
}
