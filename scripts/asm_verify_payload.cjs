// node 无头驱动 asm-playground 解释器：node asm-node-engine.cjs <worker.js> <payload.json> [maxSteps]
// 用途：不开浏览器验证 payload 可执行、断言终态寄存器
const { Worker } = require('node:worker_threads')
const { readFileSync } = require('node:fs')

const [, , workerPath, payloadPath, maxArg] = process.argv
const maxSteps = parseInt(maxArg || '200', 10)
const payload = JSON.parse(readFileSync(payloadPath, 'utf8'))

const worker = new Worker(`
  const { parentPort } = require('node:worker_threads');
  globalThis.self = globalThis;
  globalThis.postMessage = (m) => parentPort.postMessage(m);
  parentPort.on('message', (m) => { if (globalThis.onmessage) globalThis.onmessage({ data: m }); });
  require(${JSON.stringify(workerPath)});
`, { eval: true })

let last = null
const finish = (code, label) => { console.log(label, JSON.stringify(last && { status: last.status, steps: last.steps, reason: last.reason, regs: last.regs })); worker.terminate(); process.exit(code) }
worker.on('message', (data) => {
  if (data.type === 'error') return finish(1, 'ENGINE-ERROR: ' + data.error)
  if (data.type === 'snapshot') {
    last = data.snapshot
    if (last.status === 'finished') return finish(0, 'FINISHED')
    if (last.status === 'error') return finish(1, 'STATUS-ERROR')
    if ((last.steps ?? 0) >= maxSteps) return finish(2, 'STEP-LIMIT')
    setTimeout(() => worker.postMessage({ type: 'step' }), 5)
  }
})
worker.postMessage({ type: 'init', payload })
setTimeout(() => finish(3, 'TIMEOUT'), 30000)
