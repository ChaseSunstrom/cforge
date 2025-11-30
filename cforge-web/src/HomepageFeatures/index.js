import React from 'react';
import clsx from 'clsx';
import styles from './styles.module.css';

const FeatureList = [
  {
    title: 'Simple TOML Configuration',
    description: (
      <>
        Replace complex CMakeLists.txt with clean, readable TOML files.
        CForge generates optimized CMake behind the scenes while you focus on code.
      </>
    ),
    icon: '⚙️',
  },
  {
    title: 'Integrated Dependency Management',
    description: (
      <>
        First-class support for vcpkg, Conan, Git submodules, and system libraries.
        Declare dependencies once, CForge handles the rest.
      </>
    ),
    icon: '📦',
  },
  {
    title: 'Cross-Platform & Cross-Compile',
    description: (
      <>
        Build for Windows, macOS, Linux natively. Cross-compile for Android, iOS,
        Raspberry Pi, and WebAssembly with built-in toolchain support.
      </>
    ),
    icon: '🌐',
  },
  {
    title: 'Workspaces & Monorepos',
    description: (
      <>
        Manage multiple projects together with automatic dependency resolution
        between local projects. Perfect for large codebases.
      </>
    ),
    icon: '🗂️',
  },
  {
    title: 'Cargo-Style Output',
    description: (
      <>
        Beautiful colored terminal output with build timing, progress indicators,
        and enhanced error diagnostics with fix suggestions.
      </>
    ),
    icon: '🎨',
  },
  {
    title: 'Developer Tooling',
    description: (
      <>
        Built-in formatting (clang-format), linting (clang-tidy), file watching,
        documentation generation, and shell completions.
      </>
    ),
    icon: '🛠️',
  },
  {
    title: 'IDE Integration',
    description: (
      <>
        Generate project files for VS Code, CLion, Xcode, and Visual Studio
        with a single command. Includes launch configs and tasks.
      </>
    ),
    icon: '🖥️',
  },
  {
    title: 'Testing & Benchmarks',
    description: (
      <>
        Integrated CTest support for unit testing. Run Google Benchmark
        and other benchmark frameworks with cforge bench.
      </>
    ),
    icon: '🧪',
  },
  {
    title: 'Project Templates',
    description: (
      <>
        Generate boilerplate code with cforge new. Create classes, headers,
        test files, and more with consistent style.
      </>
    ),
    icon: '📝',
  },
];

function Feature({icon, title, description}) {
  return (
    <div className={clsx('col col--4')}>
      <div className={styles.featureCard}>
        <div className="text--center">
          <div className={styles.featureIcon}>{icon}</div>
        </div>
        <div className="text--center padding-horiz--md">
          <h3>{title}</h3>
          <p>{description}</p>
        </div>
      </div>
    </div>
  );
}

export default function HomepageFeatures() {
  return (
    <section className={styles.features}>
      <div className="container">
        <h2 className="text--center margin-bottom--lg" style={{fontSize: '2rem', fontWeight: 700}}>
          Why Choose CForge?
        </h2>
        <div className="row">
          {FeatureList.map((props, idx) => (
            <Feature key={idx} {...props} />
          ))}
        </div>
      </div>
    </section>
  );
}
