// @ts-check
// `@type` JSDoc annotations allow editor autocompletion and type checking
// (when paired with `@ts-check`).
// There are various equivalent ways to declare your Docusaurus config.
// See: https://docusaurus.io/docs/api/docusaurus-config

import {themes as prismThemes} from 'prism-react-renderer';

// This runs in Node.js - Don't use client-side code here (browser APIs, JSX...)

/** @type {import('@docusaurus/types').Config} */
const config = {
    title: 'cforge',
    tagline: 'A fast, cross-platform build tool for C and C++ written in Rust',
    favicon: 'img/logo.svg',  

  url: 'https://chasesunstrom.github.io', 
  baseUrl: '/cforge/',
  organizationName: 'chasesunstrom',
  projectName: 'cforge',

  onBrokenLinks: 'warn',
  onBrokenMarkdownLinks: 'warn',

  // Even if you don't use internationalization, you can use this field to set
  // useful metadata like html lang. For example, if your site is Chinese, you
  // may want to replace "en" with "zh-Hans".
  i18n: {
    defaultLocale: 'en',
    locales: ['en'],
  },

  presets: [
    [
      'classic',
      ({
        docs: {
          sidebarPath: require.resolve('./sidebars.js'),
          editUrl: 'https://github.com/chasesunstrom/cforge/edit/main/docs/', 
        },
        blog: {
          showReadingTime: true,
          editUrl: 'https://github.com/chasesunstrom/cforge/edit/main/blog/',
          authorsMapPath: "blog/authors.yml",
        },
        theme: {
          customCss: require.resolve('./src/css/custom.css'),
        },
      }),
    ],
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      image: 'img/cforge.png',
      navbar: {
        title: 'cforge',
        logo: {
          alt: 'cforge Logo',
          src: 'img/cforge.png',
        },
        items: [
          {
            type: 'docSidebar',
            sidebarId: 'sidebar',
            position: 'left',
            label: 'Docs',
          },
          { to: '/blog', label: 'Blog', position: 'left' },
          {
            href: 'https://github.com/chasesunstrom/cforge',
            label: 'GitHub',
            position: 'right',
          },
        ],
      },

      footer: {
        style: 'dark',
        links: [
          {
            title: 'Docs',
            items: [
              {
                label: 'Getting Started',
                to: '/docs/intro',
              },
            ],
          },
          {
            title: 'Community',
            items: [
              {
                label: 'GitHub Discussions',
                href: 'https://github.com/ChaseSunstrom/cforge/discussions',
              },
              {
                label: 'Discord',
                href: 'https://discord.gg/2pMEZGNwaN',
              },
              {
                label: 'Twitter',
                href: 'https://twitter.com/chasesunstrom',
              },
            ],
          },
          {
            title: 'More',
            items: [
              {
                label: 'Blog',
                to: '/blog',
              },
              {
                label: 'Crate',
                href: 'https://crates.io/crates/cforge'
              }
            ],
          },
        ],
        copyright: `Copyright © ${new Date().getFullYear()} cforge. Built with Docusaurus.`,
      },

      prism: {
        theme: prismThemes.github,
        darkTheme: prismThemes.dracula,
        additionalLanguages: ['rust', 'cpp'],
      },
    }),
};

export default config;
