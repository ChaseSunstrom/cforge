// @ts-check

// This runs in Node.js - Don't use client-side code here (browser APIs, JSX...)

/**
 * Creating a sidebar enables you to:
 - create an ordered group of docs
 - render a sidebar for each doc of that group
 - provide next/previous navigation

 The sidebars can be generated from the filesystem, or explicitly defined here.

 Create as many sidebars as you want.

 */
const sidebars = {
  sidebar: [
    {
      type: 'category',
      label: 'CForge',
      collapsed: false,
      items: [
        'intro',
        'installation',
        'quick-start',
        'command-reference',
        'workspace-commands',
        'advanced-configuration',
        'build-variants',
        'cross-compilation',
        'ide-integration',
        'scripts-hooks',
        'examples',
        'troubleshooting',
        'contributing',
        'license'
      ],
    },
  ],
};

export default sidebars;
