#!/usr/bin/env node
/**
 * Generates packages.json from the cforge-index repository
 * Run: node scripts/generate-packages.js
 */

const fs = require('fs');
const path = require('path');

// Simple TOML parser for package files
function parseToml(content) {
  const result = { package: {}, cmake: {}, features: {}, maintainers: {} };
  let currentSection = 'package';
  let currentSubSection = null;

  const lines = content.split('\n');

  for (const line of lines) {
    const trimmed = line.trim();

    // Skip comments and empty lines
    if (trimmed.startsWith('#') || trimmed === '') continue;

    // Section headers
    const sectionMatch = trimmed.match(/^\[([^\]]+)\]$/);
    if (sectionMatch) {
      const section = sectionMatch[1];
      if (section.includes('.')) {
        const parts = section.split('.');
        currentSection = parts[0];
        currentSubSection = parts.slice(1).join('.');
        if (!result[currentSection]) result[currentSection] = {};
        if (currentSubSection && !result[currentSection][currentSubSection]) {
          result[currentSection][currentSubSection] = {};
        }
      } else {
        currentSection = section;
        currentSubSection = null;
        if (!result[currentSection]) result[currentSection] = {};
      }
      continue;
    }

    // Key-value pairs
    const kvMatch = trimmed.match(/^(\w+)\s*=\s*(.+)$/);
    if (kvMatch) {
      const key = kvMatch[1];
      let value = kvMatch[2].trim();

      // Parse value
      if (value.startsWith('"') && value.endsWith('"')) {
        value = value.slice(1, -1);
      } else if (value.startsWith('[') && value.endsWith(']')) {
        // Simple array parsing
        value = value.slice(1, -1).split(',')
          .map(v => v.trim().replace(/^"|"$/g, ''))
          .filter(v => v.length > 0);
      } else if (value === 'true') {
        value = true;
      } else if (value === 'false') {
        value = false;
      }

      if (currentSubSection) {
        result[currentSection][currentSubSection][key] = value;
      } else {
        result[currentSection][key] = value;
      }
    }
  }

  return result;
}

// Category icons and colors
const categoryMeta = {
  'text-processing': { icon: 'FileText', color: '#3b82f6' },
  'utilities': { icon: 'Settings', color: '#8b5cf6' },
  'development-tools': { icon: 'Code', color: '#f97316' },
  'debugging': { icon: 'Eye', color: '#ef4444' },
  'testing': { icon: 'CheckCircle', color: '#22c55e' },
  'parser': { icon: 'FileText', color: '#06b6d4' },
  'data-structures': { icon: 'Layers', color: '#a855f7' },
  'encoding': { icon: 'Code', color: '#ec4899' },
  'networking': { icon: 'Globe', color: '#0ea5e9' },
  'graphics': { icon: 'Monitor', color: '#f43f5e' },
  'game-development': { icon: 'Zap', color: '#eab308' },
  'math': { icon: 'Code', color: '#6366f1' },
  'concurrency': { icon: 'RefreshCw', color: '#14b8a6' },
  'compression': { icon: 'Package', color: '#78716c' },
  'database': { icon: 'Layers', color: '#0891b2' },
  'audio': { icon: 'Monitor', color: '#d946ef' },
  'default': { icon: 'Package', color: '#f97316' }
};

function main() {
  const indexDir = path.join(__dirname, '..', '..', 'cforge-index', 'packages');
  const outputPath = path.join(__dirname, '..', 'src', 'data', 'packages.json');

  // Ensure output directory exists
  const outputDir = path.dirname(outputPath);
  if (!fs.existsSync(outputDir)) {
    fs.mkdirSync(outputDir, { recursive: true });
  }

  const packages = [];

  // Read all package directories
  const letterDirs = fs.readdirSync(indexDir);

  for (const letter of letterDirs) {
    const letterPath = path.join(indexDir, letter);
    if (!fs.statSync(letterPath).isDirectory()) continue;

    const files = fs.readdirSync(letterPath);

    for (const file of files) {
      if (!file.endsWith('.toml')) continue;

      const filePath = path.join(letterPath, file);
      const content = fs.readFileSync(filePath, 'utf-8');

      try {
        const parsed = parseToml(content);
        const pkg = parsed.package || {};
        const cmake = parsed.cmake || {};

        // Get first category for primary categorization
        const categories = Array.isArray(pkg.categories) ? pkg.categories :
                          (pkg.categories ? [pkg.categories] : ['utilities']);
        const primaryCategory = categories[0] || 'utilities';
        const meta = categoryMeta[primaryCategory] || categoryMeta.default;

        packages.push({
          name: pkg.name || file.replace('.toml', ''),
          description: pkg.description || '',
          repository: pkg.repository || '',
          homepage: pkg.homepage || pkg.repository || '',
          documentation: pkg.documentation || '',
          license: pkg.license || 'Unknown',
          keywords: Array.isArray(pkg.keywords) ? pkg.keywords : [],
          categories: categories,
          primaryCategory: primaryCategory,
          verified: pkg.verified === true,
          target: cmake.target || '',
          headerOnly: cmake.header_only === true,
          icon: meta.icon,
          color: meta.color
        });
      } catch (e) {
        console.error(`Error parsing ${file}:`, e.message);
      }
    }
  }

  // Sort packages alphabetically
  packages.sort((a, b) => a.name.localeCompare(b.name));

  // Generate output
  const output = {
    generated: new Date().toISOString(),
    count: packages.length,
    packages: packages
  };

  fs.writeFileSync(outputPath, JSON.stringify(output, null, 2));

  console.log(`Generated ${packages.length} packages to ${outputPath}`);

  // Print category stats
  const categoryStats = {};
  for (const pkg of packages) {
    const cat = pkg.primaryCategory;
    categoryStats[cat] = (categoryStats[cat] || 0) + 1;
  }
  console.log('\nCategory breakdown:');
  Object.entries(categoryStats)
    .sort((a, b) => b[1] - a[1])
    .forEach(([cat, count]) => console.log(`  ${cat}: ${count}`));
}

main();
