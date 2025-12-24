import React from 'react';
import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import HomepageFeatures from '@site/src/HomepageFeatures';
import CodeBlock from '@theme/CodeBlock';

import styles from './index.module.css';

function HomepageHeader() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <header className={clsx('hero', styles.heroBanner)}>
      <div className="container" style={{textAlign: 'center'}}>
        <div className={styles.heroLogo} style={{display: 'flex', justifyContent: 'center'}}>
          <img src="img/cforge.png" alt="CForge Logo" className={styles.logo} />
        </div>
        <h1 className="hero__title">{siteConfig.title}</h1>
        <p className="hero__subtitle">{siteConfig.tagline}</p>
        <div className={styles.buttons}>
          <Link
            className="button button--primary button--lg"
            to="/docs/intro">
            Get Started
          </Link>
          <Link
            className="button button--secondary button--lg margin-left--md"
            to="https://github.com/ChaseSunstrom/cforge">
            View on GitHub
          </Link>
        </div>
      </div>
    </header>
  );
}

function QuickStartSection() {
  const tomlExample = `[project]
name = "my_app"
version = "1.0.0"
type = "executable"

[dependencies]
fmt = { version = "10.1.0", source = "vcpkg" }
spdlog = { version = "1.12.0", source = "vcpkg" }`;

  const terminalOutput = `$ cforge build
   Compiling my_app v1.0.0
   Compiling src/main.cpp
   Compiling src/utils.cpp
    Finished Debug target(s) in 2.34s`;

  return (
    <section className={styles.quickStart}>
      <div className="container">
        <h2 className="text--center margin-bottom--lg">Quick Start</h2>
        <div className="row">
          <div className="col col--6">
            <h3>1. Create a project</h3>
            <CodeBlock language="bash">
{`cforge init my_project
cd my_project`}
            </CodeBlock>
          </div>
          <div className="col col--6">
            <h3>2. Configure with TOML</h3>
            <CodeBlock language="toml">
              {tomlExample}
            </CodeBlock>
          </div>
        </div>
        <div className="row margin-top--lg">
          <div className="col col--6">
            <h3>3. Build & Run</h3>
            <CodeBlock language="bash">
{`cforge build
cforge run`}
            </CodeBlock>
          </div>
          <div className="col col--6">
            <h3>Cargo-style Output</h3>
            <CodeBlock language="text">
              {terminalOutput}
            </CodeBlock>
          </div>
        </div>
      </div>
    </section>
  );
}

function DeveloperToolsSection() {
  return (
    <section className={styles.devTools}>
      <div className="container">
        <h2 className="text--center margin-bottom--lg">Developer Tools</h2>
        <div className="row">
          <div className="col col--3">
            <div className={styles.toolCard}>
              <div className={styles.toolIcon}>fmt</div>
              <h4>Code Formatting</h4>
              <p>Automatic formatting with clang-format</p>
              <code>cforge fmt</code>
            </div>
          </div>
          <div className="col col--3">
            <div className={styles.toolCard}>
              <div className={styles.toolIcon}>lint</div>
              <h4>Static Analysis</h4>
              <p>Find bugs with clang-tidy</p>
              <code>cforge lint</code>
            </div>
          </div>
          <div className="col col--3">
            <div className={styles.toolCard}>
              <div className={styles.toolIcon}>watch</div>
              <h4>File Watching</h4>
              <p>Auto-rebuild on changes</p>
              <code>cforge watch</code>
            </div>
          </div>
          <div className="col col--3">
            <div className={styles.toolCard}>
              <div className={styles.toolIcon}>doc</div>
              <h4>Documentation</h4>
              <p>Generate docs with Doxygen</p>
              <code>cforge doc</code>
            </div>
          </div>
        </div>
        <div className="row margin-top--md">
          <div className="col col--3">
            <div className={styles.toolCard}>
              <div className={styles.toolIcon}>bench</div>
              <h4>Benchmarks</h4>
              <p>Run performance benchmarks</p>
              <code>cforge bench</code>
            </div>
          </div>
          <div className="col col--3">
            <div className={styles.toolCard}>
              <div className={styles.toolIcon}>deps</div>
              <h4>Dependency Management</h4>
              <p>Add, remove, check outdated</p>
              <code>cforge deps</code>
            </div>
          </div>
          <div className="col col--3">
            <div className={styles.toolCard}>
              <div className={styles.toolIcon}>new</div>
              <h4>Templates</h4>
              <p>Generate classes, tests, headers</p>
              <code>cforge new class</code>
            </div>
          </div>
          <div className="col col--3">
            <div className={styles.toolCard}>
              <div className={styles.toolIcon}>ide</div>
              <h4>IDE Integration</h4>
              <p>VS Code, CLion, Xcode, VS</p>
              <code>cforge ide vscode</code>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}

function InstallSection() {
  return (
    <section className={styles.installSection}>
      <div className="container">
        <h2 className="text--center margin-bottom--lg">Installation</h2>
        <div className="row">
          <div className="col col--4">
            <div className={styles.installCard}>
              <h3>Windows</h3>
              <CodeBlock language="powershell">
{`# PowerShell
irm https://raw.githubusercontent.com/
ChaseSunstrom/cforge/master/
scripts/install.ps1 | iex`}
              </CodeBlock>
            </div>
          </div>
          <div className="col col--4">
            <div className={styles.installCard}>
              <h3>macOS / Linux</h3>
              <CodeBlock language="bash">
{`curl -sSL https://raw.githubusercontent.com/
ChaseSunstrom/cforge/master/
scripts/install.sh | bash`}
              </CodeBlock>
            </div>
          </div>
          <div className="col col--4">
            <div className={styles.installCard}>
              <h3>From Source</h3>
              <CodeBlock language="bash">
{`git clone https://github.com/
ChaseSunstrom/cforge.git
cd cforge && ./build.sh`}
              </CodeBlock>
            </div>
          </div>
        </div>
        <div className="text--center margin-top--lg">
          <Link
            className="button button--primary button--lg"
            to="/docs/installation">
            Full Installation Guide
          </Link>
        </div>
      </div>
    </section>
  );
}

export default function Home() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <Layout
      title={`${siteConfig.title} - Modern C/C++ Build System`}
      description="A modern TOML-based build system for C/C++ with CMake & vcpkg integration. Cargo-style output, dependency management, and developer tools.">
      <HomepageHeader />
      <main>
        <HomepageFeatures />
        <QuickStartSection />
        <DeveloperToolsSection />
        <InstallSection />
      </main>
    </Layout>
  );
}
