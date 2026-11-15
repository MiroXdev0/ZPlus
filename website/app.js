(() => {
  const { course, docs, wiki, examples } = window.ZPlusSite;
  const main = document.querySelector("#main");
  const footer = document.querySelector("#site-footer");
  const search = document.querySelector("#site-search");
  const searchResults = document.querySelector("#search-results");
  const releasesUrl = "https://github.com/MiroXdev0/ZPluse-release/releases";
  const releasesApi = "https://api.github.com/repos/MiroXdev0/ZPluse-release/releases?per_page=100";
  const releaseAssetPrefix = "/MiroXdev0/ZPluse-release/releases/download/";
  const downloadTargets = [{
    id: "windows-x86_64",
    label: "Windows x86_64",
    filePattern: /\.exe$/i,
    installerPattern: /\b(?:setup|installer)\b/i,
    platformPattern: /\b(?:windows|win64)\b/i,
    architecturePattern: /\b(?:x64|amd64|x86\s+64)\b/i,
    conflictPattern: /\b(?:linux|macos|darwin|osx|arm64|aarch64|i386|i686|win32|x86\s+32)\b/i
  }];
  const progressKey = "zplus-learning-progress-v1";
  let releaseRequest;
  let registrySearchTimer;
  let registrySearchSequence = 0;

  const lessons = course.flatMap((module, moduleIndex) =>
    module.lessons.map((lesson, lessonIndex) => ({
      ...lesson,
      module: module.title,
      moduleIndex,
      lessonIndex,
      number: course.slice(0, moduleIndex).reduce((sum, item) => sum + item.lessons.length, 0) + lessonIndex + 1,
      id: `${String(course.slice(0, moduleIndex).reduce((sum, item) => sum + item.lessons.length, 0) + lessonIndex + 1).padStart(2, "0")}-${slug(lesson.title)}`
    }))
  );
  const lessonById = new Map(lessons.map(lesson => [lesson.id, lesson]));
  const contentIndex = [
    ...docs.map(item => ({ title: item.title, summary: item.summary, href: `/docs/${item.id}`, type: "Documentation" })),
    ...wiki.map(item => ({ title: item.title, summary: item.summary, href: `/wiki/${item.id}`, type: "Wiki" })),
    ...lessons.map(item => ({ title: item.title, summary: item.explain, href: `/learn/${item.id}`, type: `Lesson ${item.number}` })),
    ...examples.map(item => ({ title: item.title, summary: item.summary, href: `/examples#${item.id}`, type: "Example" }))
  ];

  function slug(value) {
    return value.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
  }

  function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, char => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
    })[char]);
  }

  function inlineMarkup(value) {
    return String(value).split("`").map((part, index) =>
      index % 2 ? `<code>${escapeHtml(part)}</code>` : escapeHtml(part)
    ).join("");
  }

  function highlighted(code) {
    const token = /(\/\/[^\n]*|"(?:\\.|[^"\\])*"|\b(?:start|stop|let|const|fn|return|if|else|while|for|in|end|repeat|try|catch|throw|use|as|pub|struct|class|true|false|and|or|not)\b|\b\d+(?:\.\d+)?\b)/g;
    let result = "";
    let offset = 0;
    for (const match of code.matchAll(token)) {
      result += escapeHtml(code.slice(offset, match.index));
      const value = match[0];
      const kind = value.startsWith("//") ? "comment" : value.startsWith('"') ? "string" : /^\d/.test(value) ? "number" : "keyword";
      result += `<span class="syntax-${kind}">${escapeHtml(value)}</span>`;
      offset = match.index + value.length;
    }
    return result + escapeHtml(code.slice(offset));
  }

  function codeBlock(code, label = "Z+") {
    return `<div class="code-frame"><div class="code-toolbar"><span>${escapeHtml(label)}</span><button class="copy-code" type="button" data-copy="${escapeHtml(code)}">Copy</button></div><pre><code>${highlighted(code)}</code></pre></div>`;
  }

  function path() {
    const queryRoute = new URLSearchParams(location.search).get("route");
    if (queryRoute) {
      const original = new URL(queryRoute, location.origin);
      history.replaceState(null, "", original.pathname + original.search + original.hash);
      return original.pathname.replace(/\/+$/, "") || "/";
    }
    return location.pathname.replace(/\/+$/, "") || "/";
  }

  function link(href, text, className = "") {
    return `<a class="${className}" href="${href}">${text}</a>`;
  }

  function buttonLink(href, text, className = "button button-primary") {
    return link(href, text, className);
  }

  function getProgress() {
    try {
      const value = JSON.parse(localStorage.getItem(progressKey) || "[]");
      return new Set(Array.isArray(value) ? value.filter(id => lessonById.has(id)) : []);
    } catch (error) {
      console.warn("Unable to read saved Z+ learning progress.", error);
      return new Set();
    }
  }

  function saveProgress(progress) {
    try {
      localStorage.setItem(progressKey, JSON.stringify([...progress]));
      return true;
    } catch (error) {
      console.warn("Unable to save Z+ learning progress.", error);
      return false;
    }
  }

  function renderFooter() {
    footer.innerHTML = `<div class="footer-inner">
      <a class="brand" href="/"><img class="brand-logo" src="/assets/zplus-logo-dark.png" alt=""><span class="brand-word">Language</span></a>
      <p>A programming language by Miroslav Aleksandrov.</p>
      <div class="footer-links">
        ${link("/download", "Download")}
        ${link("/learn", "Learn")}
        ${link("/docs", "Documentation")}
        ${link("/wiki", "Wiki")}
        <a href="${releasesUrl}" target="_blank" rel="noreferrer">GitHub Releases ↗</a>
      </div>
      <small>Language details are grounded in the checked-in compiler documentation. Check the release page for current published builds.</small>
    </div>`;
  }

  function homePage() {
    return `<section class="hero">
      <div class="hero-copy">
        <p class="eyebrow"><span class="live-dot"></span> A language with its own compiler, interpreter, and build system</p>
        <h1>Build with <span>Z+.</span></h1>
        <p class="hero-lede">A clear, general-purpose programming language with an explicit compiler pipeline, practical standard library, and room to grow from a first program into a real project.</p>
        <div class="hero-actions">${buttonLink("/download", "Get Z+")} ${buttonLink("/learn", "Start learning", "button button-secondary")}</div>
        <p class="hero-note">Release availability is checked live on the download page. The documentation identifies backend and platform limits.</p>
      </div>
      <div class="hero-code">${codeBlock(`start {
    fn greet(name: string): string {
        return "Hello, " + name
    } end fn

    say(greet("world"))
} stop`)}<div class="terminal-line"><span>›</span> zp run hello.zp <b>·</b> Hello, world!</div></div>
    </section>
    <section class="section-block">
      <div class="section-heading"><p class="eyebrow">The language</p><h2>Small steps, real tools.</h2><p>Z+ brings its compiler, interpreter, native backend, standard library, and ZBuild into one development workflow.</p></div>
      <div class="feature-grid">
        <article class="feature"><span class="feature-index">01</span><h3>Explicit structure</h3><p>Readable brace blocks pair with named terminators, and types help catch mistakes before a program runs.</p></article>
        <article class="feature"><span class="feature-index">02</span><h3>Two execution paths</h3><p>Run source through the interpreter or build a native executable for supported constructs and platforms.</p></article>
        <article class="feature"><span class="feature-index">03</span><h3>Useful foundations</h3><p>Use modules, dynamic arrays, filesystem tools, text utilities, and a project build system as your programs grow.</p></article>
      </div>
    </section>
    <section class="section-block split-section">
      <div><p class="eyebrow">Made to explore</p><h2>Start with one file.<br>Keep going from there.</h2><p>Follow a 52-lesson course from the first instruction to modules, file-backed apps, and release builds. Each step includes real Z+ syntax and a small exercise.</p>${buttonLink("/learn", "Explore the course", "text-link")}</div>
      <div class="project-list">
        ${examples.slice(1, 5).map((item, index) => `<a href="/examples#${item.id}"><span>0${index + 1}</span><div><strong>${escapeHtml(item.title)}</strong><small>${escapeHtml(item.summary)}</small></div><b>↗</b></a>`).join("")}
      </div>
    </section>
    <section class="section-block resource-strip">
      <div><p class="eyebrow">The reference shelf</p><h2>Learn the rules.<br>See what is implemented.</h2></div>
      <div class="resource-links">${link("/docs", "Technical documentation", "resource-link")} ${link("/wiki", "Wiki & practical guides", "resource-link")} ${link("/examples", "Runnable examples", "resource-link")}</div>
    </section>`;
  }

  function loadingPanel(text) {
    return `<div class="state-panel" role="status"><span class="spinner"></span><p>${escapeHtml(text)}</p></div>`;
  }

  function nextReleasePage(response) {
    const header = response.headers.get("Link") || "";
    const match = header.match(/<([^>]+)>;\s*rel="next"/);
    if (!match) return null;
    const next = new URL(match[1]);
    if (next.origin !== "https://api.github.com" || next.pathname !== "/repos/MiroXdev0/ZPluse-release/releases") {
      throw new Error("GitHub returned an unexpected Releases pagination link.");
    }
    return next.href;
  }

  async function fetchReleasePages() {
    const releases = [];
    const visited = new Set();
    let url = releasesApi;
    while (url && !visited.has(url)) {
      visited.add(url);
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 15000);
      let response;
      try {
        response = await fetch(url, {
          headers: {
            Accept: "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28"
          },
          cache: "default",
          signal: controller.signal
        });
      } catch (error) {
        if (controller.signal.aborted) {
          throw new Error("The GitHub Releases request timed out. Check your connection or browse the official Releases page.");
        }
        throw new Error(`Could not connect to GitHub Releases: ${error.message}`);
      } finally {
        clearTimeout(timeout);
      }
      if (!response.ok) {
        const remaining = response.headers.get("X-RateLimit-Remaining");
        if (response.status === 429 || response.status === 403 && remaining === "0") {
          throw new Error("GitHub's unauthenticated API rate limit was reached. Try again later or browse the official Releases page.");
        }
        if (response.status === 403) {
          throw new Error("GitHub denied the Releases API request (HTTP 403), possibly due to its unauthenticated rate limit.");
        }
        throw new Error(`GitHub returned HTTP ${response.status} while loading releases.`);
      }
      const page = await response.json();
      if (!Array.isArray(page)) throw new Error("The GitHub Releases response was not a release list.");
      releases.push(...page.filter(release => release && !release.draft && typeof release.tag_name === "string"));
      url = nextReleasePage(response);
    }
    return releases;
  }

  async function fetchReleases() {
    if (!releaseRequest) {
      releaseRequest = fetchReleasePages().catch(error => {
        releaseRequest = undefined;
        throw error;
      });
    }
    return releaseRequest;
  }

  function parseVersionTag(tag) {
    const match = String(tag || "").match(/^v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$/);
    if (!match) return null;
    return {
      major: Number(match[1]),
      minor: Number(match[2]),
      patch: Number(match[3]),
      prerelease: match[4] ? match[4].split(".") : []
    };
  }

  function compareVersionTags(leftTag, rightTag) {
    const left = parseVersionTag(leftTag);
    const right = parseVersionTag(rightTag);
    if (!left || !right) return null;
    for (const component of ["major", "minor", "patch"]) {
      if (left[component] !== right[component]) return left[component] < right[component] ? -1 : 1;
    }
    if (!left.prerelease.length || !right.prerelease.length) {
      if (left.prerelease.length !== right.prerelease.length) return left.prerelease.length ? -1 : 1;
      return 0;
    }
    const count = Math.max(left.prerelease.length, right.prerelease.length);
    for (let index = 0; index < count; index += 1) {
      if (left.prerelease[index] === undefined) return -1;
      if (right.prerelease[index] === undefined) return 1;
      const leftNumeric = /^\d+$/.test(left.prerelease[index]);
      const rightNumeric = /^\d+$/.test(right.prerelease[index]);
      if (leftNumeric && rightNumeric) {
        const a = Number(left.prerelease[index]);
        const b = Number(right.prerelease[index]);
        if (a !== b) return a < b ? -1 : 1;
      } else if (leftNumeric !== rightNumeric) {
        return leftNumeric ? -1 : 1;
      } else if (left.prerelease[index] !== right.prerelease[index]) {
        return left.prerelease[index] < right.prerelease[index] ? -1 : 1;
      }
    }
    return 0;
  }

  function releaseTimestamp(release) {
    const timestamp = Date.parse(release.published_at || release.created_at || "");
    return Number.isFinite(timestamp) ? timestamp : 0;
  }

  function compareReleasesNewestFirst(left, right) {
    const versionOrder = compareVersionTags(left.tag_name, right.tag_name);
    if (versionOrder !== null && versionOrder !== 0) return -versionOrder;
    if (versionOrder === null && parseVersionTag(left.tag_name)) return -1;
    if (versionOrder === null && parseVersionTag(right.tag_name)) return 1;
    return releaseTimestamp(right) - releaseTimestamp(left);
  }

  function githubAssetUrl(asset) {
    if (!asset || typeof asset.browser_download_url !== "string" ||
        asset.state && asset.state !== "uploaded") return null;
    try {
      const url = new URL(asset.browser_download_url);
      if (url.protocol !== "https:" || url.hostname !== "github.com" ||
          !url.pathname.startsWith(releaseAssetPrefix)) return null;
      return url.href;
    } catch {
      return null;
    }
  }

  function validReleasePageUrl(release) {
    if (typeof release.html_url !== "string") return releasesUrl;
    try {
      const url = new URL(release.html_url);
      return url.protocol === "https:" && url.hostname === "github.com" &&
        url.pathname.startsWith("/MiroXdev0/ZPluse-release/releases/tag/")
        ? url.href
        : releasesUrl;
    } catch {
      return releasesUrl;
    }
  }

  function normalizedPlatformText(value) {
    return String(value || "").toLowerCase().replace(/[^a-z0-9]+/g, " ").trim();
  }

  function findPlatformAsset(release, platform) {
    const assets = Array.isArray(release.assets) ? release.assets : [];
    const releaseText = normalizedPlatformText(`${release.name || ""} ${release.body || ""}`);
    return assets.find(asset => {
      const name = normalizedPlatformText(asset.name);
      const url = githubAssetUrl(asset);
      if (!name || !url || !platform.filePattern.test(asset.name) ||
          !platform.installerPattern.test(name) || platform.conflictPattern.test(name)) return false;
      const combined = `${releaseText} ${name}`;
      return platform.platformPattern.test(combined) && platform.architecturePattern.test(combined);
    }) || null;
  }

  function releaseAssets(release) {
    return (Array.isArray(release.assets) ? release.assets : [])
      .filter(asset => typeof asset.name === "string" && githubAssetUrl(asset));
  }

  function formatBytes(size) {
    if (!Number.isFinite(size) || size < 0) return "Size unavailable";
    if (size < 1024) return `${size} B`;
    if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`;
    return `${(size / (1024 * 1024)).toFixed(1)} MB`;
  }

  function formatReleaseDate(release) {
    const timestamp = releaseTimestamp(release);
    return timestamp
      ? new Date(timestamp).toLocaleDateString(undefined, { year: "numeric", month: "long", day: "numeric" })
      : "Publication date unavailable";
  }

  function releaseDetails(release, latestStable) {
    const target = downloadTargets[0];
    const asset = findPlatformAsset(release, target);
    const assets = releaseAssets(release);
    const title = release.name && release.name !== release.tag_name ? release.name : "";
    const stableLabel = release.prerelease ? "Prerelease" : release.id === latestStable?.id ? "Latest stable" : "Stable release";
    const assetList = assets.length
      ? `<div class="asset-list">${assets.map(item => `<a class="asset-link ${item === asset ? "asset-compatible" : ""}" href="${escapeHtml(githubAssetUrl(item))}" target="_blank" rel="noreferrer"><span class="download-symbol">↓</span><span><strong>${escapeHtml(item.name)}</strong><small>${formatBytes(item.size)}${item === asset ? ` · ${escapeHtml(target.label)} installer` : ""}</small></span><b>Download ↗</b></a>`).join("")}</div>`
      : `<p class="muted">This release has no downloadable assets.</p>`;
    const downloadAction = asset
      ? `<a class="button button-primary release-download-button" href="${escapeHtml(githubAssetUrl(asset))}" target="_blank" rel="noreferrer"><span>Download for ${escapeHtml(target.label)}</span><small>${escapeHtml(asset.name)}</small></a>`
      : `<div class="release-unavailable"><strong>No ${escapeHtml(target.label)} installer in this release.</strong><p>No download button is shown unless a matching installer asset is attached to this release. Browse the original release page for its published compatibility details.</p></div>`;
    return `<article class="release-card release-featured">
      <div class="release-heading"><div><p class="eyebrow">${stableLabel} · Published ${escapeHtml(formatReleaseDate(release))}</p><h2>${escapeHtml(release.tag_name)}</h2></div><a class="subtle-link" href="${escapeHtml(validReleasePageUrl(release))}" target="_blank" rel="noreferrer">GitHub release ↗</a></div>
      ${title ? `<p class="release-title">${escapeHtml(title)}</p>` : ""}
      <p class="release-platform"><span class="live-dot"></span>${asset ? `Compatible download: ${escapeHtml(target.label)}` : `Checking for ${escapeHtml(target.label)} installer`}</p>
      ${downloadAction}
      ${release.body ? `<details class="release-notes" open><summary>Release notes</summary><pre>${escapeHtml(release.body)}</pre></details>` : `<div class="release-notes"><p class="muted">No release notes were published.</p></div>`}
      <section class="release-assets"><h3>Available assets</h3>${assetList}</section>
    </article>`;
  }

  async function downloadPage() {
    main.innerHTML = `<section class="page-hero compact"><p class="eyebrow">Official builds</p><h1>Download Z+</h1><p>Release information and downloads come directly from the official GitHub Releases API. This page never guesses an asset URL.</p></section><div id="release-content">${loadingPanel("Checking the official release feed…")}</div>`;
    try {
      const releases = (await fetchReleases()).sort(compareReleasesNewestFirst);
      const stable = releases.filter(release => !release.prerelease);
      const target = document.querySelector("#release-content");
      const howTo = `<section class="install-guide"><div><p class="eyebrow">After downloading</p><h2>Install, then verify.</h2><ol><li>Download the Windows installer or distribution archive published with a stable release.</li><li>Run the installer, or extract the complete archive and follow its included installation instructions.</li><li>Open a new PowerShell window and run <code>zp --version</code>.</li><li>Check and run a source file with <code>zp check hello.zp</code> and <code>zp run hello.zp</code>.</li></ol></div>${codeBlock(`start {
    say("Z+ is ready.")
} stop`, "hello.zp")}</section>`;
      if (!releases.length) {
        target.innerHTML = `<div class="state-panel state-warning"><span class="state-icon">!</span><div><h2>No releases are currently available.</h2><p>The official GitHub API returned no published releases. No version or download link has been guessed.</p><a class="button button-primary" href="${releasesUrl}" target="_blank" rel="noreferrer">Open official Releases ↗</a></div></div>${howTo}`;
        return;
      }
      const latestStable = stable[0] || null;
      const selectedRelease = latestStable || releases[0];
      const latestLabel = latestStable
        ? `<p class="latest-stable"><span class="live-dot"></span><span>Latest stable</span><strong>${escapeHtml(latestStable.tag_name)}</strong></p>`
        : `<p class="latest-stable latest-stable-empty">No stable release is published. Prereleases are marked in the version list.</p>`;
      const options = releases.map(release => {
        const label = `${release.tag_name}${release.prerelease ? " · Prerelease" : ""}`;
        return `<option value="${escapeHtml(String(release.id ?? release.tag_name))}">${escapeHtml(label)}</option>`;
      }).join("");
      target.innerHTML = `<section class="release-section release-browser"><div class="release-toolbar"><div class="version-control"><label for="release-version">Select a release version</label><select id="release-version">${options}</select><small>Versions are sorted newest to oldest.</small></div>${latestLabel}</div><div id="selected-release" aria-live="polite"></div></section>${howTo}`;
      const select = target.querySelector("#release-version");
      const detail = target.querySelector("#selected-release");
      const releasesById = new Map(releases.map(release => [String(release.id ?? release.tag_name), release]));
      select.value = String(selectedRelease.id ?? selectedRelease.tag_name);
      const showSelectedRelease = () => {
        const release = releasesById.get(select.value);
        if (release) detail.innerHTML = releaseDetails(release, latestStable);
      };
      select.addEventListener("change", showSelectedRelease);
      showSelectedRelease();
    } catch (error) {
      console.warn("Unable to load official Z+ releases.", error);
      const target = document.querySelector("#release-content");
      if (target) target.innerHTML = `<div class="state-panel state-warning"><span class="state-icon">!</span><div><h2>Release information is unavailable.</h2><p>${escapeHtml(error.message)} Check the official Releases page for current downloads; no version or asset link has been guessed.</p><a class="button button-primary" href="${releasesUrl}" target="_blank" rel="noreferrer">Open official Releases ↗</a></div></div>`;
    }
  }

  function courseSidebar(activeId = "") {
    const progress = getProgress();
    return `<aside class="course-sidebar"><div class="sidebar-title"><span>COURSE CONTENT</span><span>${progress.size}/${lessons.length}</span></div><div class="progress-track"><span style="width:${lessons.length ? progress.size / lessons.length * 100 : 0}%"></span></div>
      ${course.map((module, moduleIndex) => {
        const moduleLessons = lessons.filter(lesson => lesson.moduleIndex === moduleIndex);
        return `<details class="course-module" ${moduleLessons.some(lesson => lesson.id === activeId) || !activeId && moduleIndex === 0 ? "open" : ""}><summary><span>${escapeHtml(module.title)}</span><small>${moduleLessons.filter(lesson => progress.has(lesson.id)).length}/${moduleLessons.length}</small></summary><ol>${moduleLessons.map(lesson => `<li>${link(`/learn/${lesson.id}`, `<span>${String(lesson.number).padStart(2, "0")}</span>${escapeHtml(lesson.title)}${progress.has(lesson.id) ? '<b aria-label="Completed">✓</b>' : ""}`, lesson.id === activeId ? "active" : "")}</li>`).join("")}</ol></details>`;
      }).join("")}</aside>`;
  }

  function learnDashboard() {
    const progress = getProgress();
    const percentage = Math.round(progress.size / lessons.length * 100);
    const currentLesson = lessons.find(lesson => !progress.has(lesson.id)) || lessons[0];
    return `<section class="page-hero compact"><p class="eyebrow">Z+ learning path · 52 lessons</p><h1>Learn Z+, one idea at a time.</h1><p>A guided course from first program to native builds, grounded in the language the compiler actually accepts.</p><div class="hero-actions">${buttonLink(`/learn/${currentLesson.id}`, progress.size ? "Continue learning" : "Begin lesson one")} ${buttonLink("/docs", "Open the reference", "button button-secondary")}</div></section>
      <section class="course-overview"><div class="progress-card"><div class="progress-ring" style="--progress:${percentage}%"><span>${percentage}%</span></div><div><strong>Your progress</strong><p>${progress.size} of ${lessons.length} lessons complete. Progress is saved in this browser; no account is needed.</p></div></div>
        <div class="module-grid">${course.map((module, index) => {
          const first = lessons.find(lesson => lesson.moduleIndex === index);
          const count = lessons.filter(lesson => lesson.moduleIndex === index);
          const done = count.filter(lesson => progress.has(lesson.id)).length;
          return `<a class="module-card" href="/learn/${first.id}"><span class="feature-index">${String(index + 1).padStart(2, "0")}</span><h2>${escapeHtml(module.title)}</h2><p>${count.length} lessons · ${done} complete</p><span class="card-arrow">Open module →</span></a>`;
        }).join("")}</div>
      </section>
      <section class="glossary"><div class="section-heading"><p class="eyebrow">A few useful words</p><h2>Quick glossary</h2></div><dl><div><dt>Compiler</dt><dd>A tool that checks source code and translates supported programs to another form.</dd></div><div><dt>Interpreter</dt><dd>A runtime that executes a program from its intermediate representation.</dd></div><div><dt>Module</dt><dd>A source file that exposes named declarations to another file through the use directive.</dd></div><div><dt>Native backend</dt><dd>The compiler path that emits a host executable for its supported language subset.</dd></div></dl></section>`;
  }

  function lessonPage(id) {
    const lesson = lessonById.get(id);
    if (!lesson) return notFound();
    const progress = getProgress();
    const index = lessons.indexOf(lesson);
    const previous = lessons[index - 1];
    const next = lessons[index + 1];
    return `<div class="learning-layout">${courseSidebar(id)}<article class="lesson-content">
      <div class="breadcrumbs">${link("/", "Home")} <span>/</span> ${link("/learn", "Learn")} <span>/</span> ${escapeHtml(lesson.module)}</div>
      <p class="eyebrow">LESSON ${String(lesson.number).padStart(2, "0")} · ${escapeHtml(lesson.module)}</p><h1>${escapeHtml(lesson.title)}</h1>
      <div class="lesson-objective"><strong>By the end, you can</strong><p>${inlineMarkup(lesson.explain.split(".")[0])}.</p></div>
      <section class="lesson-section"><h2>Understand the idea</h2><p>${inlineMarkup(lesson.explain)}</p></section>
      <section class="lesson-section"><h2>Example</h2>${codeBlock(lesson.code)}<p class="code-explanation">This example demonstrates the concept from the lesson. Read it from the entry block inward and notice how each statement contributes to the result.</p>${lesson.output ? `<div class="expected-output"><span>EXPECTED OUTPUT</span><pre>${escapeHtml(lesson.output)}</pre></div>` : ""}</section>
      <section class="exercise-card"><p class="eyebrow">YOUR TURN</p><h2>Exercise</h2><p>${inlineMarkup(lesson.exercise)}</p><details><summary>Reveal one solution</summary>${codeBlock(lesson.solution)}<p>Try changing the values and checking the behavior yourself.</p></details></section>
      <section class="lesson-summary"><p class="eyebrow">TAKEAWAY</p><p>${inlineMarkup(lesson.summary)}</p></section>
      <div class="lesson-completion"><label><input id="lesson-complete" type="checkbox" ${progress.has(id) ? "checked" : ""}> <span>Mark this lesson complete</span></label><span id="save-status" role="status">Saved on this device</span></div>
      <nav class="lesson-pager" aria-label="Lesson navigation">${previous ? link(`/learn/${previous.id}`, `← Previous <small>${escapeHtml(previous.title)}</small>`, "pager-link") : `<span></span>`}${link("/learn", "Course overview", "pager-overview")}${next ? link(`/learn/${next.id}`, `Next lesson → <small>${escapeHtml(next.title)}</small>`, "pager-link pager-next") : link("/learn", "Finish course →", "pager-link pager-next")}</nav>
    </article></div>`;
  }

  function referenceIndex(items, base, label) {
    return `<section class="page-hero compact"><p class="eyebrow">${label}</p><h1>${label === "Technical reference" ? "Documentation" : label}</h1><p>${label === "Technical reference" ? "Precise reference for syntax, modules, tools, and runtime limits." : "Practical explanations of how Z+ is designed and used."} Search across both sections from the bar above.</p></section><div class="article-grid">${items.map(item => `<a class="article-card" href="${base}/${item.id}"><p class="eyebrow">${escapeHtml(item.section || "GUIDE")}</p><h2>${escapeHtml(item.title)}</h2><p>${escapeHtml(item.summary)}</p><span>Read article →</span></a>`).join("")}</div>`;
  }

  function articlePage(item, kind, all, base) {
    if (!item) return notFound();
    const index = all.indexOf(item);
    const previous = all[index - 1];
    const next = all[index + 1];
    const sections = item.body;
    return `<article class="article-page"><div class="breadcrumbs">${link("/", "Home")} <span>/</span> ${link(base, kind)} <span>/</span> ${escapeHtml(item.title)}</div><div class="article-layout"><div><p class="eyebrow">${escapeHtml(item.section || kind)}</p><h1>${escapeHtml(item.title)}</h1><p class="article-lede">${escapeHtml(item.summary)}</p>
      ${sections.map(([heading, text], sectionIndex) => `<section id="${slug(heading)}"><h2>${escapeHtml(heading)}</h2><p>${inlineMarkup(text)}</p>${kind === "Documentation" && sectionIndex === 0 ? `<p>${link("/learn", "Follow the step-by-step learning course")} · ${link("/examples", "Browse runnable examples")}</p>` : ""}</section>`).join("")}
      <div class="related-links"><strong>Related</strong>${link("/learn", "Learn Z+")}${link("/examples", "Examples")}${link(kind === "Wiki" ? "/docs" : "/wiki", kind === "Wiki" ? "Technical documentation" : "Wiki guides")}</div>
      <nav class="article-pager">${previous ? link(`${base}/${previous.id}`, `← ${escapeHtml(previous.title)}`) : "<span></span>"}${next ? link(`${base}/${next.id}`, `${escapeHtml(next.title)} →`) : "<span></span>"}</nav>
      </div><aside class="table-of-contents"><strong>ON THIS PAGE</strong><ol>${sections.map(([heading]) => `<li><a href="#${slug(heading)}">${escapeHtml(heading)}</a></li>`).join("")}</ol></aside></div></article>`;
  }

  function examplesPage() {
    return `<section class="page-hero compact"><p class="eyebrow">Learn by reading code</p><h1>Z+ examples</h1><p>Small programs and patterns based on implemented syntax and public APIs. The multi-file entry expects the sibling module described in its card.</p></section><div class="examples-grid">${examples.map(item => `<article class="example-card" id="${item.id}"><div class="example-heading"><div><p class="eyebrow">EXAMPLE</p><h2>${escapeHtml(item.title)}</h2></div><a href="/learn" aria-label="Learn more">↗</a></div><p>${escapeHtml(item.summary)}</p>${codeBlock(item.code)}${item.id === "modules" ? `<p class="example-note">Sibling file <code>greetings.zh</code> must export <code>pub fn hello(): string</code>.</p>` : ""}</article>`).join("")}</div>`;
  }

  function packagesPage() {
    return `<section class="page-hero compact"><p class="eyebrow">The Z+ ecosystem</p><h1>Packages for Z+</h1><p>Discover source-backed Z+ packages, inspect their requirements, and publish releases from your developer account.</p></section>
      <section class="registry-layout">
        <div class="registry-main">
          <form id="registry-search-form" class="registry-search">
            <label for="registry-query">Search packages</label>
            <div><input id="registry-query" name="q" type="search" maxlength="100" placeholder="Name or description" autocomplete="off"><button class="button button-primary" type="submit">Search</button></div>
          </form>
          <div id="registry-results" class="registry-results" aria-live="polite"><p class="registry-message">Loading packages…</p></div>
        </div>
        <aside id="registry-account" class="registry-account"><p class="registry-message">Checking developer account…</p></aside>
      </section>`;
  }

  function packageDetailPage(name) {
    return `<section class="page-hero compact"><p class="eyebrow">${link("/packages", "Packages")}</p><h1>${escapeHtml(name)}</h1></section>
      <section id="package-detail" class="package-detail"><p class="registry-message">Loading package details…</p></section>`;
  }

  function safeExternalLink(value, label) {
    try {
      const url = new URL(value);
      if (url.protocol !== "https:") return "";
      return `<a href="${escapeHtml(url.href)}" target="_blank" rel="noopener noreferrer">${escapeHtml(label)}</a>`;
    } catch {
      return "";
    }
  }

  async function registryJson(response, fallback) {
    let result;
    try {
      result = await response.json();
    } catch {
      throw new Error("The package registry did not return JSON. It may be unavailable or not deployed.");
    }
    if (!response.ok) {
      throw new Error(result.error?.message || fallback);
    }
    return result;
  }

  async function registryRequest(path, options, fallback) {
    return registryJson(await fetch(path, options), fallback);
  }

  function renderRegistryPackages(packages) {
    if (!packages.length) {
      return `<p class="registry-message">No packages found. Try a different search.</p>`;
    }
    return `<div class="registry-package-list">${packages.map(item => `<article class="registry-package">
      <div><h2>${link(`/packages/${encodeURIComponent(item.name)}`, escapeHtml(item.name))}</h2><p>${escapeHtml(item.description || "No description provided.")}</p></div>
      <div class="registry-package-meta"><span>v${escapeHtml(item.version || "—")}</span><span>by ${escapeHtml(item.author || item.owner)}</span>${item.license ? `<span>${escapeHtml(item.license)}</span>` : ""}</div>
    </article>`).join("")}</div>`;
  }

  async function loadRegistryPackages(query = "") {
    const target = document.querySelector("#registry-results");
    if (!target) return;
    const sequence = ++registrySearchSequence;
    target.innerHTML = `<p class="registry-message">Loading packages…</p>`;
    try {
      const params = new URLSearchParams();
      if (query) params.set("q", query);
      const result = await registryRequest(`/api/registry/search?${params}`, {
        headers: { Accept: "application/json" }
      }, "Package search is unavailable.");
      if (sequence !== registrySearchSequence || !target.isConnected) return;
      target.innerHTML = renderRegistryPackages(result.packages || []);
    } catch (error) {
      if (sequence !== registrySearchSequence || !target.isConnected) return;
      target.innerHTML = `<p class="registry-message registry-error">${escapeHtml(error.message)} The registry may be temporarily unavailable.</p>`;
    }
  }

  function authForms(message = "") {
    return `<section class="registry-panel">
      <p class="eyebrow">DEVELOPER ACCESS</p><h2>Sign in or create an account</h2>
      <p>Accounts are used to manage package ownership and publish versions.</p>
      <div class="registry-auth-grid">
        <form data-registry-auth="login">
          <h3>Sign in</h3>
          <label>Email<input name="email" type="email" autocomplete="email" maxlength="254" required></label>
          <label>Password<input name="password" type="password" autocomplete="current-password" maxlength="1024" required></label>
          <button class="button button-secondary" type="submit">Sign in</button>
        </form>
        <form data-registry-auth="register">
          <h3>Create account</h3>
          <label>Username<input name="username" type="text" autocomplete="username" minlength="3" maxlength="32" pattern="[A-Za-z0-9][A-Za-z0-9_\\-]{2,31}" required></label>
          <label>Email<input name="email" type="email" autocomplete="email" maxlength="254" required></label>
          <label>Password<input name="password" type="password" autocomplete="new-password" minlength="12" maxlength="1024" required></label>
          <button class="button button-primary" type="submit">Create account</button>
        </form>
      </div>
      <p id="registry-auth-status" class="registry-message" role="status">${escapeHtml(message)}</p>
    </section>`;
  }

  function renderRegistryAccount(user, packages = []) {
    const target = document.querySelector("#registry-account");
    if (!target) return;
    if (!user) {
      target.innerHTML = authForms();
      return;
    }
    target.innerHTML = `<section class="registry-panel">
      <p class="eyebrow">DEVELOPER ACCOUNT</p><h2>${escapeHtml(user.username)}</h2>
      <p>Signed in as ${escapeHtml(user.email)}.</p>
      <button class="button button-secondary" type="button" data-registry-logout>Sign out</button>
      <h3 class="registry-subheading">Publish a package</h3>
      <p>Build with <code>zpbuild</code>, then upload a ZPM1 <code>.zpm</code> archive created with <code>zpm pack</code>. The registry validates the manifest, archive paths, and presence of Z+ source files without executing untrusted code.</p>
      <form id="registry-publish-form" class="registry-publish-form">
        <label>Package archive<input name="archive" type="file" accept=".zpm,application/vnd.zpm.archive" required></label>
        <button class="button button-primary" type="submit">Publish release</button>
      </form>
      <p id="registry-publish-status" class="registry-message" role="status"></p>
      <h3 class="registry-subheading">Packages you manage</h3>
      ${packages.length ? `<ul class="registry-managed-list">${packages.map(item => `<li>${link(`/packages/${encodeURIComponent(item.name)}`, escapeHtml(item.name))}${item.is_owner ? " · owner" : " · maintainer"}</li>`).join("")}</ul>` : `<p class="registry-message">No packages are associated with this account yet.</p>`}
    </section>`;
  }

  async function loadRegistryAccount() {
    const target = document.querySelector("#registry-account");
    if (!target) return;
    try {
      const response = await fetch("/api/registry/auth/me", {
        credentials: "same-origin",
        headers: { Accept: "application/json" }
      });
      if (response.status === 401) {
        renderRegistryAccount(null);
        return;
      }
      const result = await registryJson(response, "Unable to load account.");
      const packageResult = await registryRequest("/api/registry/me/packages", {
        credentials: "same-origin",
        headers: { Accept: "application/json" }
      }, "Unable to load managed packages.");
      renderRegistryAccount(result.user, packageResult.packages || []);
    } catch (error) {
      if (target.isConnected) {
        target.innerHTML = `<section class="registry-panel"><p class="registry-message registry-error">${escapeHtml(error.message)}</p></section>`;
      }
    }
  }

  function packageDetailsMarkup(item, user) {
    const latest = item.versions[0];
    const metadataLinks = [
      safeExternalLink(item.repository, "Repository"),
      safeExternalLink(item.homepage, "Homepage")
    ].filter(Boolean);
    const ownerControls = user?.username === item.owner
      ? `<section class="package-owner-tools">
          <h2>Package ownership</h2>
          <form data-maintainer-add="${escapeHtml(item.name)}">
            <label>Developer username<input name="username" type="text" minlength="3" maxlength="32" pattern="[A-Za-z0-9][A-Za-z0-9_\\-]{2,31}" required></label>
            <button class="button button-secondary" type="submit">Add maintainer</button>
          </form>
          ${item.maintainers?.length ? `<ul>${item.maintainers.map(name => `<li>${escapeHtml(name)} <button type="button" data-maintainer-remove="${escapeHtml(item.name)}" data-username="${escapeHtml(name)}">Remove</button></li>`).join("")}</ul>` : `<p>No maintainers have been added.</p>`}
          <form data-owner-transfer="${escapeHtml(item.name)}">
            <label>Transfer ownership to<input name="username" type="text" minlength="3" maxlength="32" pattern="[A-Za-z0-9][A-Za-z0-9_\\-]{2,31}" required></label>
            <label class="ownership-confirm"><input name="confirm" type="checkbox" required> I understand that this action transfers package ownership.</label>
            <button class="button button-secondary" type="submit">Transfer ownership</button>
          </form>
          <p class="registry-message" data-package-admin-status role="status"></p>
        </section>`
      : "";
    return `<article class="package-detail-card">
      <div class="package-detail-heading">
        <div><p class="eyebrow">Z+ PACKAGE</p><h1>${escapeHtml(item.name)}</h1><p>${escapeHtml(item.description || "No description provided.")}</p></div>
        <span class="package-version-badge">v${escapeHtml(latest?.version || "—")}</span>
      </div>
      <div class="package-detail-meta"><span>Owner: ${escapeHtml(item.owner)}</span><span>Author: ${escapeHtml(item.author || item.owner)}</span>${item.license ? `<span>License: ${escapeHtml(item.license)}</span>` : ""}${metadataLinks.join("")}</div>
      <section><h2>Install</h2>${codeBlock(`zpm add ${item.name}`, "Terminal")}<p class="package-note">Then import the package's public module using its manifest entry point.</p></section>
      <section><h2>Release history</h2><div class="registry-version-list">${item.versions.map(version => `<article>
        <div><strong>v${escapeHtml(version.version)}</strong><small>Published ${escapeHtml(new Date(version.publishedAt).toLocaleDateString())} by ${escapeHtml(version.publisher)}</small></div>
        <div><span>Compiler ${escapeHtml(version.compiler || "unspecified")}</span><a href="${escapeHtml(version.downloadUrl)}" target="_blank" rel="noopener noreferrer">Download .zpm</a></div>
        ${Object.keys(version.dependencies || {}).length ? `<p>Dependencies: ${Object.entries(version.dependencies).map(([name, range]) => `${escapeHtml(name)} ${escapeHtml(range)}`).join(", ")}</p>` : ""}
      </article>`).join("")}</div></section>
      ${latest?.readme ? `<section><h2>Documentation</h2><pre class="package-readme">${escapeHtml(latest.readme)}</pre></section>` : ""}
      ${item.maintainers?.length ? `<section><h2>Maintainers</h2><p>${item.maintainers.map(escapeHtml).join(", ")}</p></section>` : ""}
      ${ownerControls}
    </article>`;
  }

  async function loadPackageDetails(name) {
    const target = document.querySelector("#package-detail");
    if (!target) return;
    try {
      const result = await registryRequest(`/api/registry/packages/${encodeURIComponent(name)}`, {
        headers: { Accept: "application/json" }
      }, "Package could not be loaded.");
      let user = null;
      const accountResponse = await fetch("/api/registry/auth/me", {
        credentials: "same-origin",
        headers: { Accept: "application/json" }
      });
      if (accountResponse.status !== 401) {
        const account = await registryJson(accountResponse, "Unable to load account.");
        user = account.user;
      }
      if (target.isConnected) target.innerHTML = packageDetailsMarkup(result, user);
    } catch (error) {
      if (target.isConnected) {
        target.innerHTML = `<p class="registry-message registry-error">${escapeHtml(error.message)}</p>`;
      }
    }
  }

  function notFound() {
    return `<section class="page-hero compact"><p class="eyebrow">404 · NOT FOUND</p><h1>This page isn't in the guide.</h1><p>The requested Z+ route may have moved. Start from the course, reference, or homepage.</p><div class="hero-actions">${buttonLink("/", "Go home")} ${buttonLink("/learn", "Browse lessons", "button button-secondary")}</div></section>`;
  }

  function render() {
    const route = path();
    document.querySelectorAll(".primary-nav a").forEach(anchor => {
      const active = route === anchor.pathname || (anchor.pathname === "/learn" && route.startsWith("/learn/")) ||
        (anchor.pathname === "/docs" && route.startsWith("/docs/")) || (anchor.pathname === "/wiki" && route.startsWith("/wiki/")) ||
        (anchor.pathname === "/packages" && route.startsWith("/packages"));
      anchor.classList.toggle("nav-active", active);
      if (active) anchor.setAttribute("aria-current", "page");
      else anchor.removeAttribute("aria-current");
    });

    if (route === "/") {
      main.innerHTML = homePage();
      document.title = "Z+ — A language, from first program to real projects";
    } else if (route === "/download") {
      downloadPage();
      document.title = "Download Z+";
    } else if (route === "/learn") {
      main.innerHTML = learnDashboard();
      document.title = "Learn Z+ — Course";
    } else if (route.startsWith("/learn/")) {
      const id = route.slice("/learn/".length);
      const lesson = lessonById.get(id);
      main.innerHTML = lessonPage(id);
      document.title = lesson ? `${lesson.title} — Learn Z+` : "Lesson not found — Z+";
    } else if (route === "/docs") {
      main.innerHTML = referenceIndex(docs, "/docs", "Technical reference");
      document.title = "Documentation — Z+";
    } else if (route.startsWith("/docs/")) {
      const item = docs.find(article => article.id === route.slice(6));
      main.innerHTML = articlePage(item, "Documentation", docs, "/docs");
      document.title = item ? `${item.title} — Z+ Documentation` : "Not found — Z+";
    } else if (route === "/wiki") {
      main.innerHTML = referenceIndex(wiki, "/wiki", "Wiki & guides");
      document.title = "Wiki — Z+";
    } else if (route.startsWith("/wiki/")) {
      const wikiArticles = wiki.map(entry => ({ ...entry, body: entry.body.split(/\n\n+/).map(paragraph => ["Guide", paragraph]) }));
      const item = wikiArticles.find(article => article.id === route.slice(6));
      main.innerHTML = articlePage(item, "Wiki", wikiArticles, "/wiki");
      document.title = item ? `${item.title} — Z+ Wiki` : "Not found — Z+";
    } else if (route === "/examples") {
      main.innerHTML = examplesPage();
      document.title = "Examples — Z+";
    } else if (route === "/packages") {
      main.innerHTML = packagesPage();
      document.title = "Packages — Z+";
      void loadRegistryPackages();
      void loadRegistryAccount();
    } else if (route.startsWith("/packages/")) {
      let packageName = "";
      try {
        packageName = decodeURIComponent(route.slice("/packages/".length));
      } catch {
        packageName = "";
      }
      main.innerHTML = packageDetailPage(packageName);
      document.title = `${packageName || "Package"} — Z+ Registry`;
      void loadPackageDetails(packageName);
    } else {
      main.innerHTML = notFound();
      document.title = "Not found — Z+";
    }
    const description = route === "/" ? "Discover Z+, download the compiler, and follow a complete course from first program to real projects."
      : route === "/download" ? "Get published Z+ releases directly from the official GitHub Releases feed."
      : route === "/learn" ? "Follow 52 lessons to learn the implemented Z+ language, with examples and exercises."
      : route.startsWith("/learn/") ? lessonById.get(route.slice(7))?.explain
      : route.startsWith("/docs/") ? docs.find(item => item.id === route.slice(6))?.summary
      : route.startsWith("/wiki/") ? wiki.find(item => item.id === route.slice(6))?.summary
      : route === "/docs" ? "Technical reference for Z+ syntax, modules, tools, and runtime support."
      : route === "/wiki" ? "Guides to Z+ architecture, modules, projects, and contributing."
      : route === "/examples" ? "Real Z+ source examples for functions, files, modules, and collections."
      : route.startsWith("/packages") ? "Browse, install, and publish source-backed Z+ packages."
      : "Find Z+ language documentation, examples, and learning resources.";
    document.querySelector('meta[name="description"]').setAttribute("content", description);
    document.querySelector("#primary-nav").classList.remove("nav-open");
    document.querySelector(".menu-toggle").setAttribute("aria-expanded", "false");
    main.focus({ preventScroll: true });
  }

  function navigate(href) {
    const url = new URL(href, location.origin);
    if (url.origin !== location.origin) return;
    history.pushState(null, "", url.pathname + url.search + url.hash);
    render();
    if (url.hash) requestAnimationFrame(() => document.getElementById(decodeURIComponent(url.hash.slice(1)))?.scrollIntoView());
    else window.scrollTo(0, 0);
  }

  function updateSearch() {
    const query = search.value.trim().toLowerCase();
    if (query.length < 2) {
      searchResults.hidden = true;
      searchResults.innerHTML = "";
      return;
    }
    const results = contentIndex.filter(item => `${item.title} ${item.summary} ${item.type}`.toLowerCase().includes(query)).slice(0, 8);
    searchResults.innerHTML = results.length ? results.map(item => `<a role="option" href="${item.href}"><span>${escapeHtml(item.type)}</span><strong>${escapeHtml(item.title)}</strong><small>${escapeHtml(item.summary)}</small></a>`).join("") : `<p class="search-empty">No matching Z+ pages. Try a topic such as arrays, modules, or errors.</p>`;
    searchResults.hidden = false;
  }

  async function submitRegistryAuth(form) {
    const status = document.querySelector("#registry-auth-status");
    const button = form.querySelector('button[type="submit"]');
    const mode = form.dataset.registryAuth;
    button.disabled = true;
    if (status) status.textContent = mode === "register"
      ? "Creating account…"
      : "Signing in…";
    try {
      await registryRequest(`/api/registry/auth/${mode}`, {
        method: "POST",
        credentials: "same-origin",
        headers: {
          Accept: "application/json",
          "Content-Type": "application/json"
        },
        body: JSON.stringify(Object.fromEntries(new FormData(form)))
      }, mode === "register" ? "Registration failed." : "Sign-in failed.");
      await loadRegistryAccount();
    } catch (error) {
      if (status?.isConnected) status.textContent = error.message;
    } finally {
      button.disabled = false;
    }
  }

  async function submitRegistryPublish(form) {
    const status = document.querySelector("#registry-publish-status");
    const button = form.querySelector('button[type="submit"]');
    const file = form.elements.archive.files[0];
    if (!file) return;
    if (file.size > 4 * 1024 * 1024) {
      status.textContent = "Registry packages are limited to 4 MiB.";
      return;
    }
    button.disabled = true;
    status.textContent = "Validating and publishing package…";
    try {
      const result = await registryRequest("/api/registry/publish", {
        method: "POST",
        credentials: "same-origin",
        headers: {
          Accept: "application/json",
          "Content-Type": "application/vnd.zpm.archive"
        },
        body: file
      }, "Package publishing failed.");
      const message = `Published ${result.name} ${result.version}.`;
      form.reset();
      await Promise.all([loadRegistryPackages(), loadRegistryAccount()]);
      const updatedStatus = document.querySelector("#registry-publish-status");
      if (updatedStatus) updatedStatus.textContent = message;
    } catch (error) {
      if (status?.isConnected) status.textContent = error.message;
    } finally {
      button.disabled = false;
    }
  }

  async function submitPackageAdmin(form) {
    const packageName = form.dataset.maintainerAdd || form.dataset.ownerTransfer;
    const transfer = form.hasAttribute("data-owner-transfer");
    const button = form.querySelector('button[type="submit"]');
    const status = document.querySelector("[data-package-admin-status]");
    button.disabled = true;
    try {
      await registryRequest(
        `/api/registry/packages/${encodeURIComponent(packageName)}/${transfer ? "transfer" : "maintainers"}`,
        {
          method: "POST",
          credentials: "same-origin",
          headers: {
            Accept: "application/json",
            "Content-Type": "application/json"
          },
          body: JSON.stringify({ username: form.elements.username.value.trim() })
        },
        transfer ? "Ownership transfer failed." : "Adding maintainer failed."
      );
      const message = transfer
        ? "Package ownership transferred."
        : "Maintainer added.";
      await loadPackageDetails(packageName);
      const updatedStatus = document.querySelector("[data-package-admin-status]");
      if (updatedStatus) updatedStatus.textContent = message;
    } catch (error) {
      if (status?.isConnected) status.textContent = error.message;
    } finally {
      button.disabled = false;
    }
  }

  async function removePackageMaintainer(button) {
    const packageName = button.dataset.maintainerRemove;
    const username = button.dataset.username;
    const status = document.querySelector("[data-package-admin-status]");
    button.disabled = true;
    try {
      await registryRequest(
        `/api/registry/packages/${encodeURIComponent(packageName)}/maintainers/${encodeURIComponent(username)}`,
        { method: "DELETE", credentials: "same-origin" },
        "Removing maintainer failed."
      );
      const message = "Maintainer removed.";
      await loadPackageDetails(packageName);
      const updatedStatus = document.querySelector("[data-package-admin-status]");
      if (updatedStatus) updatedStatus.textContent = message;
    } catch (error) {
      if (status?.isConnected) status.textContent = error.message;
      button.disabled = false;
    }
  }

  document.addEventListener("click", event => {
    const anchor = event.target.closest("a");
    if (anchor && anchor.origin === location.origin && !event.metaKey && !event.ctrlKey && !event.shiftKey && !event.altKey && anchor.target !== "_blank") {
      event.preventDefault();
      navigate(anchor.href);
      searchResults.hidden = true;
    }
    const copy = event.target.closest(".copy-code");
    if (copy) {
      navigator.clipboard.writeText(copy.dataset.copy).then(() => {
        copy.textContent = "Copied";
        setTimeout(() => { copy.textContent = "Copy"; }, 1200);
      }).catch(error => {
        console.warn("Unable to copy the code sample.", error);
        copy.textContent = "Copy failed";
      });
    }
    const logout = event.target.closest("[data-registry-logout]");
    if (logout) {
      logout.disabled = true;
      registryRequest("/api/registry/auth/logout", {
        method: "POST",
        credentials: "same-origin",
        headers: {
          Accept: "application/json",
          "Content-Type": "application/json"
        },
        body: "{}"
      }, "Sign-out failed.").then(
        () => loadRegistryAccount(),
        error => {
          const target = document.querySelector("#registry-account");
          if (target?.isConnected) {
            target.insertAdjacentHTML("beforeend",
              `<p class="registry-message registry-error">${escapeHtml(error.message)}</p>`);
          }
          logout.disabled = false;
        }
      );
    }
    const removeMaintainer = event.target.closest("[data-maintainer-remove]");
    if (removeMaintainer) void removePackageMaintainer(removeMaintainer);
  });

  document.addEventListener("submit", event => {
    const form = event.target;
    if (form.id === "registry-search-form") {
      event.preventDefault();
      clearTimeout(registrySearchTimer);
      void loadRegistryPackages(form.elements.q.value.trim());
    } else if (form.matches("[data-registry-auth]")) {
      event.preventDefault();
      void submitRegistryAuth(form);
    } else if (form.id === "registry-publish-form") {
      event.preventDefault();
      void submitRegistryPublish(form);
    } else if (form.matches("[data-maintainer-add], [data-owner-transfer]")) {
      event.preventDefault();
      void submitPackageAdmin(form);
    }
  });

  document.addEventListener("input", event => {
    if (event.target.id !== "registry-query") return;
    clearTimeout(registrySearchTimer);
    registrySearchTimer = setTimeout(
      () => loadRegistryPackages(event.target.value.trim()),
      250
    );
  });

  document.addEventListener("change", event => {
    if (event.target.id !== "lesson-complete") return;
    const id = path().slice("/learn/".length);
    const progress = getProgress();
    if (event.target.checked) progress.add(id);
    else progress.delete(id);
    const saved = saveProgress(progress);
    const status = document.querySelector("#save-status");
    if (status) status.textContent = saved ? "Saved on this device" : "Could not save in this browser";
    const current = document.querySelector(".course-sidebar");
    if (current) current.outerHTML = courseSidebar(id);
  });

  document.querySelector(".menu-toggle").addEventListener("click", event => {
    const nav = document.querySelector("#primary-nav");
    const open = nav.classList.toggle("nav-open");
    event.currentTarget.setAttribute("aria-expanded", String(open));
  });
  search.addEventListener("input", updateSearch);
  search.addEventListener("keydown", event => {
    if (event.key === "Escape") {
      searchResults.hidden = true;
      search.blur();
    }
    if (event.key === "Enter") {
      const first = searchResults.querySelector("a");
      if (first) navigate(first.href);
    }
  });
  document.addEventListener("click", event => {
    if (!event.target.closest(".site-search, .search-results")) searchResults.hidden = true;
  });
  window.addEventListener("popstate", render);

  renderFooter();
  render();
})();
