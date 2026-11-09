const translations = [...document.querySelectorAll('[data-fa]')].map(element => ({element, en: element.innerHTML, fa: element.dataset.fa}));
const language = document.getElementById('language');
let persian = false;
language.addEventListener('click', () => {
  persian = !persian;
  document.documentElement.lang = persian ? 'fa' : 'en';
  document.documentElement.dir = persian ? 'rtl' : 'ltr';
  for (const {element, en, fa} of translations) element.innerHTML = persian ? fa : en;
  language.textContent = persian ? 'English' : 'فارسی';
  language.setAttribute('aria-label', persian ? 'Switch to English' : 'تغییر زبان به فارسی');
});
const preview = document.getElementById('preview');
for (const link of document.querySelectorAll('.shot')) {
  link.addEventListener('click', event => {
    if (typeof preview.showModal !== 'function') return;
    event.preventDefault();
    const source = link.querySelector('img');
    const target = preview.querySelector('img');
    target.src = source.src; target.alt = source.alt;
    preview.querySelector('p').textContent = link.querySelector('strong').textContent;
    preview.showModal();
  });
}
document.getElementById('close-preview').addEventListener('click', () => preview.close());
preview.addEventListener('click', event => { if (event.target === preview) preview.close(); });
// Until a release exists, both buttons retain their useful repository-release fallback.
fetch('https://api.github.com/repos/seifzadeh/stel/releases/latest', {headers: {Accept: 'application/vnd.github+json'}})
  .then(response => response.ok ? response.json() : null)
  .then(release => {
    if (!release) return;
    for (const [id, suffix] of [['linux-download', '-linux-x86_64.run'], ['windows-download', '-windows-x64.exe']]) {
      const asset = release.assets.find(item => item.name.endsWith(suffix));
      if (asset && asset.browser_download_url.startsWith('https://github.com/seifzadeh/stel/releases/download/')) {
        document.getElementById(id).href = asset.browser_download_url;
      }
    }
  }).catch(() => {});
