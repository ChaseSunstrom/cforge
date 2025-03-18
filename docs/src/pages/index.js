import React from 'react';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';

export default function Home() {
  return (
    <Layout title="cforge" description="A fast, cross-platform build tool for C and C++ written in Rust">
      <header className="hero hero--primary">
        <div className="container">
          <h1 className="hero__title">cforge</h1>
          <p className="hero__subtitle">A fast, cross-platform build tool for C and C++ written in Rust</p>
          <div className="buttons">
            <Link className="button button--secondary button--lg" to="/docs/intro">
              Get Started 🚀
            </Link>
          </div>
        </div>
      </header>
    </Layout>
  );
}
