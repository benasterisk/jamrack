// The 10 most spoken languages in the world (total speakers, Ethnologue 2024)
// plus French, the project's original language.
// `note` is the naming convention used for pitches in that language's tradition:
//   'letter' = C D E F G A B, 'fixedDo' = Do Re Mi Fa Sol La Si,
//   'german' = like letter but B natural is "H" and B flat is "B".

export const LANGUAGES = [
  { code: 'en', name: 'English',    native: 'English',   note: 'letter',  dir: 'ltr' },
  { code: 'zh', name: 'Chinese',    native: '中文',       note: 'letter',  dir: 'ltr' },
  { code: 'hi', name: 'Hindi',      native: 'हिन्दी',      note: 'letter',  dir: 'ltr' },
  { code: 'es', name: 'Spanish',    native: 'Español',   note: 'fixedDo', dir: 'ltr' },
  { code: 'ar', name: 'Arabic',     native: 'العربية',    note: 'letter',  dir: 'rtl' },
  { code: 'fr', name: 'French',     native: 'Français',  note: 'fixedDo', dir: 'ltr' },
  { code: 'bn', name: 'Bengali',    native: 'বাংলা',      note: 'letter',  dir: 'ltr' },
  { code: 'pt', name: 'Portuguese', native: 'Português', note: 'fixedDo', dir: 'ltr' },
  { code: 'ru', name: 'Russian',    native: 'Русский',   note: 'letter',  dir: 'ltr' },
  { code: 'ur', name: 'Urdu',       native: 'اردو',       note: 'letter',  dir: 'rtl' },
  { code: 'id', name: 'Indonesian', native: 'Indonesia', note: 'letter',  dir: 'ltr' },
  { code: 'de', name: 'German',     native: 'Deutsch',   note: 'german',  dir: 'ltr' },
];

export const DEFAULT_LANG = 'en';

export const langByCode = code => LANGUAGES.find(l => l.code === code);

// Picks the best supported language from the browser's preference list.
export function detectLanguage() {
  const prefs = navigator.languages && navigator.languages.length
    ? navigator.languages
    : [navigator.language || DEFAULT_LANG];
  for (const pref of prefs) {
    const base = String(pref).toLowerCase().split('-')[0];
    if (langByCode(base)) return base;
  }
  return DEFAULT_LANG;
}
