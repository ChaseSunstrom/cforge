import React from 'react';
import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import HomepageFeatures from '@site/src/HomepageFeatures';

import styles from './index.module.css';

function HomepageHeader() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <header className={clsx('hero hero--primary', styles.heroBanner)}>
      <div className="container">
        <h1 className="hero__title">{siteConfig.title}</h1>
        <p className="hero__subtitle">{siteConfig.tagline}</p>
        <div className={styles.buttons}>
          <Link
            className="button button--secondary button--lg"
            to="/docs/intro">
            Get Started 🚀
          </Link>
        </div>
      </div>
    </header>
  );
}

export default function Home() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <Layout
      title={`${siteConfig.title}`}
      description="A TOML-based build system for C/C++ projects with seamless CMake and vcpkg integration">
      <HomepageHeader />
      <main>
        <div className="container margin-top--lg">
          <div className="row">
            <div className="col col--8 col--offset-2">
              <div className="text--center margin-bottom--lg">
                <h2>⚠️ BETA VERSION ⚠️</h2>
                <p>This software is currently in BETA. Features may be incomplete, contain bugs, or change without notice.</p>
              </div>
              
              <h2>What is CForge?</h2>
              <p>
                CForge is a modern build system designed to simplify C/C++ project management. It provides a clean 
                TOML-based configuration approach while leveraging the power of CMake and vcpkg under the hood.
              </p>
              
              <h2>Why Use CForge?</h2>
              <p>
                Managing C/C++ projects can be complex with traditional build systems. CForge makes it simple with:
              </p>
              <ul>
                <li>Simple TOML configuration files instead of complex CMake syntax</li>
                <li>Built-in dependency management with vcpkg, Conan, and Git integration</li>
                <li>Workspace support for multi-project development</li>
                <li>Cross-platform support for Windows, macOS, and Linux</li>
                <li>Cross-compilation for Android, iOS, Raspberry Pi, and WebAssembly</li>
              </ul>
              
              <div className="text--center margin-vert--xl">
                <Link
                  className="button button--primary button--lg"
                  to="/docs/installation">
                  Installation Guide
                </Link>
                <span className="margin--md"></span>
                <Link
                  className="button button--secondary button--lg"
                  to="https://github.com/ChaseSunstrom/cforge">
                  GitHub Repository
                </Link>
              </div>
            </div>
          </div>
        </div>
        <HomepageFeatures />
      </main>
    </Layout>
  );
}