import React from 'react';
import ComponentCreator from '@docusaurus/ComponentCreator';

export default [
  {
    path: '/cforge/blog',
    component: ComponentCreator('/cforge/blog', '1d1'),
    exact: true
  },
  {
    path: '/cforge/blog/announcing-cforge-beta-v1-4-0',
    component: ComponentCreator('/cforge/blog/announcing-cforge-beta-v1-4-0', '9f9'),
    exact: true
  },
  {
    path: '/cforge/blog/archive',
    component: ComponentCreator('/cforge/blog/archive', '6ba'),
    exact: true
  },
  {
    path: '/cforge/blog/tags',
    component: ComponentCreator('/cforge/blog/tags', '75b'),
    exact: true
  },
  {
    path: '/cforge/blog/tags/beta',
    component: ComponentCreator('/cforge/blog/tags/beta', '988'),
    exact: true
  },
  {
    path: '/cforge/blog/tags/cforge',
    component: ComponentCreator('/cforge/blog/tags/cforge', 'b02'),
    exact: true
  },
  {
    path: '/cforge/blog/tags/release',
    component: ComponentCreator('/cforge/blog/tags/release', 'adb'),
    exact: true
  },
  {
    path: '/cforge/docs',
    component: ComponentCreator('/cforge/docs', '955'),
    routes: [
      {
        path: '/cforge/docs',
        component: ComponentCreator('/cforge/docs', '21a'),
        routes: [
          {
            path: '/cforge/docs',
            component: ComponentCreator('/cforge/docs', '1cc'),
            routes: [
              {
                path: '/cforge/docs/advanced-topics',
                component: ComponentCreator('/cforge/docs/advanced-topics', '029'),
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
                path: '/cforge/docs/dependencies',
                component: ComponentCreator('/cforge/docs/dependencies', 'dc4'),
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
                path: '/cforge/docs/project-configuration',
                component: ComponentCreator('/cforge/docs/project-configuration', 'c32'),
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
                path: '/cforge/docs/testing',
                component: ComponentCreator('/cforge/docs/testing', 'bb3'),
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
                path: '/cforge/docs/workspaces',
                component: ComponentCreator('/cforge/docs/workspaces', '546'),
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
