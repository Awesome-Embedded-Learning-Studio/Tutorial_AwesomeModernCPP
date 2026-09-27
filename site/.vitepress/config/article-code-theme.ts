import type { ThemeRegistration } from 'shiki'
import githubLight from 'shiki/themes/github-light.mjs'
import githubDark from 'shiki/themes/github-dark.mjs'

// Preserve the bundled theme's syntax scopes while tuning the article palette.
function recolor(theme: ThemeRegistration, name: string, palette: Record<string, string>): ThemeRegistration {
  const color = (value: string | undefined) => value && (palette[value.toLowerCase()] ?? value)
  return {
    ...theme,
    name,
    colors: {
      ...theme.colors,
      'editor.foreground': color(theme.colors?.['editor.foreground'])!,
    },
    tokenColors: theme.tokenColors?.map((rule) => ({
      ...rule,
      settings: { ...rule.settings, foreground: color(rule.settings.foreground) },
    })),
  }
}

export const articleCodeThemes = {
  light: recolor(githubLight, 'article-light', {
    '#24292e': '#364152',
    '#6a737d': '#697482',
    '#d73a49': '#925477',
    '#005cc5': '#376b9b',
    '#6f42c1': '#6756a0',
    '#032f62': '#286f66',
    '#22863a': '#347252',
    '#e36209': '#99612f',
  }),
  dark: recolor(githubDark, 'article-dark', {
    '#e1e4e8': '#dce3ee',
    '#6a737d': '#9ba6b8',
    '#f97583': '#db9bbb',
    '#79b8ff': '#96bce9',
    '#b392f0': '#c0ade7',
    '#9ecbff': '#9ecfc3',
    '#85e89d': '#99c7a5',
    '#ffab70': '#dcbb8d',
  }),
}
