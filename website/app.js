(() => {
  const { course, docs, wiki, examples, zts, zjs } = window.ZPlusSite;
  const products = window.ZPlusProducts;
  const main = document.querySelector("#main");
  const footer = document.querySelector("#site-footer");
  const search = document.querySelector("#site-search");
  const searchResults = document.querySelector("#search-results");
  let metadata = null;
  let releasesUrl = "";
  let releasesApi = "";
  let releaseAssetPrefix = "";
  let downloadTargets = [];
  const progressKey = "zplus-learning-progress-v1";
  let releaseRequest;
  let releaseViewSequence = 0;
  let registrySearchTimer;
  let registrySearchSequence = 0;
  let publishSelectionSequence = 0;

  function configureMetadata(value) {
    metadata = value;
    releasesUrl = metadata.releases.page;
    releasesApi = metadata.releases.api;
    releaseAssetPrefix = metadata.releases.downloadPathPrefix;
    downloadTargets = metadata.releases.assetTargets.map(target => ({
      ...target,
      filePattern: new RegExp(target.filePattern, "i"),
      installerPattern: new RegExp(target.installerPattern, "i"),
      platformPattern: new RegExp(target.platformPattern, "i"),
      architecturePattern: new RegExp(target.architecturePattern, "i"),
      conflictPattern: new RegExp(target.conflictPattern, "i")
    }));
    document.querySelector('meta[property="og:image"]').setAttribute("content", metadata.project.socialImage);
  }

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
  const productList = Object.values(products);
  const contentIndex = [
    ...docs.map(item => ({ title: item.title, summary: item.summary, href: `/docs/${item.id}`, type: "Documentation" })),
    ...productList.map(item => ({ title: item.title, summary: item.summary, href: `/${item.id}`, type: "Ecosystem" })),
    ...wiki.map(item => ({ title: item.title, summary: item.summary, href: `/wiki/${item.id}`, type: "Wiki" })),
    { title: zts.title, summary: zts.summary, href: "/zts", type: "Web interoperability" },
    { title: zjs.title, summary: zjs.summary, href: "/zjs", type: "Web interoperability" },
    ...lessons.map(item => ({ title: item.title, summary: item.explain, href: `/learn/${item.id}`, type: `Lesson ${item.number}` })),
    ...examples.map(item => ({ title: item.title, summary: item.summary, href: `/examples#${item.id}`, type: "Example" })),
    { title: "Package registry", summary: "Search and inspect published Z+ packages.", href: "/packages", type: "Registry" },
    { title: "Publish a package", summary: "Publish a validated ZPM archive.", href: "/publish", type: "Developer tools" }
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
    const token = /(\/\/[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b(?:start|stop|let|const|fn|return|if|else|while|for|in|end|repeat|try|catch|throw|use|as|pub|private|protected|struct|class|enum|namespace|this|switch|case|default|break|continue|true|false|and|or|not)\b|\b\d+(?:\.\d+)?\b)/g;
    let result = "";
    let offset = 0;
    for (const match of code.matchAll(token)) {
      result += escapeHtml(code.slice(offset, match.index));
      const value = match[0];
      const kind = value.startsWith("//") ? "comment" : value.startsWith('"') || value.startsWith("'") ? "string" : /^\d/.test(value) ? "number" : "keyword";
      result += `<span class="syntax-${kind}">${escapeHtml(value)}</span>`;
      offset = match.index + value.length;
    }
    return result + escapeHtml(code.slice(offset));
  }

  function codeBlock(code, label = "Z+") {
    return `<div class="code-frame"><div class="code-toolbar"><span>${escapeHtml(label)}</span><button class="copy-code" type="button" data-copy="${escapeHtml(code)}" aria-label="Copy ${escapeHtml(label)} code">Copy</button></div><pre><code>${highlighted(code)}</code></pre></div>`;
  }

  function codeExample(sample, label) {
    if (typeof sample === "string") return codeBlock(sample, label);
    if (!sample || typeof sample.code !== "string") return "";
    return `${codeBlock(sample.code, sample.name || label)}${(sample.files || []).map(file => codeBlock(file.code, file.name)).join("")}`;
  }

  async function copyCode(button) {
    const value = button.dataset.copy || "";
    try {
      if (navigator.clipboard?.writeText) {
        await navigator.clipboard.writeText(value);
      } else {
        const field = document.createElement("textarea");
        field.value = value;
        field.setAttribute("readonly", "");
        field.style.position = "fixed";
        field.style.opacity = "0";
        document.body.append(field);
        field.select();
        const copied = document.execCommand("copy");
        field.remove();
        if (!copied) throw new Error("Clipboard access is unavailable in this browser.");
      }
      button.textContent = "Copied";
      button.setAttribute("aria-label", "Code copied to clipboard");
      setTimeout(() => {
        if (button.isConnected) {
          button.textContent = "Copy";
          button.setAttribute("aria-label", "Copy code sample");
        }
      }, 1400);
    } catch (error) {
      button.textContent = "Copy failed";
      button.setAttribute("aria-label", error.message || "Could not copy code");
      setTimeout(() => {
        if (button.isConnected) button.textContent = "Copy";
      }, 2200);
    }
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
        ${link("/zts", "ZTS")}
        ${link("/zjs", "ZJS")}
        ${link("/packages", "Registry")}
        ${link("/dashboard", "Developer account")}
        <a href="${releasesUrl}" target="_blank" rel="noreferrer">GitHub Releases ↗</a>
        <a href="${metadata.project.repository}" target="_blank" rel="noreferrer">Source repository ↗</a>
        <a href="${metadata.project.issueTracker}" target="_blank" rel="noreferrer">Issues ↗</a>
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
      <div class="section-heading"><p class="eyebrow">The language</p><h2>Small steps, real tools.</h2><p>Z+ brings its compiler, interpreter, native backend, standard library, and ZPBuild into one development workflow.</p></div>
      <div class="feature-grid">
        <article class="feature"><span class="feature-index">01</span><h3>Explicit structure</h3><p>Readable brace blocks pair with named terminators, and types help catch mistakes before a program runs.</p></article>
        <article class="feature"><span class="feature-index">02</span><h3>Two execution paths</h3><p>Run source through the interpreter or build a native executable for supported constructs and platforms.</p></article>
        <article class="feature"><span class="feature-index">03</span><h3>Useful foundations</h3><p>Use modules, dynamic arrays, filesystem tools, text utilities, and a project build system as your programs grow.</p></article>
      </div>
    </section>
    <section class="section-block split-section">
      <div><p class="eyebrow">Made to explore</p><h2>Start with one file.<br>Keep going from there.</h2><p>Follow a ${lessons.length}-lesson course from the first instruction to modules, file-backed apps, and release builds. Each step includes real Z+ syntax and a small exercise.</p>${buttonLink("/learn", "Explore the course", "text-link")}</div>
      <div class="project-list">
        ${examples.slice(1, 5).map((item, index) => `<a href="/examples#${item.id}"><span>0${index + 1}</span><div><strong>${escapeHtml(item.title)}</strong><small>${escapeHtml(item.summary)}</small></div><b>↗</b></a>`).join("")}
      </div>
    </section>
    <section class="section-block resource-strip">
      <div><p class="eyebrow">The reference shelf</p><h2>Learn the rules.<br>See what is implemented.</h2></div>
      <div class="resource-links">${link("/docs", "Technical documentation", "resource-link")} ${link("/wiki", "Wiki & practical guides", "resource-link")} ${link("/examples", "Runnable examples", "resource-link")} ${link("/zts", "TypeScript interoperability · experimental", "resource-link")} ${link("/zjs", "JavaScript interoperability · experimental", "resource-link")}</div>
    </section>
    <section class="section-block registry-callout">
      <div><p class="eyebrow">ZPM PACKAGE REGISTRY</p><h2>Find a package.<br>Share what you build.</h2><p>Search published Z+ packages, inspect their manifests and release history, or sign in to publish a verified archive.</p></div>
      <div class="hero-actions">${buttonLink("/packages", "Explore packages")} ${buttonLink("/publish", "Publish a package", "button button-secondary")}</div>
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
    const officialApi = new URL(releasesApi);
    if (next.origin !== officialApi.origin || next.pathname !== officialApi.pathname) {
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
      const releaseRepository = new URL(metadata.releases.repository);
      if (url.protocol !== "https:" || url.origin !== releaseRepository.origin ||
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
      const releaseRepository = new URL(metadata.releases.repository);
      return url.protocol === "https:" && url.origin === releaseRepository.origin &&
        url.pathname.startsWith(`${releaseRepository.pathname}/releases/tag/`)
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
      ${release.body ? `<details class="release-notes" open><summary>Release notes</summary><div class="release-notes-content">${window.renderReleaseNotes(release.body)}</div></details>` : `<div class="release-notes"><p class="muted">No release notes were published.</p></div>`}
      <section class="release-assets"><h3>Available assets</h3>${assetList}</section>
    </article>`;
  }

  async function downloadPage(historyOnly = false) {
    const route = historyOnly ? "/releases" : "/download";
    const sequence = ++releaseViewSequence;
    const heading = historyOnly ? "Published releases" : "Downloads";
    main.innerHTML = `<section class="page-hero compact"><p class="eyebrow">Official release assets</p><h1>${heading}</h1><p>Version history and downloads come directly from the official GitHub Releases API. A download appears only when an asset is actually published.</p></section>${historyOnly ? "" : downloadProducts()}<div id="release-content">${loadingPanel("Checking the official release feed…")}</div>`;
    try {
      const releases = (await fetchReleases()).sort(compareReleasesNewestFirst);
      if (sequence !== releaseViewSequence || path() !== route) return;
      const target = main.querySelector("#release-content");
      if (!target) return;
      const stable = releases.filter(release => !release.prerelease);
      const helloCode = ["start {", "    say(\"Z+ is ready.\")", "} stop"].join("\n");
      const howTo = historyOnly ? "" : `<section class="install-guide"><div><p class="eyebrow">After downloading</p><h2>Install, then verify.</h2><ol><li>Choose a published asset that matches your operating system and architecture.</li><li>Run the Windows x64 setup or follow the included platform instructions when another distribution is published.</li><li>Open a new terminal and run <code>zp version</code>, <code>zpm --version</code>, and <code>zpbuild --version</code>.</li><li>Check and run a source file with <code>zp check hello.zp</code> and <code>zp run hello.zp</code>.</li></ol></div>${codeBlock(helloCode, "hello.zp")}</section>`;
      if (!releases.length) {
        target.innerHTML = `<div class="state-panel state-warning"><span class="state-icon">!</span><div><h2>No releases are currently available.</h2><p>The official GitHub API returned no published releases. No version or download link has been guessed.</p><a class="button button-primary" href="${releasesUrl}" target="_blank" rel="noreferrer">Open official Releases ↗</a></div></div>${howTo}`;
        return;
      }
      const latestStable = stable[0] || null;
      const downloadVersion = document.querySelector("#download-compiler-version");
      if (downloadVersion) downloadVersion.textContent = latestStable ? `Latest published stable: ${latestStable.tag_name}.` : "No stable compiler release is currently published.";
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
      if (sequence !== releaseViewSequence || path() !== route) return;
      console.warn("Unable to load official Z+ releases.", error);
      const target = main.querySelector("#release-content");
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
    return `<section class="page-hero compact"><p class="eyebrow">Z+ learning path · ${lessons.length} lessons</p><h1>Learn Z+, one idea at a time.</h1><p>A guided course from first program to native builds, grounded in the language the compiler actually accepts.</p><div class="hero-actions">${buttonLink(`/learn/${currentLesson.id}`, progress.size ? "Continue learning" : "Begin lesson one")} ${buttonLink("/wiki/syntax", "Open Z+ Syntax", "button button-secondary")}</div></section>
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
      <section class="lesson-section"><h2>Example</h2>${codeBlock(lesson.code, "main.zp")}${(lesson.files || []).map(file => codeBlock(file.code, file.name)).join("")}<p class="code-explanation">This example demonstrates the concept from the lesson. Read it from the entry block inward and notice how each statement contributes to the result. Use <code>zp check</code> to validate it, then <code>zp run</code> for the interpreter output.</p>${lesson.output ? `<div class="expected-output"><span>EXPECTED OUTPUT</span><pre>${escapeHtml(lesson.output)}</pre></div>` : ""}</section>
      <section class="exercise-card"><p class="eyebrow">YOUR TURN</p><h2>Exercise</h2><p>${inlineMarkup(lesson.exercise)}</p><details><summary>Reveal one solution</summary>${codeBlock(lesson.solution)}<p>Try changing the values and checking the behavior yourself.</p></details></section>
      <section class="lesson-summary"><p class="eyebrow">TAKEAWAY</p><p>${inlineMarkup(lesson.summary)}</p></section>
      <p class="lesson-reference">${link("/wiki/syntax", "Open the Z+ Syntax reference →")} · ${link("/wiki/uses", "See what Z+ is used for →")}</p>
      <div class="lesson-completion"><label><input id="lesson-complete" type="checkbox" ${progress.has(id) ? "checked" : ""}> <span>Mark this lesson complete</span></label><span id="save-status" role="status">Saved on this device</span></div>
      <nav class="lesson-pager" aria-label="Lesson navigation">${previous ? link(`/learn/${previous.id}`, `← Previous <small>${escapeHtml(previous.title)}</small>`, "pager-link") : `<span></span>`}${link("/learn", "Course overview", "pager-overview")}${next ? link(`/learn/${next.id}`, `Next lesson → <small>${escapeHtml(next.title)}</small>`, "pager-link pager-next") : link("/learn", "Finish course →", "pager-link pager-next")}</nav>
    </article></div>`;
  }

  function wikiIndexPage() {
    return `<section class="page-hero compact"><p class="eyebrow">Z+ Wiki</p><h1>Choose a place to begin.</h1><p>Look up the language, understand the project’s approach, or see what the current toolchain can be used for.</p></section><nav class="article-grid wiki-destinations" aria-label="Z+ Wiki destinations">${wiki.map((item, index) => `<a class="article-card" href="/wiki/${item.id}"><p class="eyebrow">0${index + 1} · ${escapeHtml(item.section)}</p><h2>${escapeHtml(item.title)}</h2><p>${escapeHtml(item.summary)}</p><span>Open destination →</span></a>`).join("")}</nav>`;
  }

  const docsGroups = [
    { title: "Start here", ids: ["introduction", "installation", "first-program"] },
    { title: "Language", ids: ["syntax", "variables", "functions", "classes", "visibility", "enums", "structs", "modules", "errors"] },
    { title: "Runtime", ids: ["interpreter", "native", "standard-library", "builtins", "zgraphics"] },
    { title: "Tools", ids: ["cli", "zpm", "zpbuild", "vscode"] }
  ];
  const docsOrder = docsGroups.flatMap(group => group.ids.map(id => docs.find(item => item.id === id)).filter(Boolean));

  function docsNavigation(activeId = "") {
    return `<aside class="docs-sidebar" aria-label="Documentation navigation"><label class="docs-search"><span aria-hidden="true">⌕</span><input type="search" id="docs-search" placeholder="Filter documentation" aria-label="Filter documentation"></label><div id="docs-tree">${docsGroups.map(group => `<section class="docs-group"><h2>${escapeHtml(group.title)}</h2><ul>${group.ids.map(id => docs.find(item => item.id === id)).filter(Boolean).map(item => `<li><a href="/docs/${item.id}" data-doc-link class="${item.id === activeId ? "active" : ""}" ${item.id === activeId ? 'aria-current="page"' : ""}>${escapeHtml(item.title)}</a></li>`).join("")}</ul></section>`).join("")}</div><p id="docs-search-empty" class="docs-search-empty" hidden>No matching guides. Try a shorter term.</p><a class="docs-backlink" href="/docs">All documentation</a></aside>`;
  }

  function documentationPage(item = null) {
    const current = item ? docsOrder.indexOf(item) : -1;
    const previous = current > 0 ? docsOrder[current - 1] : null;
    const next = current >= 0 ? docsOrder[current + 1] : null;
    const mainContent = item ? `<article class="docs-article"><div class="breadcrumbs">${link("/", "Home")} <span>/</span> ${link("/docs", "Documentation")} <span>/</span> ${escapeHtml(item.section || "Guide")}</div><header><p class="eyebrow">${escapeHtml(item.section || "Documentation")}</p><h1>${escapeHtml(item.title)}</h1><p class="article-lede">${escapeHtml(item.summary)}</p></header><div class="docs-prose">${item.body.map(([heading, text, sample]) => `<section id="${slug(heading)}"><h2>${escapeHtml(heading)}</h2><p>${inlineMarkup(text)}</p>${sample ? codeBlock(sample, item.title) : ""}</section>`).join("")}</div><div class="related-links"><strong>Continue</strong>${link("/learn", `${lessons.length}-lesson course`)}${link("/examples", "Runnable examples")}${link("/ecosystem", "Toolchain overview")}</div><nav class="docs-pager" aria-label="Documentation pages">${previous ? link(`/docs/${previous.id}`, `← ${escapeHtml(previous.title)}`) : "<span></span>"}${next ? link(`/docs/${next.id}`, `${escapeHtml(next.title)} →`) : "<span></span>"}</nav></article>`
      : `<section class="docs-index"><p class="eyebrow">Documentation · Language & tools</p><h1>Build a working picture of Z+.</h1><p class="article-lede">A practical guide to the language, its runtime, and the tools that take a project from source to release.</p><div class="docs-start"><a href="/docs/first-program"><span>START HERE</span><strong>Your first program</strong><small>Write, check, and run a Z+ file →</small></a><a href="/docs/syntax"><span>LANGUAGE REFERENCE</span><strong>Syntax reference</strong><small>Declarations, blocks, types, and expressions →</small></a></div>${docsGroups.map(group => `<section class="docs-index-group"><div><p class="eyebrow">${escapeHtml(group.title)}</p><h2>${escapeHtml(group.title === "Start here" ? "Get oriented" : group.title)}</h2></div><div>${group.ids.map(id => docs.find(doc => doc.id === id)).filter(Boolean).map(doc => `<a href="/docs/${doc.id}"><strong>${escapeHtml(doc.title)}</strong><span>${escapeHtml(doc.summary)}</span><b aria-hidden="true">↗</b></a>`).join("")}</div></section>`).join("")}</section>`;
    return `<div class="docs-layout">${docsNavigation(item?.id || "")}${mainContent}</div>`;
  }

  function ecosystemPage(product) {
    if (!product) return notFound();
    const componentKey = { compiler: "compiler", zpm: "zpm", zpbuild: "zpbuild", vscode: "vscodeExtension" }[product.id];
    const version = componentKey ? metadata.components[componentKey].version : null;
    const platformSummary = platforms => `<ul class="metadata-list">${platforms.map(item => `<li><strong>${escapeHtml(item.operatingSystem)} ${escapeHtml(item.architecture)}</strong> · ${escapeHtml(item.status)} · ${item.evidence.map(escapeHtml).join(", ")}</li>`).join("")}</ul>`;
    const sectionContent = section => {
      if (section.metadata === "platforms.compilerBuilds") return `<p>Compiler builds covered by the current CI matrix:</p>${platformSummary(metadata.platforms.compilerBuilds)}<p>Native backend status:</p>${platformSummary(metadata.platforms.nativeBackend)}`;
      if (section.metadata === "standardLibrary.modules") return `<p>Modules discovered from <code>${escapeHtml(metadata.standardLibrary.path)}</code>:</p><p class="module-list">${metadata.standardLibrary.modules.map(item => `<code>${escapeHtml(item)}</code>`).join(" · ")}</p>`;
      if (section.metadata === "compatibility.extensionToCompiler") {
        const compatibility = metadata.compatibility.extensionToCompiler;
        return `<p>Compatibility status: <strong>${escapeHtml(compatibility.status)}</strong>. Compiler range: ${escapeHtml(compatibility.compilerRange || "not declared")}. Standard-library interface: ${escapeHtml(compatibility.standardLibraryInterface || "not pinned")}. ${escapeHtml(compatibility.evidence)}</p>`;
      }
      return `<p>${inlineMarkup(section.text || "")}</p>`;
    };
    const eyebrow = version ? `${product.eyebrow} · ${version}` : product.eyebrow;
    return `<section class="product-heading"><div><p class="eyebrow">${escapeHtml(eyebrow)}</p><h1>${escapeHtml(product.title)}</h1><p class="article-lede">${escapeHtml(product.summary)}</p></div><div class="product-status"><span class="status-square"></span>${escapeHtml(product.status || "Current implementation details")}</div></section><div class="product-content">${product.sections.map((section, index) => `<section class="product-section"><div class="product-section-number">${String(index + 1).padStart(2, "0")}</div><div><h2>${escapeHtml(section.heading)}</h2>${sectionContent(section)}${section.code ? codeBlock(section.code, product.title) : ""}${section.href ? link(section.href, "Read the guide →", "text-link") : ""}${section.marketplaceLink ? `<a class="text-link" href="${escapeHtml(metadata.components.vscodeExtension.marketplaceUrl)}" target="_blank" rel="noreferrer">Install ${escapeHtml(metadata.components.vscodeExtension.marketplaceId)} ↗</a>` : ""}${section.action ? link(section.action.href, section.action.label, "button button-secondary") : ""}</div></section>`).join("")}<div class="product-related"><p class="eyebrow">Keep exploring</p>${link("/ecosystem", "Z+ ecosystem overview")} ${link("/docs", "Documentation index")} ${link("/download", "Downloads & releases")}</div></div>`;
  }

  function ecosystemIndexPage() {
    return `<section class="page-hero compact"><p class="eyebrow">Official Z+ developer tools</p><h1>The ecosystem around Z+.</h1><p>Each component has a clear role. Follow its guide for the supported workflows and current platform limits.</p></section><section class="ecosystem-index">${productList.map((product, index) => `<a href="/${product.id}" class="ecosystem-index-card"><span>${String(index + 1).padStart(2, "0")} / ${escapeHtml(product.eyebrow)}</span><h2>${escapeHtml(product.title)}</h2><p>${escapeHtml(product.summary)}</p><b>Open guide →</b></a>`).join("")}</section><section class="ecosystem-index-links">${link("/zts", "ZTS experimental interoperability →")} ${link("/zjs", "ZJS experimental interoperability →")} ${link("/packages", "Browse the package registry →")}</section>`;
  }

  function downloadProducts() {
    const components = metadata.components;
    const ciPlatforms = metadata.platforms.compilerBuilds.map(item => `${item.operatingSystem} ${item.architecture}`).join(", ");
    return `<section class="download-products"><div class="section-heading"><p class="eyebrow">Current distribution</p><h2>Tools and availability.</h2><p>Component versions come from their source declarations. Compiler release assets are loaded from the official release feed.</p></div><div class="download-product-grid">
      <article><span class="availability-pill">Release feed</span><h3>${escapeHtml(components.compiler.name)}</h3><p>Source version: <strong>${escapeHtml(components.compiler.version)}</strong>.</p><p id="download-compiler-version">Latest stable version is loading from official releases.</p><p>Builds are tested in CI for ${escapeHtml(ciPlatforms)}. Published installers are matched against actual release assets.</p>${link("/releases", "View published releases →")}</article>
      <article><span class="availability-pill">Bundled with distribution</span><h3>${escapeHtml(components.zpm.name)} <small>${escapeHtml(components.zpm.version)}</small></h3><p>Package manager supplied with the compiler distribution.</p>${link("/zpm", "ZPM guide →")}</article>
      <article><span class="availability-pill">Bundled with distribution</span><h3>${escapeHtml(components.zpbuild.name)} <small>${escapeHtml(components.zpbuild.version)}</small></h3><p>Project build and test tool supplied alongside the compiler. <code>zbuild</code> remains a compatibility alias.</p>${link("/zpbuild", "ZPBuild guide →")}</article>
      <article><span class="availability-pill">Marketplace</span><h3>${escapeHtml(components.vscodeExtension.name)} <small>${escapeHtml(components.vscodeExtension.version)}</small></h3><p>Install the independently versioned extension from the official Marketplace listing.</p><a class="text-link" href="${escapeHtml(components.vscodeExtension.marketplaceUrl)}" target="_blank" rel="noreferrer">Open Marketplace listing ↗</a></article>
    </div><p class="platform-note"><strong>Native backend status:</strong> ${metadata.platforms.nativeBackend.map(item => `${escapeHtml(item.operatingSystem)} ${escapeHtml(item.architecture)}: ${escapeHtml(item.status)}`).join("; ")}. Installer availability is determined from published assets, not platform build claims.</p></section>`;
  }

  function articlePage(item, kind, all, base, extraRelated = []) {
    if (!item) return notFound();
    const index = all.indexOf(item);
    const previous = all[index - 1];
    const next = all[index + 1];
    const sections = item.body;
    return `<article class="article-page"><div class="breadcrumbs">${link("/", "Home")} <span>/</span> ${link(base, kind)} <span>/</span> ${escapeHtml(item.title)}</div><div class="article-layout"><div><p class="eyebrow">${escapeHtml(item.section || kind)}</p><h1>${escapeHtml(item.title)}</h1><p class="article-lede">${escapeHtml(item.summary)}</p>
      ${sections.map(([heading, text, sample], sectionIndex) => `<section id="${slug(heading)}"><h2>${escapeHtml(heading)}</h2><p>${inlineMarkup(text)}</p>${sample ? codeExample(sample, heading) : ""}${kind === "Documentation" && sectionIndex === 0 ? `<p>${link("/learn", "Follow the step-by-step learning course")} · ${link("/examples", "Browse runnable examples")}</p>` : ""}</section>`).join("")}
      <div class="related-links"><strong>Related</strong>${link("/learn", "Learn Z+")}${link("/examples", "Examples")}${link(kind === "Wiki" ? "/docs" : "/wiki/syntax", kind === "Wiki" ? "Tool documentation" : "Z+ Syntax reference")}${extraRelated.map(item => link(item.href, item.label)).join("")}</div>
      <nav class="article-pager">${previous ? link(`${base}/${previous.id}`, `← ${escapeHtml(previous.title)}`) : "<span></span>"}${next ? link(`${base}/${next.id}`, `${escapeHtml(next.title)} →`) : "<span></span>"}</nav>
      </div><aside class="table-of-contents"><strong>ON THIS PAGE</strong><ol>${sections.map(([heading]) => `<li><a href="#${slug(heading)}">${escapeHtml(heading)}</a></li>`).join("")}</ol></aside></div></article>`;
  }

  function examplesPage() {
    return `<section class="page-hero compact"><p class="eyebrow">Learn by reading code</p><h1>Z+ examples</h1><p>Small programs and patterns based on implemented syntax and public APIs. Multi-file examples include their companion source files.</p></section><div class="examples-grid">${examples.map(item => `<article class="example-card" id="${item.id}"><div class="example-heading"><div><p class="eyebrow">EXAMPLE</p><h2>${escapeHtml(item.title)}</h2></div><a href="/learn" aria-label="Learn more">↗</a></div><p>${escapeHtml(item.summary)}</p>${codeBlock(item.code, "main.zp")}${(item.relatedFiles || []).map(file => codeBlock(file.code, file.name)).join("")}</article>`).join("")}</div>`;
  }

  function packagesPage() {
    return `<section class="page-hero compact"><p class="eyebrow">The Z+ ecosystem</p><h1>Packages for Z+</h1><p>Search real published packages, review release metadata, and install with ZPM.</p><div class="hero-actions">${buttonLink("/publish", "Publish a package")} ${buttonLink("/dashboard", "Developer dashboard", "button button-secondary")}</div></section>
      <section class="registry-layout">
        <div class="registry-main">
          <form id="registry-search-form" class="registry-search">
            <label for="registry-query">Search packages</label>
            <div><input id="registry-query" name="q" type="search" maxlength="100" placeholder="Name or description" autocomplete="off"><button class="button button-primary" type="submit">Search</button></div>
          </form>
          <div id="registry-results" class="registry-results" aria-live="polite"><p class="registry-message">Loading packages…</p></div>
        </div>
        <aside id="registry-account" class="registry-account" aria-live="polite"><p class="registry-message">Checking developer account…</p></aside>
      </section>`;
  }

  function authPage(mode) {
    const registering = mode === "register";
    return `<section class="page-hero compact auth-page-heading"><p class="eyebrow">Z+ PACKAGE REGISTRY</p><h1>${registering ? "Create your developer account" : "Welcome back"}</h1><p>${registering ? "Publish and manage Z+ packages with your registry account." : "Sign in to manage your packages and releases."}</p></section>
      <section class="auth-page">
        <div class="registry-panel auth-panel">
          <form data-registry-auth="${mode}" class="registry-auth-form">
            ${registering ? `<label>Username<input name="username" type="text" autocomplete="username" minlength="3" maxlength="32" pattern="[A-Za-z0-9][A-Za-z0-9_\\-]{2,31}" required><small>3–32 letters, numbers, underscores, or hyphens; start with a letter or number.</small></label>` : ""}
            <label>Email<input name="email" type="email" autocomplete="email" maxlength="254" required></label>
            <label>Password<span class="password-control"><input name="password" type="password" aria-label="Password" autocomplete="${registering ? "new-password" : "current-password"}" minlength="${registering ? "12" : "1"}" maxlength="1024" ${registering ? `aria-describedby="password-requirement"` : ""} required><button type="button" class="password-toggle" data-password-toggle aria-label="Show password" aria-pressed="false">Show</button></span>${registering ? `<small id="password-requirement">At least 12 characters are required (12 UTF-8 bytes minimum).</small>` : ""}</label>
            ${registering ? `<label>Confirm password<span class="password-control"><input name="passwordConfirmation" type="password" aria-label="Confirm password" autocomplete="new-password" minlength="12" maxlength="1024" data-confirm-password required><button type="button" class="password-toggle" data-password-toggle aria-label="Show password confirmation" aria-pressed="false">Show</button></span></label>` : ""}
            <button class="button button-primary" type="submit">${registering ? "Create account" : "Sign in"}</button>
          </form>
          <p id="registry-auth-status" class="registry-message" role="status" aria-live="polite"></p>
          <p class="auth-switch">${registering ? `Already registered? ${link("/login", "Sign in")}` : `New to the registry? ${link("/register", "Create an account")}`}</p>
        </div>
      </section>`;
  }

  function dashboardPage() {
    return `<section class="page-hero compact"><p class="eyebrow">DEVELOPER AREA</p><h1>Your dashboard</h1><p>Manage your registry account, published packages, and release history.</p></section>
      <section id="dashboard-content" class="dashboard-content" aria-live="polite">${loadingPanel("Loading developer account…")}</section>`;
  }

  function publishPage() {
    return `<section class="page-hero compact"><p class="eyebrow">DEVELOPER AREA</p><h1>Publish a package</h1><p>Upload a ZPM1 <code>.zpm</code> archive. The registry validates the archive, stores it privately, verifies its checksum, and confirms publication before reporting success.</p></section>
      <section id="publish-access" class="publish-content" aria-live="polite">${loadingPanel("Checking developer account…")}</section>`;
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
      <div><h2>${link(`/packages/${encodeURIComponent(item.name)}`, escapeHtml(item.name))}</h2><p>${escapeHtml(item.description || "No description provided.")}</p>${renderPackageTags(item.tags)}</div>
      <div class="registry-package-meta"><span>v${escapeHtml(item.version || "—")}</span><span>by ${escapeHtml(item.author || item.owner)}</span>${item.license ? `<span>${escapeHtml(item.license)}</span>` : ""}</div>
    </article>`).join("")}</div>`;
  }

  function renderPackageTags(tags) {
    const safeTags = Array.isArray(tags)
      ? [...new Set(tags.filter(tag => typeof tag === "string" && tag.trim()).map(tag => tag.trim().slice(0, 40)))].slice(0, 8)
      : [];
    return safeTags.length
      ? `<ul class="package-tags" aria-label="Package tags">${safeTags.map(tag => `<li>${escapeHtml(tag)}</li>`).join("")}</ul>`
      : "";
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

  function renderRegistryAccount(user, packages = []) {
    const target = document.querySelector("#registry-account");
    if (!target) return;
    if (!user) {
      target.innerHTML = `<section class="registry-panel">
        <p class="eyebrow">DEVELOPER ACCESS</p><h2>Publish with ZPM</h2>
        <p>Sign in to manage package ownership and publish validated <code>.zpm</code> releases.</p>
        <div class="account-actions">${buttonLink("/login", "Sign in")} ${buttonLink("/register", "Create account", "button button-secondary")}</div>
      </section>`;
      return;
    }
    target.innerHTML = `<section class="registry-panel">
      <p class="eyebrow">DEVELOPER ACCOUNT</p><h2>${escapeHtml(user.username)}</h2>
      <p>Signed in as ${escapeHtml(user.email)}.</p>
      <div class="account-actions">${buttonLink("/dashboard", "Open dashboard")} ${buttonLink("/publish", "Publish package", "button button-secondary")}</div>
      <button class="button button-quiet" type="button" data-registry-logout>Sign out</button>
      <p class="registry-subheading">Packages you manage</p>
      ${packages.length ? `<ul class="registry-managed-list">${packages.map(item => `<li>${link(`/packages/${encodeURIComponent(item.name)}`, escapeHtml(item.name))}${item.is_owner ? " · owner" : " · maintainer"}</li>`).join("")}</ul>` : `<p class="registry-message">No packages are associated with this account yet.</p>`}
    </section>`;
  }

  async function getRegistryAccount() {
    const response = await fetch("/api/registry/auth/me", {
      credentials: "same-origin",
      headers: { Accept: "application/json" }
    });
    if (response.status === 401) return null;
    const result = await registryJson(response, "Unable to load account.");
    return result.user;
  }

  async function loadRegistryAccount() {
    const target = document.querySelector("#registry-account");
    if (!target) return;
    try {
      const user = await getRegistryAccount();
      if (!user) {
        renderRegistryAccount(null);
        return;
      }
      const packageResult = await registryRequest("/api/registry/me/packages", {
        credentials: "same-origin",
        headers: { Accept: "application/json" }
      }, "Unable to load managed packages.");
      renderRegistryAccount(user, packageResult.packages || []);
    } catch (error) {
      if (target.isConnected) {
        target.innerHTML = `<section class="registry-panel"><p class="registry-message registry-error">${escapeHtml(error.message)}</p></section>`;
      }
    }
  }

  function dashboardMarkup(user, packageResult) {
    const packages = packageResult.packages || [];
    const recentPublications = packageResult.recentPublications || [];
    return `<div class="dashboard-heading">
        <div><p class="eyebrow">ACCOUNT</p><h2>${escapeHtml(user.username)}</h2><p>${escapeHtml(user.email)} · Session active</p></div>
        <div class="dashboard-actions">${buttonLink("/publish", "Publish a package")}<button class="button button-secondary" type="button" data-registry-logout>Sign out</button></div>
      </div>
      <div class="dashboard-stats"><article><span>Managed packages</span><strong>${packages.length}</strong></article><article><span>Recent releases</span><strong>${recentPublications.length}</strong></article></div>
      <div class="dashboard-grid">
        <section class="dashboard-panel"><div class="dashboard-panel-heading"><div><p class="eyebrow">PACKAGE MANAGEMENT</p><h2>Published packages</h2></div><a class="text-link" href="/publish">Publish new →</a></div>
          ${packages.length ? `<ul class="dashboard-package-list">${packages.map(item => `<li><div><strong>${link(`/packages/${encodeURIComponent(item.name)}`, escapeHtml(item.name))}</strong><span>${escapeHtml(item.description || "No description provided.")}</span></div><small>${item.is_owner ? "Owner" : "Maintainer"}</small></li>`).join("")}</ul>` : `<div class="empty-state"><h3>Your registry is ready</h3><p>You haven’t published or joined a package yet. Upload a ZPM archive to start sharing a package.</p>${buttonLink("/publish", "Publish your first package")}</div>`}
        </section>
        <section class="dashboard-panel"><div class="dashboard-panel-heading"><div><p class="eyebrow">PUBLICATION HISTORY</p><h2>Recent releases</h2></div></div>
          ${recentPublications.length ? `<ol class="publication-list">${recentPublications.map(item => `<li><span class="publication-marker" aria-hidden="true"></span><div><strong>${link(`/packages/${encodeURIComponent(item.name)}`, escapeHtml(item.name))} <span>v${escapeHtml(item.version)}</span></strong><p>Published by ${escapeHtml(item.publisher)} · ${escapeHtml(new Date(item.publishedAt).toLocaleDateString())}</p></div></li>`).join("")}</ol>` : `<p class="registry-message">${packages.length ? "No published versions are available yet." : "Published releases will appear here after your first successful upload."}</p>`}
        </section>
      </div>`;
  }

  async function loadDeveloperDashboard() {
    const target = document.querySelector("#dashboard-content");
    if (!target) return;
    try {
      const user = await getRegistryAccount();
      if (!target.isConnected) return;
      if (!user) {
        target.innerHTML = `<section class="state-panel"><div><h2>Sign in to continue</h2><p>The developer dashboard requires a registry account.</p><div class="account-actions">${buttonLink("/login", "Sign in")} ${buttonLink("/register", "Create account", "button button-secondary")}</div></div></section>`;
        return;
      }
      const packageResult = await registryRequest("/api/registry/me/packages", {
        credentials: "same-origin",
        headers: { Accept: "application/json" }
      }, "Unable to load your packages.");
      if (target.isConnected) target.innerHTML = dashboardMarkup(user, packageResult);
    } catch (error) {
      if (target.isConnected) target.innerHTML = `<section class="state-panel state-warning" role="alert"><div><h2>Dashboard unavailable</h2><p>${escapeHtml(error.message)}</p></div></section>`;
    }
  }

  async function loadPublishAccess() {
    const target = document.querySelector("#publish-access");
    if (!target) return;
    try {
      const user = await getRegistryAccount();
      if (!target.isConnected) return;
      if (!user) {
        target.innerHTML = `<section class="state-panel"><div><h2>Sign in to publish</h2><p>Publishing requires an authenticated developer account.</p><div class="account-actions">${buttonLink("/login", "Sign in")} ${buttonLink("/register", "Create account", "button button-secondary")}</div></div></section>`;
        return;
      }
      target.innerHTML = `<div class="publish-account"><span class="live-dot" aria-hidden="true"></span><p>Publishing as <strong>${escapeHtml(user.username)}</strong> (${escapeHtml(user.email)})</p><a href="/dashboard">Dashboard</a></div>
        <form id="registry-publish-form" class="registry-panel registry-publish-form">
          <label for="package-archive">ZPM package archive</label>
          <p class="form-help">Choose a <code>.zpm</code> file created with <code>zpm pack</code>. Maximum size: 4 MiB.</p>
          <input id="package-archive" name="archive" type="file" accept=".zpm,application/vnd.zpm.archive" required>
          <div id="publish-preview" class="publish-preview" aria-live="polite"><p>Select a package archive to inspect its manifest.</p></div>
          <div class="upload-progress" id="upload-progress" hidden><label for="publish-progress">Upload progress</label><progress id="publish-progress" max="100" value="0">0%</progress><span id="publish-progress-label">0%</span></div>
          <button id="publish-submit" class="button button-primary" type="submit" disabled>Publish package</button>
          <p id="registry-publish-status" class="registry-message" role="status" aria-live="polite"></p>
        </form>`;
    } catch (error) {
      if (target.isConnected) target.innerHTML = `<section class="state-panel state-warning" role="alert"><div><h2>Account check failed</h2><p>${escapeHtml(error.message)}</p><button class="button button-secondary" type="button" data-retry-publish-access>Try again</button></div></section>`;
    }
  }

  function packageDetailsMarkup(item, user) {
    const latest = item.versions[0];
    const packageTags = latest?.manifest?.tags || latest?.manifest?.keywords || latest?.manifest?.categories;
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
      ${renderPackageTags(packageTags)}
      <section class="package-metadata"><h2>Package metadata</h2><dl>
        <div><dt>Entry module</dt><dd><code>${escapeHtml(latest?.main || "Not specified")}</code></dd></div>
        <div><dt>Compiler</dt><dd>${escapeHtml(latest?.compiler || "Unspecified")}</dd></div>
        <div><dt>Dependencies</dt><dd>${Object.keys(latest?.dependencies || {}).length ? Object.entries(latest.dependencies).map(([name, range]) => `${escapeHtml(name)} ${escapeHtml(range)}`).join(", ") : "None"}</dd></div>
      </dl></section>
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

  async function inspectPackageArchive(file) {
    if (file.size > 4 * 1024 * 1024) throw new Error("Registry packages are limited to 4 MiB.");
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (bytes.length < 8 || new TextDecoder().decode(bytes.subarray(0, 4)) !== "ZPM1") {
      throw new Error("This file is not a supported ZPM1 package archive.");
    }
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const count = view.getUint32(4, true);
    if (!count || count > 10000) throw new Error("The package archive has an invalid entry count.");
    let offset = 8;
    const paths = new Set();
    let manifestBytes;
    let hasZPlusSource = false;
    for (let index = 0; index < count; index++) {
      if (offset + 12 > bytes.length) throw new Error("The package archive is truncated.");
      const pathLength = view.getUint32(offset, true);
      const size = view.getBigUint64(offset + 4, true);
      offset += 12;
      if (!pathLength || pathLength > 4096 || size > BigInt(4 * 1024 * 1024) ||
          BigInt(offset) + BigInt(pathLength) + size > BigInt(bytes.length)) {
        throw new Error("The package archive contains an invalid or truncated entry.");
      }
      const pathEnd = offset + pathLength;
      let name;
      try {
        name = new TextDecoder("utf-8", { fatal: true }).decode(bytes.subarray(offset, pathEnd));
      } catch {
        throw new Error("The package archive contains a file path that is not valid UTF-8.");
      }
      const components = name.split("/");
      if (!name || name.startsWith("/") || name.includes("\\") ||
          components.some(component => !component || component === "." || component === "..")) {
        throw new Error("The package archive contains an unsafe file path.");
      }
      const normalizedName = name.toLowerCase();
      if (paths.has(normalizedName)) throw new Error("The package archive contains duplicate file paths.");
      paths.add(normalizedName);
      offset = pathEnd;
      const sizeNumber = Number(size);
      if (name === "zpm.json") {
        if (sizeNumber > 1024 * 1024) throw new Error("The package manifest exceeds the 1 MiB limit.");
        manifestBytes = bytes.slice(offset, offset + sizeNumber);
      }
      if (/\.(zh|zp)$/i.test(name)) hasZPlusSource = true;
      offset += sizeNumber;
    }
    if (offset !== bytes.length) throw new Error("The package archive contains trailing data.");
    if (!manifestBytes) throw new Error("The archive must contain a root zpm.json manifest.");
    let manifest;
    try {
      manifest = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(manifestBytes));
    } catch {
      throw new Error("The package manifest is not valid UTF-8 JSON.");
    }
    if (!manifest || typeof manifest !== "object" || Array.isArray(manifest)) {
      throw new Error("The package manifest must be a JSON object.");
    }
    if (!/^[a-z](?:[a-z0-9-]{0,62}[a-z0-9])?$/.test(manifest.name || "")) {
      throw new Error("The package manifest has an invalid package name.");
    }
    if (!/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:[-+][0-9A-Za-z.-]+)?$/.test(manifest.version || "")) {
      throw new Error("The package manifest has an invalid semantic version.");
    }
    if (typeof manifest.main !== "string" || !manifest.main.endsWith(".zh") || !paths.has(manifest.main.toLowerCase())) {
      throw new Error("The package manifest main field must name an included .zh module.");
    }
    if (!hasZPlusSource) throw new Error("The archive must contain at least one Z+ source file.");
    return { manifest, entryCount: count };
  }

  async function inspectSelectedArchive(input) {
    const sequence = ++publishSelectionSequence;
    const file = input.files?.[0];
    const preview = document.querySelector("#publish-preview");
    const submit = document.querySelector("#publish-submit");
    const status = document.querySelector("#registry-publish-status");
    if (!preview || !submit) return;
    submit.disabled = true;
    if (!file) {
      preview.innerHTML = "<p>Select a package archive to inspect its manifest.</p>";
      if (status) status.textContent = "";
      return;
    }
    preview.innerHTML = `<p class="registry-message">Inspecting ${escapeHtml(file.name)}…</p>`;
    if (status) status.textContent = "";
    try {
      const { manifest, entryCount } = await inspectPackageArchive(file);
      if (sequence !== publishSelectionSequence || !preview.isConnected) return;
      const size = `${(file.size / 1024).toFixed(1)} KiB`;
      preview.innerHTML = `<div class="publish-validation"><span class="validation-mark" aria-hidden="true">✓</span><div><strong>Archive structure recognized</strong><p>${escapeHtml(file.name)} · ${size} · ${entryCount} files</p><dl><div><dt>Package</dt><dd>${escapeHtml(manifest.name)}</dd></div><div><dt>Version</dt><dd>${escapeHtml(manifest.version)}</dd></div><div><dt>Description</dt><dd>${escapeHtml(manifest.description || "No description provided.")}</dd></div></dl><p class="form-help">The registry performs authoritative metadata, path, duplicate-version, storage, and checksum validation before confirming publication.</p></div></div>`;
      submit.disabled = false;
    } catch (error) {
      if (sequence !== publishSelectionSequence || !preview.isConnected) return;
      preview.innerHTML = `<p class="registry-message registry-error" role="alert">${escapeHtml(error.message)}</p>`;
      if (status) status.textContent = "Choose a valid ZPM package archive to continue.";
    }
  }

  function uploadArchiveWithProgress(file, onProgress) {
    return new Promise((resolve, reject) => {
      const request = new XMLHttpRequest();
      request.open("POST", "/api/registry/publish");
      request.withCredentials = true;
      request.timeout = 120000;
      request.setRequestHeader("Accept", "application/json");
      request.setRequestHeader("Content-Type", "application/vnd.zpm.archive");
      request.upload.addEventListener("progress", event => {
        onProgress(event.lengthComputable ? Math.round(event.loaded / event.total * 100) : null);
      });
      request.addEventListener("load", () => {
        let result;
        try {
          result = JSON.parse(request.responseText);
        } catch {
          reject(new Error("The package registry did not return JSON. It may be unavailable."));
          return;
        }
        if (request.status < 200 || request.status >= 300) {
          reject(new Error(result.error?.message || "Package publishing failed."));
          return;
        }
        resolve(result);
      });
      request.addEventListener("error", () => reject(new Error("The upload could not reach the package registry. Check your connection and retry.")));
      request.addEventListener("timeout", () => reject(new Error("The package upload timed out. You can safely retry.")));
      request.addEventListener("abort", () => reject(new Error("The package upload was cancelled.")));
      request.send(file);
    });
  }

  function notFound() {
    return `<section class="page-hero compact"><p class="eyebrow">404 · NOT FOUND</p><h1>This page isn't in the guide.</h1><p>The requested Z+ route may have moved. Start from the course, reference, or homepage.</p><div class="hero-actions">${buttonLink("/", "Go home")} ${buttonLink("/learn", "Browse lessons", "button button-secondary")}</div></section>`;
  }

  function render() {
    if (!metadata) return;
    const route = path();
    main.classList.remove("page-enter");
    void main.offsetWidth;
    main.classList.add("page-enter");
    document.querySelectorAll(".primary-nav a").forEach(anchor => {
      const active = route === anchor.pathname || (anchor.pathname === "/learn" && route.startsWith("/learn/")) ||
        (anchor.pathname === "/docs" && route.startsWith("/docs/")) || (anchor.pathname === "/wiki" && route.startsWith("/wiki/")) ||
        (anchor.pathname === "/zts" && route === "/zts") || (anchor.pathname === "/zjs" && route === "/zjs") ||
        (anchor.pathname === "/compiler" && route === "/compiler") || (anchor.pathname === "/ecosystem" && route === "/ecosystem") ||
        (anchor.pathname === "/releases" && ["/download", "/releases"].includes(route)) ||
        (anchor.pathname === "/packages" && (route.startsWith("/packages") || route === "/publish" || route === "/dashboard")) ||
        (anchor.pathname === "/dashboard" && ["/dashboard", "/publish", "/login", "/register"].includes(route));
      anchor.classList.toggle("nav-active", active);
      if (active) anchor.setAttribute("aria-current", "page");
      else anchor.removeAttribute("aria-current");
    });

    if (route === "/") {
      main.innerHTML = homePage();
      document.title = "Z+ — A language, from first program to real projects";
    } else if (route === "/download") {
      downloadPage();
      document.title = "Downloads — Z+";
    } else if (route === "/releases") {
      downloadPage(true);
      document.title = "Published releases — Z+";
    } else if (route === "/ecosystem") {
      main.innerHTML = ecosystemIndexPage();
      document.title = "Z+ ecosystem — Compiler, ZPM, ZPBuild and more";
    } else if (["compiler", "zpm", "zpbuild", "vscode", "zgraphics"].includes(route.slice(1)) && route.split("/").length === 2) {
      const product = products[route.slice(1)];
      main.innerHTML = ecosystemPage(product);
      document.title = `${product.title} — Z+`;
    } else if (route === "/learn") {
      main.innerHTML = learnDashboard();
      document.title = "Learn Z+ — Course";
    } else if (route.startsWith("/learn/")) {
      const id = route.slice("/learn/".length);
      const lesson = lessonById.get(id);
      main.innerHTML = lessonPage(id);
      document.title = lesson ? `${lesson.title} — Learn Z+` : "Lesson not found — Z+";
    } else if (route === "/docs") {
      main.innerHTML = documentationPage();
      document.title = "Documentation — Z+ Language and Tools";
    } else if (route.startsWith("/docs/")) {
      const item = docs.find(article => article.id === route.slice(6));
      main.innerHTML = item ? documentationPage(item) : notFound();
      document.title = item ? `${item.title} — Z+ Documentation` : "Not found — Z+";
    } else if (route === "/wiki") {
      main.innerHTML = wikiIndexPage();
      document.title = "Z+ Wiki — Syntax, ideas, and uses";
    } else if (route.startsWith("/wiki/")) {
      const item = wiki.find(article => article.id === route.slice(6));
      main.innerHTML = articlePage(item, "Wiki", wiki, "/wiki");
      document.title = item ? `${item.title} — Z+ Wiki` : "Not found — Z+";
    } else if (route === "/zts") {
      main.innerHTML = articlePage(zts, "ZTS", [zts], "/zts", [{ href: "/zjs", label: "ZJS JavaScript guide" }]);
      document.title = "ZTS TypeScript interoperability — Z+";
    } else if (route === "/zjs") {
      main.innerHTML = articlePage(zjs, "ZJS", [zjs], "/zjs", [{ href: "/zts", label: "ZTS TypeScript guide" }]);
      document.title = "ZJS JavaScript interoperability — Z+";
    } else if (route === "/examples") {
      main.innerHTML = examplesPage();
      document.title = "Examples — Z+";
    } else if (route === "/login" || route === "/register") {
      const mode = route.slice(1);
      main.innerHTML = authPage(mode);
      document.title = mode === "login" ? "Sign in — Z+ Registry" : "Create account — Z+ Registry";
    } else if (route === "/dashboard") {
      main.innerHTML = dashboardPage();
      document.title = "Developer dashboard — Z+ Registry";
      void loadDeveloperDashboard();
    } else if (route === "/publish") {
      main.innerHTML = publishPage();
      document.title = "Publish a package — Z+ Registry";
      void loadPublishAccess();
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
    const descriptions = {
      "/": "The official Z+ language website: learn the language, explore developer tools, and find the latest published compiler release.",
      "/download": "See actual Z+ compiler release assets, platform availability, and versions of bundled developer tools.",
      "/releases": "Browse published Z+ releases and read their original, sanitized release notes.",
      "/docs": "A navigable guide to Z+ syntax, types, modules, runtime behavior, ZPM, ZPBuild, and editor tooling.",
      "/wiki": "Explore the Z+ language syntax reference, the ideas behind Z+, and practical uses of the current toolchain.",
      "/ecosystem": "Explore the Z+ compiler, ZPM package manager, ZPBuild, VS Code extension, and ZGraphics.",
      "/compiler": products.compiler.summary,
      "/zpm": products.zpm.summary,
      "/zpbuild": products.zpbuild.summary,
      "/vscode": products.vscode.summary,
      "/zgraphics": products.zgraphics.summary,
      "/learn": "Follow 54 lessons to learn implemented Z+ syntax, with examples and exercises.",
      "/examples": "Read runnable Z+ examples for functions, files, modules, arrays, and more.",
      "/packages": "Browse and inspect packages published to the Z+ package registry."
    };
    const description = descriptions[route] || (route.startsWith("/docs/") ? docs.find(item => item.id === route.slice(6))?.summary
      : route.startsWith("/learn/") ? lessonById.get(route.slice(7))?.explain
      : route.startsWith("/wiki/") ? wiki.find(item => item.id === route.slice(6))?.summary
      : route.startsWith("/packages") ? "Browse and publish Z+ packages through the official package registry."
      : route === "/zts" ? zts.summary
      : route === "/zjs" ? zjs.summary
      : route === "/wiki" ? "Guides to Z+ architecture, modules, and project workflows."
      : route === "/publish" ? "Publish a validated ZPM package archive to the Z+ registry."
      : route === "/dashboard" ? "Manage your Z+ packages and view publication history."
      : "Official Z+ documentation, examples, and developer tools.");
    const metaDescription = description || "Official Z+ documentation, examples, and developer tools.";
    document.querySelector('meta[name="description"]').setAttribute("content", metaDescription);
    document.querySelector('meta[property="og:title"]').setAttribute("content", document.title);
    document.querySelector('meta[property="og:description"]').setAttribute("content", metaDescription);
    const canonical = new URL(route, metadata.project.website).href;
    document.querySelector('meta[property="og:url"]').setAttribute("content", canonical);
    document.querySelector('link[rel="canonical"]').setAttribute("href", canonical);
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
      search.setAttribute("aria-expanded", "false");
      searchResults.innerHTML = "";
      return;
    }
    const results = contentIndex.filter(item => `${item.title} ${item.summary} ${item.type}`.toLowerCase().includes(query)).slice(0, 8);
    searchResults.innerHTML = results.length ? results.map(item => `<a role="option" href="${item.href}"><span>${escapeHtml(item.type)}</span><strong>${escapeHtml(item.title)}</strong><small>${escapeHtml(item.summary)}</small></a>`).join("") : `<p class="search-empty">No matching Z+ pages. Try a topic such as arrays, modules, or errors.</p>`;
    searchResults.hidden = false;
    search.setAttribute("aria-expanded", "true");
  }

  async function submitRegistryAuth(form) {
    const status = document.querySelector("#registry-auth-status");
    const button = form.querySelector('button[type="submit"]');
    const mode = form.dataset.registryAuth;
    const values = Object.fromEntries(new FormData(form));
    if (mode === "register" && values.password !== values.passwordConfirmation) {
      if (status) status.textContent = "Passwords do not match.";
      form.elements.passwordConfirmation.focus();
      return;
    }
    if (mode === "register") {
      const passwordBytes = new TextEncoder().encode(String(values.password)).length;
      if (passwordBytes < 12 || passwordBytes > 1024) {
        if (status) status.textContent = "Use a password between 12 and 1024 UTF-8 bytes.";
        return;
      }
    }
    delete values.passwordConfirmation;
    button.disabled = true;
    if (status) status.textContent = mode === "register" ? "Creating account…" : "Signing in…";
    try {
      await registryRequest(`/api/registry/auth/${mode}`, {
        method: "POST",
        credentials: "same-origin",
        headers: {
          Accept: "application/json",
          "Content-Type": "application/json"
        },
        body: JSON.stringify(values)
      }, mode === "register" ? "Registration failed." : "Sign-in failed.");
      navigate("/dashboard");
    } catch (error) {
      if (status?.isConnected) status.textContent = error.message;
    } finally {
      if (button.isConnected) button.disabled = false;
    }
  }

  async function submitRegistryPublish(form) {
    const status = document.querySelector("#registry-publish-status");
    const button = form.querySelector('button[type="submit"]');
    const file = form.elements.archive.files[0];
    if (!file) return;
    let succeeded = false;
    button.disabled = true;
    status.textContent = "Validating package archive…";
    const progressPanel = document.querySelector("#upload-progress");
    const progress = document.querySelector("#publish-progress");
    const progressLabel = document.querySelector("#publish-progress-label");
    if (progressPanel) progressPanel.hidden = true;
    try {
      await inspectPackageArchive(file);
      if (progressPanel) progressPanel.hidden = false;
      if (progress) {
        progress.value = 0;
        progress.setAttribute("value", "0");
        progress.textContent = "0%";
        progress.setAttribute("aria-valuetext", "0% uploaded");
      }
      if (status) status.textContent = "Uploading archive…";
      const result = await uploadArchiveWithProgress(file, percent => {
        if (progress) {
          if (percent === null) {
            progress.removeAttribute("value");
            progress.removeAttribute("aria-valuetext");
            progress.textContent = "Uploading…";
          } else {
            progress.value = percent;
            progress.setAttribute("value", String(percent));
            progress.textContent = `${percent}%`;
            progress.setAttribute("aria-valuetext", `${percent}% uploaded`);
          }
        }
        if (progressLabel) progressLabel.textContent = percent === null ? "Uploading…" : `${percent}% uploaded`;
      });
      succeeded = true;
      if (progress) {
        progress.value = 100;
        progress.setAttribute("value", "100");
        progress.textContent = "100%";
        progress.setAttribute("aria-valuetext", "Upload complete, 100 percent");
      }
      if (progressLabel) progressLabel.textContent = "Upload complete";
      const packageHref = `/packages/${encodeURIComponent(result.name)}`;
      if (status) {
        status.innerHTML = `Published ${escapeHtml(result.name)} ${escapeHtml(result.version)} successfully. ${link(packageHref, "View package", "text-link")}${result.integrity ? `<span class="publish-integrity">SHA-256 ${escapeHtml(result.integrity)}</span>` : ""}`;
      }
      form.reset();
      const preview = document.querySelector("#publish-preview");
      if (preview) preview.innerHTML = `<p>Publication confirmed by the registry. ${link(packageHref, "Open package details →", "text-link")}</p>`;
      button.disabled = true;
      if (document.querySelector("#registry-results")) void loadRegistryPackages();
    } catch (error) {
      if (status?.isConnected) status.textContent = error.message;
    } finally {
      if (button.isConnected && !succeeded) button.disabled = false;
      if (progressPanel?.isConnected && !succeeded) progressPanel.hidden = true;
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
    if (!window.confirm(`Remove ${username} as a maintainer of ${packageName}?`)) return;
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
    if (copy) void copyCode(copy);
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
        () => {
          if (path() === "/dashboard" || path() === "/publish") navigate("/login");
          else loadRegistryAccount();
        },
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
    const passwordToggle = event.target.closest("[data-password-toggle]");
    if (passwordToggle) {
      const input = passwordToggle.parentElement.querySelector("input");
      const showing = input.type === "password";
      input.type = showing ? "text" : "password";
      passwordToggle.textContent = showing ? "Hide" : "Show";
      passwordToggle.setAttribute("aria-label", `${showing ? "Hide" : "Show"} ${input.name === "passwordConfirmation" ? "password confirmation" : "password"}`);
      passwordToggle.setAttribute("aria-pressed", String(showing));
    }
    const retryPublishAccess = event.target.closest("[data-retry-publish-access]");
    if (retryPublishAccess) void loadPublishAccess();
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
    if (event.target.id === "docs-search") {
      const query = event.target.value.trim().toLowerCase();
      let visible = 0;
      document.querySelectorAll("[data-doc-link], .docs-index-group a").forEach(anchor => {
        const matches = !query || anchor.textContent.toLowerCase().includes(query);
        anchor.hidden = !matches;
        if (matches) visible += 1;
      });
      document.querySelectorAll(".docs-index-group").forEach(group => {
        group.hidden = !group.querySelector("a:not([hidden])");
      });
      const empty = document.querySelector("#docs-search-empty");
      if (empty) empty.hidden = visible > 0;
      return;
    }
    if (event.target.matches("[data-confirm-password]")) {
      const password = event.target.form.elements.password.value;
      event.target.setCustomValidity(event.target.value === password ? "" : "Passwords do not match.");
      return;
    }
    if (event.target.id !== "registry-query") return;
    clearTimeout(registrySearchTimer);
    registrySearchTimer = setTimeout(
      () => loadRegistryPackages(event.target.value.trim()),
      250
    );
  });

  document.addEventListener("change", event => {
    if (event.target.id === "package-archive") {
      void inspectSelectedArchive(event.target);
      return;
    }
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
    event.currentTarget.setAttribute("aria-label", open ? "Close navigation" : "Open navigation");
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

  if (window.ZPlusMetadata) {
    configureMetadata(window.ZPlusMetadata);
    renderFooter();
    render();
  } else {
    fetch("/ecosystem-metadata.json", { headers: { Accept: "application/json" } }).then(response => {
      if (!response.ok) throw new Error(`Metadata request failed with HTTP ${response.status}.`);
      return response.json();
    }).then(value => {
      configureMetadata(value);
      renderFooter();
      render();
    }).catch(error => {
      main.innerHTML = `<section class="state-panel state-warning"><h1>Project metadata is unavailable.</h1><p>${escapeHtml(error.message)}</p></section>`;
      console.error("Unable to load generated Z+ ecosystem metadata.", error);
    });
  }
})();
