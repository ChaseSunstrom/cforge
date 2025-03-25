import React from 'react';
import clsx from 'clsx';
import styles from './styles.module.css';

const FeatureList = [
  {
    title: 'Simple Configuration',
    description: (
      <>
        Use simple TOML files to configure your projects instead of complex CMake syntax. 
        CForge handles the CMake generation behind the scenes.
      </>
    ),
    icon: '⚙️',
  },
  {
    title: 'Dependency Management',
    description: (
      <>
        Integrated support for vcpkg, Conan, Git dependencies, and more. 
        Manage all your libraries with ease.
      </>
    ),
    icon: '📦',
  },
  {
    title: 'Multi-Platform',
    description: (
      <>
        Build for Windows, macOS, Linux, and cross-compile for Android, iOS, 
        Raspberry Pi, and WebAssembly.
      </>
    ),
    icon: '🌐',
  },
  {
    title: 'Workspaces',
    description: (
      <>
        Manage multiple related projects together. Dependencies between 
        projects are automatically resolved.
      </>
    ),
    icon: '🗂️',
  },
  {
    title: 'IDE Integration',
    description: (
      <>
        Generate project files for VS Code, CLion, Xcode, and Visual Studio with
        a single command.
      </>
    ),
    icon: '🖥️',
  },
  {
    title: 'Testing Support',
    description: (
      <>
        Built-in integration with CTest for unit testing and test automation.
      </>
    ),
    icon: '🧪',
  },
];

function Feature({icon, title, description}) {
  return (
    <div className={clsx('col col--4')}>
      <div className="text--center">
        <div className={styles.featureIcon}>{icon}</div>
      </div>
      <div className="text--center padding-horiz--md">
        <h3>{title}</h3>
        <p>{description}</p>
      </div>
    </div>
  );
}

export default function HomepageFeatures() {
  return (
    <section className={styles.features}>
      <div className="container">
        <div className="row">
          {FeatureList.map((props, idx) => (
            <Feature key={idx} {...props} />
          ))}
        </div>
      </div>
    </section>
  );
}