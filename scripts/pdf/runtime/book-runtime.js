(function bookRuntime() {
  'use strict'

  var RESOURCE_TIMEOUT_MS = 60_000

  function asError(value) {
    if (value instanceof Error) return value
    return new Error(typeof value === 'string' ? value : JSON.stringify(value))
  }

  function describeElement(element) {
    var tag = element.tagName.toLowerCase()
    var id = element.id ? '#' + element.id : ''
    var classes = typeof element.className === 'string' && element.className.trim()
      ? '.' + element.className.trim().split(/\s+/).join('.')
      : ''
    return tag + id + classes
  }

  function withTimeout(promise, label, timeoutMs) {
    var duration = timeoutMs || RESOURCE_TIMEOUT_MS
    return new Promise(function timeoutPromise(resolve, reject) {
      var timer = window.setTimeout(function onTimeout() {
        reject(new Error(label + ' timed out after ' + duration + ' ms'))
      }, duration)

      Promise.resolve(promise).then(
        function onResolved(value) {
          window.clearTimeout(timer)
          resolve(value)
        },
        function onRejected(error) {
          window.clearTimeout(timer)
          reject(error)
        },
      )
    })
  }

  function waitForDom() {
    if (document.readyState !== 'loading') return Promise.resolve()
    return new Promise(function domPromise(resolve) {
      document.addEventListener('DOMContentLoaded', resolve, { once: true })
    })
  }

  function nextPaint(frames) {
    var remaining = frames || 1
    return new Promise(function paintPromise(resolve) {
      function advance() {
        remaining -= 1
        if (remaining <= 0) resolve()
        else window.requestAnimationFrame(advance)
      }
      window.requestAnimationFrame(advance)
    })
  }

  function absoluteUrl(value) {
    return new URL(value, document.baseURI).href
  }

  function waitForStylesheet(link) {
    if (link.sheet) return Promise.resolve()
    return withTimeout(new Promise(function stylesheetPromise(resolve, reject) {
      link.addEventListener('load', resolve, { once: true })
      link.addEventListener('error', function stylesheetError() {
        reject(new Error('Stylesheet failed to load: ' + link.href))
      }, { once: true })
    }), 'Stylesheet ' + link.href)
  }

  async function waitForStylesheets() {
    var links = Array.from(document.querySelectorAll('link[rel~="stylesheet"]'))
    await Promise.all(links.map(waitForStylesheet))
  }

  async function waitForFonts() {
    if (!document.fonts || !document.fonts.ready) {
      throw new Error('Font Loading API is unavailable; cannot guarantee embedded book fonts')
    }

    await withTimeout(document.fonts.ready, 'Document fonts')
    var failed = Array.from(document.fonts).filter(function failedFont(font) {
      return font.status === 'error'
    })
    if (failed.length !== 0) {
      throw new Error(failed.length + ' declared font face(s) failed to load')
    }
  }

  function validateImage(image) {
    if (image.naturalWidth === 0 || image.naturalHeight === 0) {
      var source = image.currentSrc || image.src || '(missing src)'
      throw new Error('Image failed to load: ' + source)
    }
  }

  function waitForImage(image) {
    image.loading = 'eager'
    image.decoding = 'sync'

    var loaded
    if (image.complete) {
      loaded = Promise.resolve()
    } else {
      loaded = new Promise(function imagePromise(resolve, reject) {
        image.addEventListener('load', resolve, { once: true })
        image.addEventListener('error', function imageError() {
          reject(new Error('Image failed to load: ' + (image.currentSrc || image.src || '(missing src)')))
        }, { once: true })
      })
    }

    return withTimeout(loaded, 'Image ' + (image.currentSrc || image.src || '(missing src)'))
      .then(function decodeImage() {
        validateImage(image)
        if (typeof image.decode !== 'function') return undefined
        return image.decode().catch(function decodeError(error) {
          throw new Error('Image decode failed for ' + (image.currentSrc || image.src) + ': ' + asError(error).message)
        })
      })
  }

  async function waitForImages() {
    var images = Array.from(document.images)
    await Promise.all(images.map(waitForImage))
    return images.length
  }

  function decodeMermaidSource(node) {
    if (node.dataset.mermaid) {
      try {
        return decodeURIComponent(node.dataset.mermaid)
      } catch (error) {
        throw new Error('Invalid encoded Mermaid source in ' + describeElement(node) + ': ' + asError(error).message)
      }
    }
    if (node.dataset.bookMermaid) return node.dataset.bookMermaid
    if (node.matches('pre.mermaid')) return node.textContent || ''
    return ''
  }

  async function renderMermaid(assets) {
    var nodes = Array.from(document.querySelectorAll('.mermaid-diagram, pre.mermaid, [data-book-mermaid]'))
      .filter(function uniqueMermaidNode(node, index, all) {
        return all.indexOf(node) === index
      })
    nodes.forEach(function identifyMermaid(node, index) {
      node.dataset.bookMermaidId = 'mermaid-' + (index + 1)
    })
    if (nodes.length === 0) return 0
    if (!assets.mermaidModule) {
      throw new Error('Book contains Mermaid diagrams but no local mermaidModule asset URL was provided')
    }

    var imported = await withTimeout(import(absoluteUrl(assets.mermaidModule)), 'Mermaid module')
    var mermaid = imported.default || imported
    if (!mermaid || typeof mermaid.initialize !== 'function' || typeof mermaid.render !== 'function') {
      throw new Error('The configured Mermaid module does not expose initialize() and render()')
    }

    mermaid.initialize({
      startOnLoad: false,
      securityLevel: 'loose',
      theme: 'neutral',
      deterministicIds: true,
      deterministicIDSeed: document.body.dataset.bookId || 'book',
      flowchart: {
        htmlLabels: true,
        useMaxWidth: true,
        nodeSpacing: 45,
        rankSpacing: 45,
        padding: 12,
      },
    })

    for (var index = 0; index < nodes.length; index += 1) {
      var node = nodes[index]
      if (node.dataset.rendered === 'error') {
        throw new Error('Mermaid diagram was already marked as failed: ' + describeElement(node))
      }
      if (node.dataset.rendered === 'true' && node.querySelector('svg')) continue

      var source = decodeMermaidSource(node).trim()
      if (!source) {
        if (node.querySelector('svg')) {
          node.dataset.rendered = 'true'
          continue
        }
        throw new Error('Mermaid diagram has no source: ' + describeElement(node))
      }

      try {
        var result = await mermaid.render('book-mermaid-' + index, source)
        node.innerHTML = result.svg
        if (!node.querySelector('svg')) throw new Error('renderer returned no SVG')
        node.dataset.rendered = 'true'
      } catch (error) {
        node.dataset.rendered = 'error'
        throw new Error('Mermaid render failed at diagram ' + (index + 1) + ': ' + asError(error).message)
      }
    }

    await nextPaint(2)
    return nodes.length
  }

  function drawioHasOutput(node) {
    if (node.dataset.rendered === 'error') return false
    // .geDiagramContainer appears before its asynchronous XML load finishes;
    // accepting that wrapper would let pagination race the actual graph.
    return Boolean(node.querySelector('svg'))
  }

  function waitForDrawioNode(node, index) {
    return new Promise(function drawioNodePromise(resolve, reject) {
      var settled = false
      var settleTimer
      var timeoutTimer

      function cleanup() {
        observer.disconnect()
        window.clearTimeout(settleTimer)
        window.clearTimeout(timeoutTimer)
      }

      function finish(error) {
        if (settled) return
        settled = true
        cleanup()
        if (error) reject(error)
        else resolve()
      }

      function checkDrawio() {
        if (node.dataset.rendered === 'error') {
          finish(new Error('Drawio diagram ' + (index + 1) + ' was marked as failed'))
          return
        }
        window.clearTimeout(settleTimer)
        if (!drawioHasOutput(node)) return

        // GraphViewer inserts an intermediate SVG and then replaces it while
        // applying the final graph bounds. A mutation-only barrier used to
        // resolve on that transient node. Require a quiet period and re-check
        // that the final SVG is still attached before allowing pagination.
        var candidate = node.querySelector('svg')
        settleTimer = window.setTimeout(function acceptStableSvg() {
          if (candidate && candidate.isConnected && node.contains(candidate) && drawioHasOutput(node)) {
            finish()
          } else {
            checkDrawio()
          }
        }, 250)
      }

      var observer = new MutationObserver(checkDrawio)
      observer.observe(node, { attributes: true, childList: true, subtree: true })
      timeoutTimer = window.setTimeout(function drawioTimeout() {
        finish(new Error('Drawio diagram ' + (index + 1) + ' timed out after ' + RESOURCE_TIMEOUT_MS + ' ms'))
      }, RESOURCE_TIMEOUT_MS)
      checkDrawio()
    })
  }

  function normalizeDrawioLayout(node, index) {
    var svgs = Array.from(node.querySelectorAll('svg'))
    if (svgs.length === 0) {
      throw new Error('drawio GraphViewer produced no SVG for diagram ' + (index + 1))
    }

    svgs.forEach(function constrainDrawioSvg(svg) {
      // viewer-static expresses the source canvas size as min-width/min-height
      // on the SVG (instead of a viewBox). Merely changing width leaves that
      // minimum in force, so a 606 px graph still overflows a 514 px text box.
      // Preserve the source aspect ratio in a viewBox before removing those
      // pixel minima; the printable width can then scale the whole graph.
      var sourceWidth = Number.parseFloat(svg.style.minWidth || node.style.width || '')
      var sourceHeight = Number.parseFloat(svg.style.minHeight || node.style.height || '')
      if (!svg.hasAttribute('viewBox') && sourceWidth > 0 && sourceHeight > 0) {
        svg.setAttribute('viewBox', '0 0 ' + sourceWidth + ' ' + sourceHeight)
      }
      svg.setAttribute('preserveAspectRatio', 'xMidYMid meet')
      svg.style.setProperty('width', '100%', 'important')
      svg.style.setProperty('max-width', '100%', 'important')
      svg.style.setProperty('min-width', '0', 'important')
      svg.style.setProperty('height', 'auto', 'important')
      svg.style.setProperty('min-height', '0', 'important')

      // viewer-static writes the source pixel width onto one or more wrapper
      // elements. Normalize the chain outside the SVG; descendants inside the
      // SVG keep their own coordinate system and label geometry.
      var wrapper = svg.parentElement
      while (wrapper && wrapper !== node) {
        wrapper.style.setProperty('width', '100%', 'important')
        wrapper.style.setProperty('max-width', '100%', 'important')
        wrapper.style.setProperty('min-width', '0', 'important')
        wrapper = wrapper.parentElement
      }
    })
    node.style.setProperty('width', '100%', 'important')
    node.style.setProperty('max-width', '100%', 'important')
    node.style.setProperty('min-width', '0', 'important')
  }

  async function renderDrawio(assets) {
    var nodes = Array.from(document.querySelectorAll('.mxgraph[data-mxgraph], .drawio-diagram[data-mxgraph]'))
      .filter(function uniqueDrawioNode(node, index, all) {
        return all.indexOf(node) === index
      })
    nodes.forEach(function identifyDrawio(node, index) {
      node.dataset.bookDrawioId = 'drawio-' + (index + 1)
    })
    if (nodes.length === 0) return 0
    if (!assets.drawioViewerScript) {
      throw new Error('Book contains drawio diagrams but no local drawioViewerScript asset URL was provided')
    }

    var viewer = window.GraphViewer
    if (!viewer || typeof viewer.processElements !== 'function') {
      throw new Error('The local drawio GraphViewer script did not expose GraphViewer.processElements()')
    }

    var pending = nodes.map(waitForDrawioNode)
    try {
      var processing = viewer.processElements()
      if (processing && typeof processing.then === 'function') await processing
    } catch (error) {
      throw new Error('drawio GraphViewer failed: ' + asError(error).message)
    }
    await Promise.all(pending)

    nodes.forEach(function markDrawioReady(node, index) {
      normalizeDrawioLayout(node, index)
      node.dataset.rendered = 'true'
    })
    await nextPaint(2)
    return nodes.length
  }

  function validateInternalTargets() {
    var missing = []
    var links = document.querySelectorAll('.book-toc a[href^="#"], .book-body a[href^="#"]')
    links.forEach(function validateLink(link) {
      var raw = link.getAttribute('href')
      if (!raw || raw === '#') return
      var id
      try {
        id = decodeURIComponent(raw.slice(1))
      } catch (error) {
        missing.push(raw + ' (invalid URL encoding)')
        return
      }
      if (!document.getElementById(id)) missing.push(raw)
    })
    if (missing.length !== 0) {
      throw new Error('Missing internal target(s): ' + missing.slice(0, 12).join(', ') + (missing.length > 12 ? ', …' : ''))
    }
  }

  function rejectRenderedErrors() {
    var failed = Array.from(document.querySelectorAll('[data-rendered="error"]'))
    if (failed.length !== 0) {
      throw new Error('Rendered asset error(s): ' + failed.slice(0, 8).map(describeElement).join(', '))
    }
  }

  function sourceDocumentSentinels() {
    var documents = Array.from(document.querySelectorAll('#book-source [data-document-id]'))
    var declaredCount = Number.parseInt(document.body.dataset.sourceDocumentCount || '', 10)
    if (!Number.isSafeInteger(declaredCount) || declaredCount !== documents.length) {
      throw new Error(
        'Source document count mismatch before pagination: declared=' + declaredCount + ', DOM=' + documents.length,
      )
    }

    var ids = documents.map(function documentId(node) { return node.dataset.documentId || '' })
    if (ids.some(function missingId(id) { return !id })) {
      throw new Error('A source document is missing data-document-id')
    }
    if (new Set(ids).size !== ids.length) throw new Error('Duplicate data-document-id before pagination')

    var sentinels = new Set(Array.from(document.querySelectorAll('#book-source [data-document-sentinel]'))
      .map(function sentinelId(node) { return node.dataset.documentSentinel || '' }))
    var missing = ids.filter(function missingSentinel(id) { return !sentinels.has(id) })
    if (missing.length !== 0 || sentinels.size !== ids.length) {
      throw new Error('Source document sentinel mismatch before pagination: ' + missing.slice(0, 8).join(', '))
    }
    return ids
  }

  function assertDocumentSentinels(expectedIds) {
    var rendered = Array.from(document.querySelectorAll('.pagedjs_pages [data-document-sentinel]'))
    var byId = new Map()
    rendered.forEach(function collectSentinel(node) {
      var id = node.dataset.documentSentinel || ''
      if (!byId.has(id)) byId.set(id, [])
      byId.get(id).push(node)
    })

    var failures = []
    expectedIds.forEach(function validateSentinel(id) {
      var fragments = byId.get(id) || []
      if (fragments.length === 0) {
        failures.push(id + ' missing')
        return
      }
      if (!fragments.some(function positionedSentinel(node) {
        var content = node.closest('.pagedjs_page_content')
        if (!content) return false
        var rect = node.getBoundingClientRect()
        var bounds = content.getBoundingClientRect()
        return rect.width > 1 && rect.top >= bounds.top - 2 && rect.top <= bounds.bottom + 2
      })) failures.push(id + ' has no positioned tail marker')
    })
    if (byId.size !== expectedIds.length) {
      failures.push('expected ' + expectedIds.length + ' sentinel id(s), found ' + byId.size)
    }
    if (failures.length !== 0) {
      throw new Error(
        'Paged output document-tail integrity failed: ' + failures.slice(0, 12).join(', '),
      )
    }
  }

  function visiblyRendered(element) {
    if (typeof element.checkVisibility === 'function') {
      try {
        if (!element.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true })) return false
      } catch (_error) {
        // Older Chromium versions accept no options; use the style fallback.
        if (!element.checkVisibility()) return false
      }
    } else {
      var style = window.getComputedStyle(element)
      if (style.display === 'none' || style.visibility === 'hidden' || Number(style.opacity) === 0) return false
    }
    var rect = element.getBoundingClientRect()
    if (rect.width <= 1) return false
    if (rect.height > 1) return true
    if (element.tagName.toLowerCase() !== 'hr') return false
    var hrStyle = window.getComputedStyle(element)
    return ['borderTopWidth', 'borderBottomWidth'].some(function visibleBorder(property) {
      return Number.parseFloat(hrStyle[property]) > 0
    })
  }

  function assertNoBlankPages() {
    var blank = []
    var pages = Array.from(document.querySelectorAll('.pagedjs_page'))
    pages.forEach(function inspectPage(page, index) {
      var content = page.querySelector('.pagedjs_page_content')
      if (!content) {
        blank.push(index + 1 + ' (missing page content box)')
        return
      }
      var text = (content.innerText || '').replace(/[\s\u200b\ufeff]+/gu, '')
      var visual = Array.from(content.querySelectorAll(
        'img, svg, canvas, video, iframe, table, hr, .book-cover__frame, .mermaid-diagram, .mxgraph, .drawio-diagram',
      )).some(visiblyRendered)
      if (!text && !visual) blank.push(String(index + 1))
    })
    if (blank.length !== 0) throw new Error('Blank paged output page(s): ' + blank.slice(0, 12).join(', '))
  }

  function assertMermaidOutput(expectedCount) {
    if (expectedCount === 0) return

    var rendered = Array.from(document.querySelectorAll('.pagedjs_pages [data-book-mermaid-id]'))
    var byId = new Map()
    rendered.forEach(function collectMermaid(node) {
      var id = node.dataset.bookMermaidId || ''
      if (!byId.has(id)) byId.set(id, [])
      byId.get(id).push(node)
    })

    var failures = []
    for (var index = 0; index < expectedCount; index += 1) {
      var id = 'mermaid-' + (index + 1)
      var fragments = byId.get(id) || []
      var svgs = fragments.flatMap(function mermaidSvgs(node) {
        return Array.from(node.querySelectorAll('svg'))
      })
      if (fragments.length === 0) {
        failures.push(id + ' missing from paged output')
      } else if (svgs.length === 0) {
        failures.push(id + ' contains no SVG in paged output')
      } else if (!svgs.some(visiblyRendered)) {
        failures.push(id + ' has no visible non-zero SVG in paged output')
      }
    }
    if (byId.size !== expectedCount) {
      failures.push('expected ' + expectedCount + ' Mermaid id(s), found ' + byId.size)
    }
    if (failures.length !== 0) throw new Error('Mermaid pagination integrity failed: ' + failures.join('; '))
  }

  function assertDrawioOutput(expectedCount) {
    if (expectedCount === 0) return

    var rendered = Array.from(document.querySelectorAll('.pagedjs_pages [data-book-drawio-id]'))
    var byId = new Map()
    rendered.forEach(function collectDrawio(node) {
      var id = node.dataset.bookDrawioId || ''
      if (!byId.has(id)) byId.set(id, [])
      byId.get(id).push(node)
    })

    var failures = []
    for (var index = 0; index < expectedCount; index += 1) {
      var id = 'drawio-' + (index + 1)
      var fragments = byId.get(id) || []
      var svgs = fragments.flatMap(function drawioSvgs(node) {
        return Array.from(node.querySelectorAll('svg'))
      })
      if (fragments.length === 0) {
        failures.push(id + ' missing from paged output')
      } else if (svgs.length === 0) {
        failures.push(id + ' contains no SVG in paged output')
      } else if (!svgs.some(visiblyRendered)) {
        failures.push(id + ' has no visible non-zero SVG in paged output')
      }
    }
    if (byId.size !== expectedCount) {
      failures.push('expected ' + expectedCount + ' drawio id(s), found ' + byId.size)
    }
    if (failures.length !== 0) throw new Error('Drawio pagination integrity failed: ' + failures.join('; '))
  }

  function assertNoHorizontalOverflow() {
    var failures = []
    var selector = [
      '.book-cover', '.book-toc', '.toc-entry a',
      '.book-content p', '.book-content li', '.book-content h1', '.book-content h2',
      '.book-content h3', '.book-content h4', '.book-content h5', '.book-content h6',
      '.book-content a', '.book-content pre', '.book-content table', '.book-content img',
      '.book-content svg', '.book-content canvas', '.book-content video', '.book-content iframe',
      '.book-content figure', '.book-content .mermaid-diagram', '.book-content .mxgraph',
      '.book-content .drawio-diagram', '.book-content [class*="language-"]',
      '.book-content .custom-block', '.book-content .reference-card',
      '.book-content .talk-info-card', '.book-content .online-demo', '.document-endnotes',
    ].join(', ')

    var pages = Array.from(document.querySelectorAll('.pagedjs_page'))
    for (var pageIndex = 0; pageIndex < pages.length && failures.length < 12; pageIndex += 1) {
      var content = pages[pageIndex].querySelector('.pagedjs_page_content')
      if (!content) continue
      var bounds = content.getBoundingClientRect()
      var candidates = content.querySelectorAll(selector)
      for (var index = 0; index < candidates.length && failures.length < 12; index += 1) {
        var candidate = candidates[index]
        // Descendants of an SVG use their own coordinate system; the outer SVG
        // is the printable box whose bounds matter here.
        if (candidate.ownerSVGElement) continue
        var rect = candidate.getBoundingClientRect()
        if (rect.width === 0 || rect.height === 0) continue
        // Paged.js lays fragmented continuations in adjacent page columns. For
        // a block spanning several pages, getBoundingClientRect() is the union
        // of those columns and therefore looks extremely wide even when every
        // fragment fits. clientWidth/scrollWidth describe the actual fragment
        // box and avoid that false positive.
        var boxTooWide = candidate instanceof HTMLElement
          ? candidate.clientWidth > content.clientWidth + 2
          : rect.width > bounds.width + 2
        var hasScrollOverflow = candidate instanceof HTMLElement
          && candidate.clientWidth > 0
          && candidate.scrollWidth > candidate.clientWidth + 2
        if (boxTooWide || hasScrollOverflow) {
          var style = window.getComputedStyle(candidate)
          failures.push(
            'page ' + (pageIndex + 1) + ' ' + describeElement(candidate)
            + ' (left=' + rect.left.toFixed(1) + ', right=' + rect.right.toFixed(1)
            + ', page=' + bounds.left.toFixed(1) + '..' + bounds.right.toFixed(1)
            + ', client/scroll=' + candidate.clientWidth + '/' + candidate.scrollWidth
            + ', css-width=' + style.width + ', max=' + style.maxWidth
            + ', white-space=' + style.whiteSpace + ', overflow-wrap=' + style.overflowWrap + ')',
          )
        }
      }
    }

    if (failures.length !== 0) {
      throw new Error('Horizontal overflow after pagination:\n' + failures.join('\n'))
    }
  }

  function assertNoVerticalOverflow() {
    var failures = []
    var avoidSelector = [
      '.book-content figure', '.book-content img', '.book-content > svg',
      '.book-content canvas', '.book-content .mermaid-diagram',
      '.book-content .mxgraph', '.book-content .drawio-diagram',
      '.book-content math[display="block"]', '.book-content mjx-container[display="true"]',
      '.book-content .math-display', '.book-content .katex-display',
      '.book-content .reference-card li', '.book-content tr', '.book-content .online-demo-note',
    ].join(', ')
    var pages = Array.from(document.querySelectorAll('.pagedjs_page'))

    for (var pageIndex = 0; pageIndex < pages.length && failures.length < 12; pageIndex += 1) {
      var content = pages[pageIndex].querySelector('.pagedjs_page_content')
      if (!content) continue
      var bounds = content.getBoundingClientRect()
      if (content.clientHeight > 0 && content.scrollHeight > content.clientHeight + 2) {
        failures.push(
          'page ' + (pageIndex + 1) + ' content scrollHeight/clientHeight='
          + content.scrollHeight + '/' + content.clientHeight,
        )
      }

      var clipped = Array.from(content.querySelectorAll('*')).filter(function clipsOwnContent(element) {
        if (!(element instanceof HTMLElement) || element.clientHeight <= 0) return false
        var overflowY = window.getComputedStyle(element).overflowY
        return (overflowY === 'hidden' || overflowY === 'clip')
          && element.scrollHeight > element.clientHeight + 2
      })
      clipped.slice(0, 12 - failures.length).forEach(function reportClipping(element) {
        failures.push(
          'page ' + (pageIndex + 1) + ' ' + describeElement(element)
          + ' clips vertical content (' + element.scrollHeight + '/' + element.clientHeight + ')',
        )
      })

      var candidates = content.querySelectorAll(avoidSelector)
      for (var index = 0; index < candidates.length && failures.length < 12; index += 1) {
        var candidate = candidates[index]
        if (candidate.ownerSVGElement || !visiblyRendered(candidate)) continue
        var rect = candidate.getBoundingClientRect()
        if (rect.top < bounds.top - 2 || rect.bottom > bounds.bottom + 2) {
          failures.push(
            'page ' + (pageIndex + 1) + ' ' + describeElement(candidate)
            + ' exceeds vertical page box (' + rect.top.toFixed(1) + '..' + rect.bottom.toFixed(1)
            + ' vs ' + bounds.top.toFixed(1) + '..' + bounds.bottom.toFixed(1) + ')',
          )
        }
      }
    }

    if (failures.length !== 0) {
      throw new Error('Vertical overflow or clipping after pagination:\n' + failures.join('\n'))
    }
  }

  function assertTocPageNumbers() {
    var declared = Number.parseInt(document.body.dataset.tocEntryCount || '', 10)
    var links = Array.from(document.querySelectorAll('.pagedjs_pages .book-toc .toc-entry a[href^="#"]'))
    if (!Number.isSafeInteger(declared) || declared < 0 || links.length !== declared) {
      throw new Error('TOC entry count mismatch after pagination: declared=' + declared + ', DOM=' + links.length)
    }

    var pages = Array.from(document.querySelectorAll('.pagedjs_page'))
    var failures = []
    links.forEach(function validateTocPage(link) {
      var raw = link.getAttribute('href') || ''
      var id
      try {
        id = decodeURIComponent(raw.slice(1))
      } catch (_error) {
        failures.push(raw + ' has invalid URL encoding')
        return
      }
      var target = document.querySelector('.pagedjs_pages #' + CSS.escape(id))
      var targetPage = target && target.closest('.pagedjs_page')
      var physicalPage = targetPage ? pages.indexOf(targetPage) + 1 : 0
      var counterAttribute = Array.from(link.attributes).find(function targetCounterAttribute(attribute) {
        return attribute.name.indexOf('data-target-counter-') === 0
      })
      var pseudoStyle = window.getComputedStyle(link, '::after')
      var counterName = counterAttribute ? counterAttribute.name.slice('data-'.length) : ''
      var escapedCounterName = counterName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
      var resetMatch = counterName
        ? new RegExp('(?:^|\\s)' + escapedCounterName + '\\s+(-?\\d+)(?:\\s|$)').exec(pseudoStyle.counterReset)
        : null
      var resolvedPage = resetMatch ? Number(resetMatch[1]) : Number.NaN
      if (!target || physicalPage < 1) {
        failures.push(raw + ' has no paged target')
      } else if (!counterAttribute) {
        failures.push(raw + ' has no resolved Paged.js target-counter marker')
      } else if (!Number.isSafeInteger(resolvedPage) || resolvedPage < 1) {
        failures.push(
          raw + ' has no numeric target-counter reset in ' + JSON.stringify(pseudoStyle.counterReset),
        )
      } else if (resolvedPage !== physicalPage) {
        failures.push(raw + ' says page ' + resolvedPage + ' but target is on physical page ' + physicalPage)
      }
    })
    if (failures.length !== 0) {
      throw new Error('TOC page-number integrity failed: ' + failures.slice(0, 12).join('; '))
    }
  }

  async function paginate(expectedDocumentIds, expectedMermaidCount, expectedDrawioCount) {
    var polyfill = window.PagedPolyfill
    if (!polyfill || typeof polyfill.preview !== 'function') {
      throw new Error('The local Paged.js script did not expose PagedPolyfill.preview()')
    }
    if (document.querySelector('.pagedjs_pages, .pagedjs_page')) {
      throw new Error('Paged.js output already exists before explicit preview(); auto pagination must remain disabled')
    }

    document.body.dataset.readyState = 'paginating'
    var flow = await polyfill.preview()
    await nextPaint(2)

    var pageCount = document.querySelectorAll('.pagedjs_page').length
    var reportedTotal = Number(flow && flow.total)
    if (pageCount < 1) throw new Error('Paged.js completed without producing any pages')
    if (Number.isFinite(reportedTotal) && reportedTotal > 0 && reportedTotal !== pageCount) {
      throw new Error('Paged.js page count mismatch: flow.total=' + reportedTotal + ', DOM=' + pageCount)
    }

    assertDocumentSentinels(expectedDocumentIds)
    assertMermaidOutput(expectedMermaidCount)
    assertDrawioOutput(expectedDrawioCount)
    assertNoBlankPages()
    assertNoHorizontalOverflow()
    assertNoVerticalOverflow()
    assertTocPageNumbers()

    document.documentElement.dataset.pageCount = String(pageCount)
    document.body.dataset.pageCount = String(pageCount)
    return pageCount
  }

  async function buildBook() {
    await waitForDom()
    var assets = window.__BOOK_ASSETS__ || {}
    window.PagedConfig = Object.assign({}, window.PagedConfig, { auto: false })

    document.body.dataset.readyState = 'loading-assets'
    await waitForStylesheets()
    await waitForFonts()
    var initialImages = await waitForImages()

    document.body.dataset.readyState = 'rendering-diagrams'
    var mermaidCount = await renderMermaid(assets)
    var drawioCount = await renderDrawio(assets)

    // Diagram renderers can add images and request additional font glyphs.
    var finalImages = await waitForImages()
    await waitForFonts()
    await nextPaint(2)
    rejectRenderedErrors()
    validateInternalTargets()
    var expectedDocumentIds = sourceDocumentSentinels()

    var pageCount = await paginate(expectedDocumentIds, mermaidCount, drawioCount)
    document.body.dataset.readyState = 'ready'
    document.body.dataset.mermaidCount = String(mermaidCount)
    document.body.dataset.drawioCount = String(drawioCount)
    document.body.dataset.imageCount = String(Math.max(initialImages, finalImages))

    return {
      pageCount: pageCount,
      mermaidCount: mermaidCount,
      drawioCount: drawioCount,
      imageCount: Math.max(initialImages, finalImages),
    }
  }

  // Puppeteer awaits this exact promise. Every asynchronous prerequisite and
  // pagination failure propagates through it instead of becoming a console-only
  // warning or a partially rendered PDF.
  window.__BOOK_READY__ = buildBook().catch(function bookFailure(value) {
    var error = asError(value)
    if (document.body) {
      document.body.dataset.readyState = 'error'
      document.body.dataset.readyError = (error.stack || error.message).slice(0, 2_000)
    }
    throw error
  })
}())
