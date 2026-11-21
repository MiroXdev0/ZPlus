(() => {
  const sanitizer = window.DOMPurify;
  if (!sanitizer) throw new Error("DOMPurify must load before release-note rendering.");

  const options = {
    USE_PROFILES: { html: true },
    FORBID_TAGS: ["base", "embed", "form", "iframe", "input", "link", "meta", "object", "script", "style"],
    FORBID_ATTR: ["style"],
    ALLOW_DATA_ATTR: false
  };

  window.renderReleaseNotes = html => sanitizer.sanitize(String(html ?? ""), options);
})();
