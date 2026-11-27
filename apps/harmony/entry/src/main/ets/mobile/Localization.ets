import { translations } from './LocalizationData';

let languageProvider: (() => string) | undefined = undefined;

export function setLanguageProvider(provider: () => string): void {
  languageProvider = provider;
}

export function languageIndex(tag: string): number {
  const value = tag.toLowerCase().replace(/_/g, '-').split('.')[0].split('@')[0];
  const base = value.split('-')[0];
  if (base === 'zh') {
    const parts = '-' + value + '-';
    if (parts.includes('-hans-')) return 0;
    return ['-hant-', '-tw-', '-hk-', '-mo-'].some((part: string) => parts.includes(part)) ? 3 : 0;
  }
  const codes = ['zh', 'en', 'ru', 'zh-hant', 'ja', 'ko', 'de', 'fr', 'es', 'pt', 'tr', 'he'];
  // iw הוא הקוד הישן לעברית.
  const index = codes.indexOf(base === 'iw' ? 'he' : base);
  return index < 0 ? 1 : index;
}

export function currentLanguage(): number {
  try { return languageProvider ? languageIndex(languageProvider()) : 1; }
  catch { return 1; }
}

export function tr(key: string, args: string[] = []): string {
  // The pure models can also run before an ability installs its locale provider.
  const values = translations[key];
  let text = languageProvider && values ? values[currentLanguage()] || values[1] : key;
  // A single replacement pass avoids interpreting placeholder-like user data.
  text = text.replace(/\{(\d+)\}/g, (match: string, index: string) => {
    const value = args[Number(index)];
    return value === undefined ? match : value;
  });
  return text;
}
