// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import starlightThemeBlack from 'starlight-theme-black';
import mermaid from 'astro-mermaid';

// Diagrams are drawn with colour on the *stroke*, never the fill: mermaid's
// hand-drawn look plus transparent nodes means one palette reads correctly on
// both the light and the dark site theme, so no per-theme re-render is needed.
// Label colour is handled in `src/styles/fltr.css`, which follows the toggle.
const transparent = 'transparent';
const stroke = '#8a93a8';
const text = '#8a93a8';

export default defineConfig({
  site: 'https://cxcubehd.github.io',
  base: '/fltr',
  integrations: [
    // Must come before starlight: it registers the markdown transform.
    mermaid({
      theme: 'base',
      autoTheme: false,
      enableLog: false,
      mermaidConfig: {
        look: 'handDrawn',
        handDrawnSeed: 7,
        flowchart: { curve: 'basis', htmlLabels: true, padding: 14 },
        sequence: { useMaxWidth: true, mirrorActors: false, wrap: true },
        themeVariables: {
          background: transparent,
          mainBkg: transparent,
          primaryColor: transparent,
          secondaryColor: transparent,
          tertiaryColor: transparent,
          primaryBorderColor: stroke,
          secondaryBorderColor: stroke,
          tertiaryBorderColor: stroke,
          primaryTextColor: text,
          secondaryTextColor: text,
          tertiaryTextColor: text,
          lineColor: stroke,
          textColor: text,
          nodeBorder: stroke,
          clusterBkg: transparent,
          clusterBorder: stroke,
          edgeLabelBackground: transparent,
          actorBkg: transparent,
          actorBorder: stroke,
          actorTextColor: text,
          actorLineColor: stroke,
          signalColor: stroke,
          signalTextColor: text,
          labelBoxBkgColor: transparent,
          labelBoxBorderColor: stroke,
          labelTextColor: text,
          loopTextColor: text,
          noteBkgColor: transparent,
          noteBorderColor: stroke,
          noteTextColor: text,
          sequenceNumberColor: text,
        },
      },
    }),
    starlight({
      title: 'fltr',
      description:
        'A retained-mode UI framework for game engines. Flutter’s architecture in C++23, with no GC and no graphics API.',
      customCss: ['./src/styles/fltr.css'],
      social: [
        { icon: 'github', label: 'GitHub', href: 'https://github.com/cxcubehd/fltr' },
      ],
      plugins: [
        starlightThemeBlack({
          navLinks: [
            { label: 'Docs', link: '/getting-started/' },
            { label: 'Architecture', link: '/architecture/overview/' },
            { label: 'Walkthrough', link: '/walkthrough/' },
            { label: 'Reference', link: '/reference/core/' },
          ],
        }),
      ],
      sidebar: [
        {
          label: 'Getting started',
          items: [
            'getting-started',
            'getting-started/install',
            'getting-started/first-frame',
            'getting-started/where-things-live',
          ],
        },
        {
          label: 'Architecture',
          items: [
            'architecture/overview',
            'architecture/the-frame',
            'architecture/layout',
            'architecture/painting',
            'architecture/input',
            'architecture/reactivity',
            'architecture/animation',
            'architecture/focus-and-keyboard',
            'architecture/scrolling',
            'architecture/overlay',
            'architecture/memory-and-lifetimes',
            'architecture/invariants',
          ],
        },
        {
          label: 'Guides',
          items: [
            'guides/writing-a-backend',
            'guides/implementing-textservice',
            'guides/game-loop',
            'guides/styling-components',
            'guides/performance',
          ],
        },
        {
          label: 'Walkthrough',
          items: [
            'walkthrough',
            'walkthrough/skeleton',
            'walkthrough/the-loop',
            'walkthrough/backend',
            'walkthrough/text',
            'walkthrough/first-pixels',
            'walkthrough/styled-primitives',
            'walkthrough/router',
            'walkthrough/main-menu',
            'walkthrough/level-select',
            'walkthrough/settings',
            'walkthrough/gameplay-hud',
            'walkthrough/pause-menu',
            'walkthrough/animated-components',
            'walkthrough/verify',
          ],
        },
        {
          label: 'Reference',
          items: [
            'reference/core',
            'reference/paint',
            'reference/render',
            'reference/widgets',
            'reference/components',
            'reference/gestures',
            'reference/focus',
            'reference/scroll',
            'reference/animation',
            'reference/harness',
          ],
        },
        {
          label: 'Project',
          items: ['project/gaps-and-rework', 'project/roadmap', 'project/design-notes'],
        },
      ],
    }),
  ],
});
