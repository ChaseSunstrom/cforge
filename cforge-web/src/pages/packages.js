import React, { useState, useEffect, useMemo } from 'react';
import clsx from 'clsx';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import styles from './packages.module.css';
import packagesData from '../data/packages.json';

// SVG Icons
const Icons = {
  Package: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <line x1="16.5" y1="9.4" x2="7.5" y2="4.21"></line>
      <path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"></path>
      <polyline points="3.27 6.96 12 12.01 20.73 6.96"></polyline>
      <line x1="12" y1="22.08" x2="12" y2="12"></line>
    </svg>
  ),
  Search: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <circle cx="11" cy="11" r="8"></circle>
      <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
    </svg>
  ),
  GitHub: () => (
    <svg viewBox="0 0 24 24" fill="currentColor">
      <path d="M12 0c-6.626 0-12 5.373-12 12 0 5.302 3.438 9.8 8.207 11.387.599.111.793-.261.793-.577v-2.234c-3.338.726-4.033-1.416-4.033-1.416-.546-1.387-1.333-1.756-1.333-1.756-1.089-.745.083-.729.083-.729 1.205.084 1.839 1.237 1.839 1.237 1.07 1.834 2.807 1.304 3.492.997.107-.775.418-1.305.762-1.604-2.665-.305-5.467-1.334-5.467-5.931 0-1.311.469-2.381 1.236-3.221-.124-.303-.535-1.524.117-3.176 0 0 1.008-.322 3.301 1.23.957-.266 1.983-.399 3.003-.404 1.02.005 2.047.138 3.006.404 2.291-1.552 3.297-1.23 3.297-1.23.653 1.653.242 2.874.118 3.176.77.84 1.235 1.911 1.235 3.221 0 4.609-2.807 5.624-5.479 5.921.43.372.823 1.102.823 2.222v3.293c0 .319.192.694.801.576 4.765-1.589 8.199-6.086 8.199-11.386 0-6.627-5.373-12-12-12z"/>
    </svg>
  ),
  ExternalLink: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"></path>
      <polyline points="15 3 21 3 21 9"></polyline>
      <line x1="10" y1="14" x2="21" y2="3"></line>
    </svg>
  ),
  Book: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"></path>
      <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"></path>
    </svg>
  ),
  CheckCircle: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"></path>
      <polyline points="22 4 12 14.01 9 11.01"></polyline>
    </svg>
  ),
  Copy: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>
      <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path>
    </svg>
  ),
  Check: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <polyline points="20 6 9 17 4 12"></polyline>
    </svg>
  ),
  Filter: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <polygon points="22 3 2 3 10 12.46 10 19 14 21 14 12.46 22 3"></polygon>
    </svg>
  ),
  Grid: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <rect x="3" y="3" width="7" height="7"></rect>
      <rect x="14" y="3" width="7" height="7"></rect>
      <rect x="14" y="14" width="7" height="7"></rect>
      <rect x="3" y="14" width="7" height="7"></rect>
    </svg>
  ),
  List: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <line x1="8" y1="6" x2="21" y2="6"></line>
      <line x1="8" y1="12" x2="21" y2="12"></line>
      <line x1="8" y1="18" x2="21" y2="18"></line>
      <line x1="3" y1="6" x2="3.01" y2="6"></line>
      <line x1="3" y1="12" x2="3.01" y2="12"></line>
      <line x1="3" y1="18" x2="3.01" y2="18"></line>
    </svg>
  ),
};

// Category display names
const categoryInfo = {
  'utilities': { name: 'Utilities' },
  'networking': { name: 'Networking' },
  'graphics': { name: 'Graphics' },
  'parsing': { name: 'Parsing' },
  'development-tools': { name: 'Dev Tools' },
  'game-development': { name: 'Game Dev' },
  'encoding': { name: 'Encoding' },
  'command-line': { name: 'CLI' },
  'terminal': { name: 'Terminal' },
  'compression': { name: 'Compression' },
  'concurrency': { name: 'Concurrency' },
  'math': { name: 'Math' },
  'audio': { name: 'Audio' },
  'data-structures': { name: 'Data Structures' },
  'scripting': { name: 'Scripting' },
  'database': { name: 'Database' },
  'debugging': { name: 'Debugging' },
  'text-processing': { name: 'Text Processing' },
  'gui': { name: 'GUI' },
  'testing': { name: 'Testing' },
  'logging': { name: 'Logging' },
  'computer-vision': { name: 'Computer Vision' },
  'algorithms': { name: 'Algorithms' },
  'async': { name: 'Async' },
  'parser': { name: 'Parser' },
  'text': { name: 'Text' },
};

function PackageCard({ pkg, index, viewMode, onCopy }) {
  const [copied, setCopied] = useState(false);

  const handleCopy = async () => {
    await navigator.clipboard.writeText(`${pkg.name} = "*"`);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
    onCopy?.(pkg.name);
  };

  const catInfo = categoryInfo[pkg.primaryCategory] || { name: pkg.primaryCategory };

  return (
    <div
      className={clsx(styles.packageCard, viewMode === 'list' && styles.listView)}
      style={{ '--animation-delay': `${index * 30}ms` }}
    >
      <div className={styles.cardHeader}>
        <div className={styles.packageIcon} style={{ '--accent-color': pkg.color }}>
          <Icons.Package />
        </div>
        <div className={styles.packageMeta}>
          <h3 className={styles.packageName}>
            {pkg.name}
            {pkg.verified && (
              <span className={styles.verifiedBadge} title="Verified package">
                <Icons.CheckCircle />
              </span>
            )}
          </h3>
          <div className={styles.packageTags}>
            <span className={styles.categoryTag}>
              {catInfo.name}
            </span>
            {pkg.headerOnly && (
              <span className={styles.headerOnlyTag}>Header-only</span>
            )}
            <span className={styles.licenseTag}>{pkg.license}</span>
          </div>
        </div>
      </div>

      <p className={styles.packageDescription}>{pkg.description}</p>

      {pkg.keywords.length > 0 && (
        <div className={styles.keywords}>
          {pkg.keywords.slice(0, 4).map((kw, i) => (
            <span key={i} className={styles.keyword}>{kw}</span>
          ))}
        </div>
      )}

      <div className={styles.cardActions}>
        <button
          className={clsx(styles.copyButton, copied && styles.copied)}
          onClick={handleCopy}
          title="Copy to cforge.toml"
        >
          {copied ? <Icons.Check /> : <Icons.Copy />}
          <span>{copied ? 'Copied!' : 'Copy'}</span>
        </button>
        <div className={styles.actionLinks}>
          {pkg.repository && (
            <a
              href={pkg.repository.replace('.git', '')}
              target="_blank"
              rel="noopener noreferrer"
              className={styles.actionLink}
              title="GitHub"
            >
              <Icons.GitHub />
            </a>
          )}
          {pkg.documentation && (
            <a
              href={pkg.documentation}
              target="_blank"
              rel="noopener noreferrer"
              className={styles.actionLink}
              title="Documentation"
            >
              <Icons.Book />
            </a>
          )}
          {pkg.homepage && pkg.homepage !== pkg.repository && (
            <a
              href={pkg.homepage}
              target="_blank"
              rel="noopener noreferrer"
              className={styles.actionLink}
              title="Homepage"
            >
              <Icons.ExternalLink />
            </a>
          )}
        </div>
      </div>

      {pkg.target && (
        <div className={styles.cmakeTarget}>
          <code>target: {pkg.target}</code>
        </div>
      )}
    </div>
  );
}

function CategoryFilter({ categories, selected, onSelect }) {
  return (
    <div className={styles.categoryFilter}>
      <button
        className={clsx(styles.categoryChip, selected === null && styles.active)}
        onClick={() => onSelect(null)}
      >
        All
      </button>
      {categories.map(([cat, count]) => {
        const info = categoryInfo[cat] || { name: cat };
        return (
          <button
            key={cat}
            className={clsx(styles.categoryChip, selected === cat && styles.active)}
            onClick={() => onSelect(cat)}
          >
            {info.name} <span className={styles.count}>{count}</span>
          </button>
        );
      })}
    </div>
  );
}

export default function PackagesPage() {
  const [search, setSearch] = useState('');
  const [selectedCategory, setSelectedCategory] = useState(null);
  const [viewMode, setViewMode] = useState('grid');
  const [copiedPackage, setCopiedPackage] = useState(null);

  // Get unique categories with counts
  const categories = useMemo(() => {
    const counts = {};
    packagesData.packages.forEach(pkg => {
      const cat = pkg.primaryCategory;
      counts[cat] = (counts[cat] || 0) + 1;
    });
    return Object.entries(counts).sort((a, b) => b[1] - a[1]);
  }, []);

  // Filter packages
  const filteredPackages = useMemo(() => {
    return packagesData.packages.filter(pkg => {
      const matchesSearch = !search ||
        pkg.name.toLowerCase().includes(search.toLowerCase()) ||
        pkg.description.toLowerCase().includes(search.toLowerCase()) ||
        pkg.keywords.some(kw => kw.toLowerCase().includes(search.toLowerCase()));

      const matchesCategory = !selectedCategory ||
        pkg.primaryCategory === selectedCategory ||
        pkg.categories.includes(selectedCategory);

      return matchesSearch && matchesCategory;
    });
  }, [search, selectedCategory]);

  const handleCopy = (name) => {
    setCopiedPackage(name);
    setTimeout(() => setCopiedPackage(null), 2000);
  };

  return (
    <Layout
      title="Package Registry"
      description="Browse 70+ C/C++ packages available in the cforge registry"
    >
      <main className={styles.packagesPage}>
        {/* Hero Section */}
        <section className={styles.hero}>
          <div className={styles.heroBackground}>
            <div className={styles.heroOrb1}></div>
            <div className={styles.heroOrb2}></div>
            <div className={styles.heroOrb3}></div>
          </div>
          <div className={styles.heroContent}>
            <div className={styles.heroBadge}>
              <Icons.Package />
              <span>{packagesData.count} Packages</span>
            </div>
            <h1 className={styles.heroTitle}>
              Package <span className={styles.gradientText}>Registry</span>
            </h1>
            <p className={styles.heroSubtitle}>
              Discover and install high-quality C/C++ libraries with a single command.
              All packages are curated and verified for seamless CMake integration.
            </p>

            {/* Search Bar */}
            <div className={styles.searchContainer}>
              <div className={styles.searchIcon}>
                <Icons.Search />
              </div>
              <input
                type="text"
                className={styles.searchInput}
                placeholder="Search packages... (e.g., json, logging, http)"
                value={search}
                onChange={(e) => setSearch(e.target.value)}
              />
              {search && (
                <button
                  className={styles.clearSearch}
                  onClick={() => setSearch('')}
                >
                  ×
                </button>
              )}
            </div>

            {/* Quick Stats */}
            <div className={styles.quickStats}>
              <div className={styles.stat}>
                <span className={styles.statValue}>{packagesData.count}</span>
                <span className={styles.statLabel}>Packages</span>
              </div>
              <div className={styles.stat}>
                <span className={styles.statValue}>{categories.length}</span>
                <span className={styles.statLabel}>Categories</span>
              </div>
              <div className={styles.stat}>
                <span className={styles.statValue}>
                  {packagesData.packages.filter(p => p.verified).length}
                </span>
                <span className={styles.statLabel}>Verified</span>
              </div>
            </div>
          </div>
        </section>

        {/* Filters Section */}
        <section className={styles.filtersSection}>
          <div className={styles.filtersHeader}>
            <div className={styles.filterInfo}>
              <Icons.Filter />
              <span>
                Showing <strong>{filteredPackages.length}</strong> of {packagesData.count} packages
              </span>
            </div>
            <div className={styles.viewToggle}>
              <button
                className={clsx(styles.viewButton, viewMode === 'grid' && styles.active)}
                onClick={() => setViewMode('grid')}
                title="Grid view"
              >
                <Icons.Grid />
              </button>
              <button
                className={clsx(styles.viewButton, viewMode === 'list' && styles.active)}
                onClick={() => setViewMode('list')}
                title="List view"
              >
                <Icons.List />
              </button>
            </div>
          </div>

          <CategoryFilter
            categories={categories}
            selected={selectedCategory}
            onSelect={setSelectedCategory}
          />
        </section>

        {/* Packages Grid */}
        <section className={styles.packagesSection}>
          {filteredPackages.length > 0 ? (
            <div className={clsx(styles.packagesGrid, viewMode === 'list' && styles.listLayout)}>
              {filteredPackages.map((pkg, index) => (
                <PackageCard
                  key={pkg.name}
                  pkg={pkg}
                  index={index}
                  viewMode={viewMode}
                  onCopy={handleCopy}
                />
              ))}
            </div>
          ) : (
            <div className={styles.noResults}>
              <div className={styles.noResultsIcon}>
                <Icons.Search />
              </div>
              <h3>No packages found</h3>
              <p>Try adjusting your search or filter criteria</p>
              <button
                className={styles.resetButton}
                onClick={() => {
                  setSearch('');
                  setSelectedCategory(null);
                }}
              >
                Reset filters
              </button>
            </div>
          )}
        </section>

        {/* CTA Section */}
        <section className={styles.ctaSection}>
          <div className={styles.ctaCard}>
            <h2>Can't find what you need?</h2>
            <p>
              Contribute to the cforge ecosystem by adding your favorite C/C++ libraries
              to the registry.
            </p>
            <div className={styles.ctaButtons}>
              <a
                href="https://github.com/ChaseSunstrom/cforge-index"
                target="_blank"
                rel="noopener noreferrer"
                className={styles.primaryButton}
              >
                <Icons.GitHub />
                <span>Contribute a Package</span>
              </a>
              <Link to="/docs/dependencies" className={styles.secondaryButton}>
                <Icons.Book />
                <span>Learn More</span>
              </Link>
            </div>
          </div>
        </section>

        {/* Toast notification */}
        {copiedPackage && (
          <div className={styles.toast}>
            <Icons.Check />
            <span>Copied <code>{copiedPackage}</code> to clipboard!</span>
          </div>
        )}
      </main>
    </Layout>
  );
}
