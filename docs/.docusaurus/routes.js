import React from 'react';
import ComponentCreator from '@docusaurus/ComponentCreator';

export default [
  {
    path: '/cforge/blog',
    component: ComponentCreator('/cforge/blog', '881'),
    exact: true
  },
  {
    path: '/cforge/blog/archive',
    component: ComponentCreator('/cforge/blog/archive', '6ba'),
    exact: true
  },
  {
    path: '/cforge/blog/release',
    component: ComponentCreator('/cforge/blog/release', 'dc9'),
    exact: true
  },
  {
    path: '/cforge/blog/tags',
    component: ComponentCreator('/cforge/blog/tags', '75b'),
    exact: true
  },
  {
    path: '/cforge/blog/tags/cforge',
    component: ComponentCreator('/cforge/blog/tags/cforge', '8a3'),
    exact: true
  },
  {
    path: '/cforge/blog/tags/development',
    component: ComponentCreator('/cforge/blog/tags/development', 'c54'),
    exact: true
  },
  {
    path: '/cforge/blog/tags/rust',
    component: ComponentCreator('/cforge/blog/tags/rust', 'ea6'),
    exact: true
  },
  {
    path: '/cforge/markdown-page',
    component: ComponentCreator('/cforge/markdown-page', '694'),
    exact: true
  },
  {
    path: '/cforge/docs',
    component: ComponentCreator('/cforge/docs', '7a0'),
    routes: [
      {
        path: '/cforge/docs',
        component: ComponentCreator('/cforge/docs', '10a'),
        routes: [
          {
            path: '/cforge/docs',
            component: ComponentCreator('/cforge/docs', '2ad'),
            routes: [
              {
                path: '/cforge/docs/advanced-configuration',
                component: ComponentCreator('/cforge/docs/advanced-configuration', '58d'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/build-variants',
                component: ComponentCreator('/cforge/docs/build-variants', 'a86'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/command-reference',
                component: ComponentCreator('/cforge/docs/command-reference', '687'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/contributing',
                component: ComponentCreator('/cforge/docs/contributing', 'db8'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/cross-compilation',
                component: ComponentCreator('/cforge/docs/cross-compilation', '4e1'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/examples',
                component: ComponentCreator('/cforge/docs/examples', '117'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/ide-integration',
                component: ComponentCreator('/cforge/docs/ide-integration', 'aa8'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/installation',
                component: ComponentCreator('/cforge/docs/installation', 'f59'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/intro',
                component: ComponentCreator('/cforge/docs/intro', '4e0'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/license',
                component: ComponentCreator('/cforge/docs/license', 'b8c'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/quick-start',
                component: ComponentCreator('/cforge/docs/quick-start', '357'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/scripts-hooks',
                component: ComponentCreator('/cforge/docs/scripts-hooks', 'cbf'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/troubleshooting',
                component: ComponentCreator('/cforge/docs/troubleshooting', '373'),
                exact: true,
                sidebar: "sidebar"
              },
              {
                path: '/cforge/docs/workspace-commands',
                component: ComponentCreator('/cforge/docs/workspace-commands', 'f44'),
                exact: true,
                sidebar: "sidebar"
              }
            ]
          }
        ]
      }
    ]
  },
  {
    path: '/cforge/',
    component: ComponentCreator('/cforge/', 'e22'),
    exact: true
  },
  {
    path: '*',
    component: ComponentCreator('*'),
  },
];
