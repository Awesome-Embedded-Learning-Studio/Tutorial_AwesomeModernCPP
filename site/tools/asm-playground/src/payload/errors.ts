export class PayloadError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'PayloadError';
  }
}

export function requireValue(condition: unknown, message: string): asserts condition {
  if (!condition) throw new PayloadError(message);
}
