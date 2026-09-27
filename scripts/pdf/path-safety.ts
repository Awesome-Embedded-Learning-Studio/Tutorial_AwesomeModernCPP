import { realpath } from 'node:fs/promises'
import { isAbsolute, relative, resolve, sep } from 'node:path'

function assertInside(canonicalRoot: string, canonicalPath: string, label: string): void {
  const rel = relative(canonicalRoot, canonicalPath)
  if (rel === '..' || rel.startsWith(`..${sep}`) || isAbsolute(rel)) {
    throw new Error(`${label} escapes the repository: ${canonicalPath}`)
  }
}

/**
 * Resolve both sides through the filesystem before checking containment.
 * A lexical `relative()` check alone can be bypassed by a repository symlink
 * whose target lives outside the checkout.
 */
export async function canonicalRepositoryPath(
  repositoryRoot: string,
  candidate: string,
  label = 'Path',
): Promise<string> {
  const canonicalRoot = await realpath(resolve(repositoryRoot))
  const canonicalCandidate = await realpath(resolve(candidate))
  assertInside(canonicalRoot, canonicalCandidate, label)
  return canonicalCandidate
}
