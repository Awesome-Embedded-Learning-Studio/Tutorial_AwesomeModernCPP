import { inject, type ComputedRef, type InjectionKey } from 'vue'
import type { Week } from '../utils/weekly'

export interface WeeklyPractice {
  week: ComputedRef<Week | undefined>
  active: ComputedRef<string>
  practicing: ComputedRef<boolean>
  overview: () => void
  select: (src: string, focusPanel?: boolean) => void
}
export const weeklyPracticeKey: InjectionKey<WeeklyPractice> = Symbol('weekly-practice')
export function useWeeklyPractice() { return inject(weeklyPracticeKey, undefined) }
