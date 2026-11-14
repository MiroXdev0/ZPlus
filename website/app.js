(() => {
  const { course, docs, wiki, examples } = window.ZPlusSite;
  const main = document.querySelector("#main");
  const footer = document.querySelector("#site-footer");
  const search = document.querySelector("#site-search");
  const searchResults = document.querySelector("#search-results");
  const releasesUrl = "https://github.com/MiroXdev0/ZPluse-release/releases";
  const releasesApi = "https://api.github.com/repos/MiroXdev0/ZPluse-release/releases?per_page=30";
  const progressKey = "zplus-learning-progress-v1";
  let releaseRequest;

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
      <a class="brand" href="/"><span class="brand-mark">Z+</span><span class="brand-word">Language</span></a>
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

  async function fetchReleases() {
    if (!releaseRequest) {
      releaseRequest = fetch(releasesApi, { headers: { Accept: "application/vnd.github+json" } }).then(async response => {
        if (!response.ok) throw new Error(`GitHub returned HTTP ${response.status}`);
        const data = await response.json();
        if (!Array.isArray(data)) throw new Error("The GitHub Releases response was not a release list.");
        return data.filter(release => !release.draft);
      });
    }
    return releaseRequest;
  }

  function releaseCard(release, prominent = false) {
    const assets = Array.isArray(release.assets) ? release.assets : [];
    const releaseDate = release.published_at ? new Date(release.published_at).toLocaleDateString(undefined, { year: "numeric", month: "long", day: "numeric" }) : "";
    return `<article class="release-card ${prominent ? "release-featured" : ""}">
      <div class="release-heading"><div><p class="eyebrow">${release.prerelease ? "Prerelease" : prominent ? "Latest stable" : "Stable release"}${releaseDate ? ` · ${escapeHtml(releaseDate)}` : ""}</p><h3>${escapeHtml(release.tag_name || release.name || "Untitled release")}</h3></div>${release.html_url ? `<a class="subtle-link" href="${escapeHtml(release.html_url)}" target="_blank" rel="noreferrer">Release notes ↗</a>` : ""}</div>
      ${release.name && release.name !== release.tag_name ? `<p>${escapeHtml(release.name)}</p>` : ""}
      ${release.body ? `<details class="release-notes"><summary>Read release notes</summary><pre>${escapeHtml(release.body)}</pre></details>` : `<p class="muted">No release notes were published.</p>`}
      <div class="asset-list">${assets.length ? assets.map(asset => `<a class="asset-link" href="${escapeHtml(asset.browser_download_url)}" target="_blank" rel="noreferrer"><span class="download-symbol">↓</span><span><strong>${escapeHtml(asset.name)}</strong><small>${formatBytes(asset.size)}</small></span><b>Download ↗</b></a>`).join("") : `<p class="muted">No downloadable assets are attached to this release.</p>`}</div>
    </article>`;
  }

  function formatBytes(size) {
    if (!Number.isFinite(size) || size < 0) return "Size unavailable";
    if (size < 1024) return `${size} B`;
    if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`;
    return `${(size / (1024 * 1024)).toFixed(1)} MB`;
  }

  async function downloadPage() {
    main.innerHTML = `<section class="page-hero compact"><p class="eyebrow">Official builds</p><h1>Download Z+</h1><p>Release information and downloads come directly from the official GitHub Releases API. This page never guesses an asset URL.</p></section><div id="release-content">${loadingPanel("Checking the official release feed…")}</div>`;
    try {
      const releases = await fetchReleases();
      const stable = releases.filter(release => !release.prerelease).sort((a, b) => Date.parse(b.published_at || b.created_at) - Date.parse(a.published_at || a.created_at));
      const prereleases = releases.filter(release => release.prerelease).sort((a, b) => Date.parse(b.published_at || b.created_at) - Date.parse(a.published_at || a.created_at));
      const target = document.querySelector("#release-content");
      const howTo = `<section class="install-guide"><div><p class="eyebrow">After downloading</p><h2>Install, then verify.</h2><ol><li>Download the Windows installer or distribution archive published with a stable release.</li><li>Run the installer, or extract the complete archive and follow its included installation instructions.</li><li>Open a new PowerShell window and run <code>zp --version</code>.</li><li>Check and run a source file with <code>zp check hello.zp</code> and <code>zp run hello.zp</code>.</li></ol></div>${codeBlock(`start {
    say("Z+ is ready.")
} stop`, "hello.zp")}</section>`;
      if (!stable.length) {
        target.innerHTML = `<div class="state-panel state-warning"><span class="state-icon">!</span><div><h2>No stable release is currently published.</h2><p>The official GitHub API returned no published stable releases. We will not substitute a guessed version or download link.</p><a class="button button-primary" href="${releasesUrl}" target="_blank" rel="noreferrer">Open official Releases ↗</a></div></div>
          ${prereleases.length ? `<section class="release-section"><div class="section-heading"><p class="eyebrow">For testing</p><h2>Prereleases</h2><p>Prereleases may change and are not the recommended starting point.</p></div>${prereleases.map(item => releaseCard(item)).join("")}</section>` : ""}
          ${howTo}`;
        return;
      }
      target.innerHTML = `<section class="release-section"><div class="section-heading"><p class="eyebrow">Current stable</p><h2>Choose a published build.</h2><p>Version, notes, and asset links are supplied by GitHub at page load.</p></div>${releaseCard(stable[0], true)}</section>
        ${stable.length > 1 ? `<section class="release-section"><div class="section-heading"><p class="eyebrow">History</p><h2>Previous stable releases</h2></div><div class="release-list">${stable.slice(1).map(item => releaseCard(item)).join("")}</div></section>` : ""}
        ${prereleases.length ? `<section class="release-section"><div class="section-heading"><p class="eyebrow">Early testing</p><h2>Prereleases</h2><p>These are separate from the stable recommendation.</p></div><div class="release-list">${prereleases.map(item => releaseCard(item)).join("")}</div></section>` : ""}
        ${howTo}`;
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
    return `<section class="page-hero compact"><p class="eyebrow">The ecosystem, carefully</p><h1>Packages for Z+</h1><p>A central package registry is not published yet. We will link to it when it exists; this page does not pretend package installation is available today.</p></section>
      <section class="package-status"><div class="status-mark">P</div><div><p class="eyebrow">IN DEVELOPMENT</p><h2>Share a project today</h2><p>Until a registry is available, share a ZBuild project with its zproject.toml, source modules, license, and usage notes. Use relative imports and state the compiler version and backend requirements.</p>${buttonLink("/wiki/packages", "Read the package sharing guide", "text-link")}</div></section>
      <section class="section-block"><div class="section-heading"><p class="eyebrow">Current boundaries</p><h2>What is available now?</h2></div><div class="feature-grid"><article class="feature"><span class="feature-index">01</span><h3>Project builds</h3><p>ZBuild supports project build and package commands. See its reference for actual configuration and platform limits.</p>${link("/docs/zbuild", "ZBuild reference →", "text-link")}</article><article class="feature"><span class="feature-index">02</span><h3>Reusable modules</h3><p>Use relative .zh modules with explicit public declarations to share code between projects.</p>${link("/docs/modules", "Module reference →", "text-link")}</article><article class="feature"><span class="feature-index">03</span><h3>No registry command yet</h3><p>Do not rely on a package-install command or public catalog until one is announced.</p>${link("/wiki/packages", "Sharing guide →", "text-link")}</article></div></section>`;
  }

  function notFound() {
    return `<section class="page-hero compact"><p class="eyebrow">404 · NOT FOUND</p><h1>This page isn't in the guide.</h1><p>The requested Z+ route may have moved. Start from the course, reference, or homepage.</p><div class="hero-actions">${buttonLink("/", "Go home")} ${buttonLink("/learn", "Browse lessons", "button button-secondary")}</div></section>`;
  }

  function render() {
    const route = path();
    document.querySelectorAll(".primary-nav a").forEach(anchor => {
      const active = route === anchor.pathname || (anchor.pathname === "/learn" && route.startsWith("/learn/")) ||
        (anchor.pathname === "/docs" && route.startsWith("/docs/")) || (anchor.pathname === "/wiki" && route.startsWith("/wiki/"));
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
      : route === "/packages" ? "Current status and guidance for sharing Z+ projects."
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
