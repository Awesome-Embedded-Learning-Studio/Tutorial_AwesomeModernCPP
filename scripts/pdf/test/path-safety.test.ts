import assert from 'node:assert/strict'
import { mkdir, mkdtemp, realpath, rm, symlink, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import test from 'node:test'

import { canonicalRepositoryPath } from '../path-safety'

test('accepts canonical files contained in the repository', async (context) => {
  const root = await mkdtemp(join(tmpdir(), 'pdf-path-safe-'))
  context.after(() => rm(root, { recursive: true, force: true }))
  const directory = join(root, 'assets')
  const file = join(directory, 'figure.svg')
  await mkdir(directory)
  await writeFile(file, '<svg/>')

  assert.equal(await canonicalRepositoryPath(root, file), await realpath(file))
})

test('rejects a repository symlink targeting a file outside the repository', async (context) => {
  const root = await mkdtemp(join(tmpdir(), 'pdf-path-root-'))
  const outside = await mkdtemp(join(tmpdir(), 'pdf-path-outside-'))
  context.after(() => Promise.all([
    rm(root, { recursive: true, force: true }),
    rm(outside, { recursive: true, force: true }),
  ]))
  const secret = join(outside, 'secret.txt')
  const link = join(root, 'leak.txt')
  await writeFile(secret, 'must not be published')
  await symlink(secret, link)

  await assert.rejects(
    canonicalRepositoryPath(root, link, 'Fixture path'),
    /Fixture path escapes the repository/,
  )
})
