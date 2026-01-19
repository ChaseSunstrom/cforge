import React, { useEffect, useRef, useState } from 'react';
import clsx from 'clsx';
import Link from '@docusaurus/Link';
import Layout from '@theme/Layout';

import styles from './index.module.css';

// SVG Icons
const Icons = {
  Terminal: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <polyline points="4 17 10 11 4 5"></polyline>
      <line x1="12" y1="19" x2="20" y2="19"></line>
    </svg>
  ),
  Package: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <line x1="16.5" y1="9.4" x2="7.5" y2="4.21"></line>
      <path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"></path>
      <polyline points="3.27 6.96 12 12.01 20.73 6.96"></polyline>
      <line x1="12" y1="22.08" x2="12" y2="12"></line>
    </svg>
  ),
  Globe: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <circle cx="12" cy="12" r="10"></circle>
      <line x1="2" y1="12" x2="22" y2="12"></line>
      <path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path>
    </svg>
  ),
  Zap: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"></polygon>
    </svg>
  ),
  Code: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <polyline points="16 18 22 12 16 6"></polyline>
      <polyline points="8 6 2 12 8 18"></polyline>
    </svg>
  ),
  Settings: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <circle cx="12" cy="12" r="3"></circle>
      <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
    </svg>
  ),
  Layers: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <polygon points="12 2 2 7 12 12 22 7 12 2"></polygon>
      <polyline points="2 17 12 22 22 17"></polyline>
      <polyline points="2 12 12 17 22 12"></polyline>
    </svg>
  ),
  Monitor: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <rect x="2" y="3" width="20" height="14" rx="2" ry="2"></rect>
      <line x1="8" y1="21" x2="16" y2="21"></line>
      <line x1="12" y1="17" x2="12" y2="21"></line>
    </svg>
  ),
  CheckCircle: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"></path>
      <polyline points="22 4 12 14.01 9 11.01"></polyline>
    </svg>
  ),
  ArrowRight: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <line x1="5" y1="12" x2="19" y2="12"></line>
      <polyline points="12 5 19 12 12 19"></polyline>
    </svg>
  ),
  GitHub: () => (
    <svg viewBox="0 0 24 24" fill="currentColor">
      <path d="M12 0c-6.626 0-12 5.373-12 12 0 5.302 3.438 9.8 8.207 11.387.599.111.793-.261.793-.577v-2.234c-3.338.726-4.033-1.416-4.033-1.416-.546-1.387-1.333-1.756-1.333-1.756-1.089-.745.083-.729.083-.729 1.205.084 1.839 1.237 1.839 1.237 1.07 1.834 2.807 1.304 3.492.997.107-.775.418-1.305.762-1.604-2.665-.305-5.467-1.334-5.467-5.931 0-1.311.469-2.381 1.236-3.221-.124-.303-.535-1.524.117-3.176 0 0 1.008-.322 3.301 1.23.957-.266 1.983-.399 3.003-.404 1.02.005 2.047.138 3.006.404 2.291-1.552 3.297-1.23 3.297-1.23.653 1.653.242 2.874.118 3.176.77.84 1.235 1.911 1.235 3.221 0 4.609-2.807 5.624-5.479 5.921.43.372.823 1.102.823 2.222v3.293c0 .319.192.694.801.576 4.765-1.589 8.199-6.086 8.199-11.386 0-6.627-5.373-12-12-12z"/>
    </svg>
  ),
  Eye: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path>
      <circle cx="12" cy="12" r="3"></circle>
    </svg>
  ),
  RefreshCw: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <polyline points="23 4 23 10 17 10"></polyline>
      <polyline points="1 20 1 14 7 14"></polyline>
      <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"></path>
    </svg>
  ),
  FileText: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path>
      <polyline points="14 2 14 8 20 8"></polyline>
      <line x1="16" y1="13" x2="8" y2="13"></line>
      <line x1="16" y1="17" x2="8" y2="17"></line>
      <polyline points="10 9 9 9 8 9"></polyline>
    </svg>
  ),
  Play: () => (
    <svg viewBox="0 0 24 24" fill="currentColor">
      <polygon points="5 3 19 12 5 21 5 3"></polygon>
    </svg>
  ),
  Sparkles: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M12 3l1.912 5.813a2 2 0 001.275 1.275L21 12l-5.813 1.912a2 2 0 00-1.275 1.275L12 21l-1.912-5.813a2 2 0 00-1.275-1.275L3 12l5.813-1.912a2 2 0 001.275-1.275L12 3z"></path>
    </svg>
  ),
  Folder: () => (
    <svg viewBox="0 0 24 24" fill="currentColor">
      <path d="M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/>
    </svg>
  ),
  FolderOpen: () => (
    <svg viewBox="0 0 24 24" fill="currentColor">
      <path d="M20 6h-8l-2-2H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2zm0 12H4V8h16v10z"/>
    </svg>
  ),
  File: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path>
      <polyline points="14 2 14 8 20 8"></polyline>
    </svg>
  ),
  FileCpp: () => (
    <svg viewBox="0 0 24 24" fill="none" className="iconCpp">
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" stroke="currentColor" strokeWidth="2"/>
      <polyline points="14 2 14 8 20 8" stroke="currentColor" strokeWidth="2"/>
      <text x="7" y="17" fontSize="7" fill="currentColor" fontWeight="bold">C++</text>
    </svg>
  ),
  FileToml: () => (
    <svg viewBox="0 0 24 24" fill="none" className="iconToml">
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" stroke="currentColor" strokeWidth="2"/>
      <polyline points="14 2 14 8 20 8" stroke="currentColor" strokeWidth="2"/>
    </svg>
  ),
  FileMd: () => (
    <svg viewBox="0 0 24 24" fill="none" className="iconMd">
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" stroke="currentColor" strokeWidth="2"/>
      <polyline points="14 2 14 8 20 8" stroke="currentColor" strokeWidth="2"/>
    </svg>
  ),
  ChevronRight: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
      <polyline points="9 18 15 12 9 6"></polyline>
    </svg>
  ),
  ChevronDown: () => (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
      <polyline points="6 9 12 15 18 9"></polyline>
    </svg>
  ),
};

// Intersection Observer Hook for animations
function useInView(options = {}) {
  const ref = useRef(null);
  const [isInView, setIsInView] = useState(false);

  useEffect(() => {
    const observer = new IntersectionObserver(([entry]) => {
      if (entry.isIntersecting) {
        setIsInView(true);
        observer.disconnect();
      }
    }, { threshold: 0.1, ...options });

    if (ref.current) {
      observer.observe(ref.current);
    }

    return () => observer.disconnect();
  }, []);

  return [ref, isInView];
}

// Animated Section Wrapper
function AnimatedSection({ children, className, delay = 0 }) {
  const [ref, isInView] = useInView();

  return (
    <div
      ref={ref}
      className={clsx(styles.reveal, isInView && styles.revealVisible, className)}
      style={{ '--reveal-delay': `${delay}ms` }}
    >
      {children}
    </div>
  );
}

// Interactive Demo Component
function InteractiveDemo() {
  const [ref, isInView] = useInView();
  const [phase, setPhase] = useState(0);
  const [terminalLines, setTerminalLines] = useState([]);
  const [files, setFiles] = useState([]);
  const [currentTyping, setCurrentTyping] = useState('');
  const [selectedFile, setSelectedFile] = useState(null);
  const [fileContent, setFileContent] = useState('');
  const [cursorPos, setCursorPos] = useState({ x: 0, y: 0 });
  const [showCursor, setShowCursor] = useState(false);
  const [cursorClicking, setCursorClicking] = useState(false);
  const [hasStarted, setHasStarted] = useState(false);

  const fileContents = {
    'cforge.toml': `[project]
name = "my_app"
version = "0.1.0"
cpp_standard = "20"
binary_type = "executable"

[build.config.debug]
optimize = "debug"
warnings = "all"

[build.config.release]
optimize = "speed"
lto = true`,
    'main.cpp': `#include <iostream>

int main() {
    std::cout << "Hello from my_app!" << std::endl;
    return 0;
}`,
    'README.md': `# my_app

A C++ project built with CForge.

## Build

\`\`\`bash
cforge build
\`\`\`

## Run

\`\`\`bash
cforge run
\`\`\``,
  };

  const typeText = async (text, delay = 30) => {
    for (let i = 0; i <= text.length; i++) {
      setCurrentTyping(text.slice(0, i));
      await new Promise(r => setTimeout(r, delay));
    }
    await new Promise(r => setTimeout(r, 100));
    setCurrentTyping('');
    return text;
  };

  const addLine = (text, type = 'output') => {
    setTerminalLines(prev => [...prev, { text, type, id: Date.now() + Math.random() }]);
  };

  const moveCursor = async (x, y, duration = 600) => {
    setShowCursor(true);
    setCursorPos({ x, y });
    await new Promise(r => setTimeout(r, duration));
  };

  const clickAnimation = async () => {
    setCursorClicking(true);
    await new Promise(r => setTimeout(r, 150));
    setCursorClicking(false);
    await new Promise(r => setTimeout(r, 100));
  };

  const runDemo = async () => {
    // Reset
    setTerminalLines([]);
    setFiles([]);
    setSelectedFile(null);
    setFileContent('');
    setShowCursor(false);
    setPhase(1);

    // Phase 1: Init command
    await new Promise(r => setTimeout(r, 500));
    await typeText('cforge init my_app', 40);
    addLine('$ cforge init my_app', 'command');

    await new Promise(r => setTimeout(r, 300));
    addLine('cforge - C/C++ Build System v3.1', 'info');
    await new Promise(r => setTimeout(r, 200));
    addLine('    Creating my_app', 'creating');

    // Files appear one by one
    const fileList = [
      { name: 'README.md', type: 'md', delay: 300 },
      { name: 'CMakeLists.txt', type: 'cmake', delay: 250 },
      { name: 'cforge.toml', type: 'toml', delay: 250 },
      { name: 'src', type: 'folder', delay: 300 },
      { name: 'main.cpp', type: 'cpp', parent: 'src', delay: 250 },
      { name: 'LICENSE', type: 'file', delay: 200 },
    ];

    for (const file of fileList) {
      await new Promise(r => setTimeout(r, file.delay));
      addLine(`     Created ${file.parent ? file.parent + '/' : ''}${file.name}`, 'success');
      setFiles(prev => [...prev, { ...file, id: Date.now() }]);
    }

    await new Promise(r => setTimeout(r, 300));
    addLine('    Finished my_app target(s)', 'finish');
    addLine('    Finished Command completed successfully', 'finish');

    // Phase 2: Mouse interaction with files
    await new Promise(r => setTimeout(r, 800));
    setPhase(2);

    // Move cursor to cforge.toml and click
    // File list: folder header (~28px), README (~26px), CMakeLists (~26px), cforge.toml (~26px)
    // Explorer header is ~35px, so cforge.toml starts around y = 35 + 28 + 26 + 26 = ~115, center at ~128
    await moveCursor(95, 158, 800);
    await clickAnimation();
    setSelectedFile('cforge.toml');
    setFileContent(fileContents['cforge.toml']);

    await new Promise(r => setTimeout(r, 1500));

    // Move to main.cpp (nested inside src folder)
    // src folder header at ~154, main.cpp nested below at ~180, center at ~193
    await moveCursor(115, 223, 600);
    await clickAnimation();
    setSelectedFile('main.cpp');
    setFileContent(fileContents['main.cpp']);

    await new Promise(r => setTimeout(r, 1200));
    setShowCursor(false);

    // Phase 3: Build command
    await new Promise(r => setTimeout(r, 600));
    setPhase(3);
    await typeText('cforge build', 40);
    addLine('', 'empty');
    addLine('$ cforge build', 'command');

    await new Promise(r => setTimeout(r, 300));
    addLine('cforge - C/C++ Build System v3.1', 'info');
    await new Promise(r => setTimeout(r, 200));
    addLine('  Generating CMakeLists.txt from cforge.toml', 'info');
    await new Promise(r => setTimeout(r, 300));
    addLine('    Building my_app [Debug]', 'building');
    await new Promise(r => setTimeout(r, 200));
    addLine(' Configuring CMake', 'info');
    await new Promise(r => setTimeout(r, 400));
    addLine('    Finished CMake configuration', 'finish');
    await new Promise(r => setTimeout(r, 200));
    addLine('   Compiling my_app', 'compile');
    await new Promise(r => setTimeout(r, 350));
    addLine('   Compiling main.cpp', 'compile');
    await new Promise(r => setTimeout(r, 400));
    addLine('     Linking my_app.exe', 'link');
    await new Promise(r => setTimeout(r, 300));
    addLine('    Finished Debug target(s) in 2.34s', 'finish');
    addLine('    Finished Command completed successfully', 'finish');

    // Phase 4: Run command
    await new Promise(r => setTimeout(r, 800));
    setPhase(4);
    await typeText('cforge run', 40);
    addLine('', 'empty');
    addLine('$ cforge run', 'command');

    await new Promise(r => setTimeout(r, 300));
    addLine('cforge - C/C++ Build System v3.1', 'info');
    await new Promise(r => setTimeout(r, 200));
    addLine('     Running my_app', 'run');
    await new Promise(r => setTimeout(r, 300));
    addLine('', 'empty');
    addLine('Hello from my_app!', 'program-output');
    await new Promise(r => setTimeout(r, 200));
    addLine('', 'empty');
    addLine('    Finished Command completed successfully', 'finish');

    setPhase(5);
  };

  useEffect(() => {
    if (isInView && !hasStarted) {
      setHasStarted(true);
      setTimeout(runDemo, 800);
    }
  }, [isInView, hasStarted]);

  const handleReplay = () => {
    runDemo();
  };

  const getLineClass = (type) => {
    const classes = {
      'command': styles.lineCommand,
      'info': styles.lineInfo,
      'creating': styles.lineCreating,
      'success': styles.lineSuccess,
      'finish': styles.lineFinish,
      'building': styles.lineBuilding,
      'compile': styles.lineCompile,
      'link': styles.lineLink,
      'run': styles.lineRun,
      'program-output': styles.lineProgramOutput,
      'empty': styles.lineEmpty,
    };
    return classes[type] || '';
  };

  const getFileIcon = (file) => {
    if (file.type === 'folder') return <Icons.Folder />;
    if (file.type === 'toml') return <Icons.FileToml />;
    if (file.type === 'cpp') return <Icons.FileCpp />;
    if (file.type === 'md') return <Icons.FileMd />;
    return <Icons.File />;
  };

  const handleFileClick = (fileName) => {
    if (fileContents[fileName]) {
      setSelectedFile(fileName);
      setFileContent(fileContents[fileName]);
    }
  };

  // Syntax highlighting for code
  const renderHighlightedCode = (code, language) => {
    if (language === 'toml') {
      return code.split('\n').map((line, i) => {
        // Comments
        if (line.trim().startsWith('#')) {
          return <div key={i}><span className={styles.syntaxComment}>{line}</span></div>;
        }
        // Section headers [section]
        if (line.match(/^\[.+\]$/)) {
          return <div key={i}><span className={styles.syntaxSection}>{line}</span></div>;
        }
        // Key-value pairs
        const match = line.match(/^(\s*)(\w+)(\s*=\s*)(.+)$/);
        if (match) {
          const [, indent, key, eq, value] = match;
          const isString = value.startsWith('"');
          return (
            <div key={i}>
              {indent}
              <span className={styles.syntaxKey}>{key}</span>
              <span className={styles.syntaxOperator}>{eq}</span>
              <span className={isString ? styles.syntaxString : styles.syntaxValue}>{value}</span>
            </div>
          );
        }
        return <div key={i}>{line || ' '}</div>;
      });
    }

    if (language === 'cpp') {
      const keywords = ['#include', 'int', 'return', 'void', 'const', 'auto', 'class', 'struct'];
      return code.split('\n').map((line, i) => {
        let result = line;

        // Preprocessor directives
        if (line.trim().startsWith('#include')) {
          const match = line.match(/(#include\s*)(<[^>]+>|"[^"]+")/);
          if (match) {
            return (
              <div key={i}>
                <span className={styles.syntaxPreproc}>{match[1]}</span>
                <span className={styles.syntaxString}>{match[2]}</span>
              </div>
            );
          }
        }

        // Comments
        if (line.trim().startsWith('//')) {
          return <div key={i}><span className={styles.syntaxComment}>{line}</span></div>;
        }

        // Strings
        const parts = [];
        let remaining = line;
        let keyI = 0;

        // Handle std:: namespace
        remaining = remaining.replace(/(std::)(\w+)/g, (m, ns, name) => `§NS§${ns}§NAME§${name}§END§`);

        // Handle keywords
        keywords.forEach(kw => {
          const regex = new RegExp(`\\b(${kw})\\b`, 'g');
          remaining = remaining.replace(regex, `§KW§$1§END§`);
        });

        // Handle strings
        remaining = remaining.replace(/("[^"]*")/g, '§STR§$1§END§');

        // Handle numbers
        remaining = remaining.replace(/\b(\d+)\b/g, '§NUM§$1§END§');

        // Parse the marked string
        const tokens = remaining.split(/§(KW|STR|NUM|NS|NAME|END)§/);
        let mode = null;
        const elements = [];

        tokens.forEach((token, idx) => {
          if (token === 'KW') { mode = 'keyword'; return; }
          if (token === 'STR') { mode = 'string'; return; }
          if (token === 'NUM') { mode = 'number'; return; }
          if (token === 'NS') { mode = 'namespace'; return; }
          if (token === 'NAME') { mode = 'name'; return; }
          if (token === 'END') { mode = null; return; }

          if (mode === 'keyword') {
            elements.push(<span key={idx} className={styles.syntaxKeyword}>{token}</span>);
          } else if (mode === 'string') {
            elements.push(<span key={idx} className={styles.syntaxString}>{token}</span>);
          } else if (mode === 'number') {
            elements.push(<span key={idx} className={styles.syntaxNumber}>{token}</span>);
          } else if (mode === 'namespace') {
            elements.push(<span key={idx} className={styles.syntaxNamespace}>{token}</span>);
          } else if (mode === 'name') {
            elements.push(<span key={idx} className={styles.syntaxFunction}>{token}</span>);
          } else {
            elements.push(<span key={idx}>{token}</span>);
          }
        });

        return <div key={i}>{elements.length > 0 ? elements : ' '}</div>;
      });
    }

    // Default: no highlighting
    return code.split('\n').map((line, i) => <div key={i}>{line || ' '}</div>);
  };

  const getLanguage = (fileName) => {
    if (fileName?.endsWith('.toml')) return 'toml';
    if (fileName?.endsWith('.cpp') || fileName?.endsWith('.h')) return 'cpp';
    return 'text';
  };

  return (
    <section className={styles.liveDemo} ref={ref}>
      <div className={styles.liveDemoGlow}></div>
      <div className="container">
        <AnimatedSection>
          <div className={styles.liveDemoHeader}>
            <div className={styles.liveDemoBadge}>
              <Icons.Play />
              <span>See it in action</span>
            </div>
            <h2 className={styles.liveDemoTitle}>From zero to running in seconds</h2>
            <p className={styles.liveDemoSubtitle}>
              Watch how easy it is to create, build, and run a C++ project with CForge
            </p>
          </div>
        </AnimatedSection>

        <AnimatedSection delay={100}>
          <div className={styles.demoContainer}>
            {/* IDE-like view */}
            <div className={styles.ideView}>
              {/* File Explorer */}
              <div className={styles.fileExplorer}>
                <div className={styles.explorerHeader}>
                  <span>EXPLORER</span>
                </div>
                <div className={styles.explorerContent}>
                  {files.length > 0 && (
                    <div className={styles.projectFolder}>
                      <div className={styles.folderHeader}>
                        <Icons.ChevronDown />
                        <Icons.FolderOpen />
                        <span>my_app</span>
                      </div>
                      <div className={styles.fileList}>
                        {files.filter(f => !f.parent).map((file) => (
                          <div key={file.id}>
                            {file.type === 'folder' ? (
                              <div className={styles.folderItem}>
                                <div className={styles.folderItemHeader}>
                                  <Icons.ChevronDown />
                                  <Icons.FolderOpen />
                                  <span>{file.name}</span>
                                </div>
                                <div className={styles.nestedFiles}>
                                  {files.filter(f => f.parent === file.name).map(child => (
                                    <div
                                      key={child.id}
                                      className={clsx(styles.fileItem, selectedFile === child.name && styles.fileSelected)}
                                      onClick={() => handleFileClick(child.name)}
                                    >
                                      {getFileIcon(child)}
                                      <span>{child.name}</span>
                                    </div>
                                  ))}
                                </div>
                              </div>
                            ) : (
                              <div
                                className={clsx(styles.fileItem, selectedFile === file.name && styles.fileSelected)}
                                onClick={() => handleFileClick(file.name)}
                              >
                                {getFileIcon(file)}
                                <span>{file.name}</span>
                              </div>
                            )}
                          </div>
                        ))}
                      </div>
                    </div>
                  )}
                  {files.length === 0 && (
                    <div className={styles.explorerEmpty}>
                      <span>No folder opened</span>
                    </div>
                  )}
                </div>
              </div>

              {/* Editor */}
              <div className={styles.editor}>
                <div className={styles.editorTabs}>
                  {selectedFile && (
                    <div className={styles.editorTab}>
                      {selectedFile === 'cforge.toml' ? <Icons.FileToml /> : <Icons.FileCpp />}
                      <span>{selectedFile}</span>
                    </div>
                  )}
                </div>
                <div className={styles.editorContent}>
                  {fileContent ? (
                    <pre className={styles.codePreview}>
                      <code>{renderHighlightedCode(fileContent, getLanguage(selectedFile))}</code>
                    </pre>
                  ) : (
                    <div className={styles.editorWelcome}>
                      <Icons.Code />
                      <span>Select a file to view its contents</span>
                    </div>
                  )}
                </div>
              </div>

              {/* Animated Cursor */}
              {showCursor && (
                <div
                  className={clsx(styles.animatedCursor, cursorClicking && styles.cursorClicking)}
                  style={{ left: cursorPos.x, top: cursorPos.y }}
                >
                  <svg viewBox="0 0 24 24" fill="white" stroke="black" strokeWidth="1">
                    <path d="M4 4l16 12-6 0-4 8-2-10z"/>
                  </svg>
                </div>
              )}
            </div>

            {/* Terminal */}
            <div className={styles.terminal}>
              <div className={styles.terminalHeader}>
                <div className={styles.terminalDots}>
                  <span></span><span></span><span></span>
                </div>
                <span className={styles.terminalTitle}>Terminal</span>
                {phase >= 5 && (
                  <button className={styles.replayBtn} onClick={handleReplay}>
                    <Icons.RefreshCw />
                    Replay
                  </button>
                )}
              </div>
              <div className={styles.terminalBody}>
                {terminalLines.map((line) => (
                  <div key={line.id} className={clsx(styles.termLine, getLineClass(line.type))}>
                    {line.text}
                  </div>
                ))}
                {currentTyping && (
                  <div className={styles.termLine}>
                    <span className={styles.lineCommand}>$ {currentTyping}</span>
                    <span className={styles.cursor}>|</span>
                  </div>
                )}
                {!currentTyping && phase > 0 && phase < 5 && (
                  <div className={styles.termLine}>
                    <span className={styles.cursor}>|</span>
                  </div>
                )}
              </div>
            </div>
          </div>
        </AnimatedSection>
      </div>
    </section>
  );
}

function HomepageHeader() {
  return (
    <header className={styles.hero}>
      <div className={styles.heroBackground}>
        <div className={styles.heroBgGradient}></div>
        <div className={styles.heroOrb}></div>
        <div className={styles.heroOrb2}></div>
      </div>

      <div className={clsx('container', styles.heroInner)}>
        <div className={styles.heroContent}>
          <div className={styles.heroBadge}>
            <Icons.Sparkles />
            <span>Version 3.1 now available</span>
          </div>

          <h1 className={styles.heroTitle}>
            The modern way to
            <span className={styles.heroGradientText}> build C++</span>
          </h1>

          <p className={styles.heroSubtitle}>
            Replace complex CMakeLists with clean TOML configuration. CForge brings
            cargo-style simplicity to C++ development with beautiful output, dependency
            management, and powerful developer tools.
          </p>

          <div className={styles.heroActions}>
            <Link className={styles.heroPrimaryBtn} to="/docs/quick-start">
              Get Started
              <Icons.ArrowRight />
            </Link>
            <Link className={styles.heroSecondaryBtn} to="https://github.com/ChaseSunstrom/cforge">
              <Icons.GitHub />
              View on GitHub
            </Link>
          </div>

          <div className={styles.heroMeta}>
            <div className={styles.heroMetaItem}>
              <span className={styles.heroMetaValue}>C++17+</span>
              <span className={styles.heroMetaLabel}>Standard</span>
            </div>
            <div className={styles.heroMetaDivider}></div>
            <div className={styles.heroMetaItem}>
              <span className={styles.heroMetaValue}>Cross-platform</span>
              <span className={styles.heroMetaLabel}>Windows, macOS, Linux</span>
            </div>
            <div className={styles.heroMetaDivider}></div>
            <div className={styles.heroMetaItem}>
              <span className={styles.heroMetaValue}>Open Source</span>
              <span className={styles.heroMetaLabel}>PolyForm License</span>
            </div>
          </div>
        </div>

        <div className={styles.heroVisual}>
          <div className={styles.heroCodeCard}>
            <div className={styles.heroCodeHeader}>
              <div className={styles.heroCodeDots}>
                <span></span><span></span><span></span>
              </div>
              <span className={styles.heroCodeTitle}>cforge.toml</span>
            </div>
            <div className={styles.heroCodeBody}>
              <pre className={styles.heroCodePre}>
                <code>
                  <span className={styles.codeComment}># Simple, readable configuration</span>{'\n'}
                  <span className={styles.codeKey}>[project]</span>{'\n'}
                  <span className={styles.codeAttr}>name</span> = <span className={styles.codeString}>"my_app"</span>{'\n'}
                  <span className={styles.codeAttr}>version</span> = <span className={styles.codeString}>"1.0.0"</span>{'\n'}
                  <span className={styles.codeAttr}>cpp_standard</span> = <span className={styles.codeString}>"20"</span>{'\n'}
                  {'\n'}
                  <span className={styles.codeKey}>[dependencies]</span>{'\n'}
                  <span className={styles.codeAttr}>fmt</span> = <span className={styles.codeString}>"11.1.4"</span>{'\n'}
                  <span className={styles.codeAttr}>spdlog</span> = <span className={styles.codeString}>"1.12.0"</span>
                </code>
              </pre>
            </div>
          </div>
          <div className={styles.heroTerminalCard}>
            <div className={styles.heroTerminalHeader}>
              <div className={styles.heroTerminalDots}>
                <span></span><span></span><span></span>
              </div>
              <span className={styles.heroTerminalTitle}>Terminal</span>
            </div>
            <div className={styles.heroTerminalBody}>
              <pre className={styles.heroTerminalPre}>
                <code>
                  <span className={styles.termPrompt}>$</span> cforge build --release{'\n'}
                  <span className={styles.termCompileHero}>   Compiling</span> my_app v1.0.0{'\n'}
                  <span className={styles.termFinishHero}>    Finished</span> release in 2.1s
                </code>
              </pre>
            </div>
          </div>
        </div>
      </div>

      <div className={styles.heroFade}></div>
    </header>
  );
}

function FeaturesSection() {
  const features = [
    {
      icon: <Icons.Settings />,
      title: 'TOML Configuration',
      description: 'Replace CMakeLists.txt with clean, readable TOML. CForge generates optimized CMake behind the scenes.',
    },
    {
      icon: <Icons.Package />,
      title: 'Package Management',
      description: 'Built-in registry with vcpkg, Git, and system library support. Check for outdated packages easily.',
    },
    {
      icon: <Icons.Globe />,
      title: 'Cross-Platform',
      description: 'Build natively for Windows, macOS, and Linux. Cross-compile for Android, iOS, and WebAssembly.',
    },
    {
      icon: <Icons.Layers />,
      title: 'Workspaces',
      description: 'Manage multiple projects with automatic dependency resolution. Perfect for monorepos.',
    },
    {
      icon: <Icons.Terminal />,
      title: 'Beautiful Output',
      description: 'Cargo-style colored output with progress indicators and enhanced error diagnostics.',
    },
    {
      icon: <Icons.Code />,
      title: 'Developer Tools',
      description: 'Built-in formatting, linting, file watching, documentation, and shell completions.',
    },
  ];

  return (
    <section className={styles.features}>
      <div className="container">
        <AnimatedSection>
          <div className={styles.sectionHeader}>
            <h2 className={styles.sectionTitle}>Why developers choose CForge</h2>
            <p className={styles.sectionSubtitle}>
              Everything you need for modern C++ development, without the complexity
            </p>
          </div>
        </AnimatedSection>

        <div className={styles.featuresGrid}>
          {features.map((feature, idx) => (
            <AnimatedSection key={idx} delay={idx * 80}>
              <div className={styles.featureCard}>
                <div className={styles.featureIcon}>{feature.icon}</div>
                <h3 className={styles.featureTitle}>{feature.title}</h3>
                <p className={styles.featureDescription}>{feature.description}</p>
              </div>
            </AnimatedSection>
          ))}
        </div>
      </div>
    </section>
  );
}

function ToolsSection() {
  const tools = [
    { icon: <Icons.Code />, cmd: 'fmt', title: 'Format', desc: 'clang-format' },
    { icon: <Icons.Eye />, cmd: 'lint', title: 'Lint', desc: 'clang-tidy' },
    { icon: <Icons.RefreshCw />, cmd: 'watch', title: 'Watch', desc: 'Auto-rebuild' },
    { icon: <Icons.FileText />, cmd: 'doc', title: 'Docs', desc: 'Doxygen' },
    { icon: <Icons.Zap />, cmd: 'bench', title: 'Bench', desc: 'Performance' },
    { icon: <Icons.Package />, cmd: 'deps', title: 'Deps', desc: 'Packages' },
    { icon: <Icons.Monitor />, cmd: 'ide', title: 'IDE', desc: 'VS Code/CLion' },
    { icon: <Icons.CheckCircle />, cmd: 'test', title: 'Test', desc: 'CTest' },
  ];

  return (
    <section className={styles.tools}>
      <div className="container">
        <AnimatedSection>
          <div className={styles.sectionHeader}>
            <h2 className={styles.sectionTitle}>Complete developer toolkit</h2>
            <p className={styles.sectionSubtitle}>
              All the tools you need for a professional C++ workflow
            </p>
          </div>
        </AnimatedSection>

        <div className={styles.toolsGrid}>
          {tools.map((tool, idx) => (
            <AnimatedSection key={idx} delay={idx * 50}>
              <div className={styles.toolCard}>
                <div className={styles.toolIcon}>{tool.icon}</div>
                <div className={styles.toolInfo}>
                  <code className={styles.toolCmd}>cforge {tool.cmd}</code>
                  <span className={styles.toolDesc}>{tool.desc}</span>
                </div>
              </div>
            </AnimatedSection>
          ))}
        </div>
      </div>
    </section>
  );
}

function InstallSection() {
  return (
    <section className={styles.install}>
      <div className="container">
        <AnimatedSection>
          <div className={styles.sectionHeader}>
            <h2 className={styles.sectionTitle}>Get started in seconds</h2>
            <p className={styles.sectionSubtitle}>
              One command to install, one command to build
            </p>
          </div>
        </AnimatedSection>

        <div className={styles.installGrid}>
          <AnimatedSection delay={100}>
            <div className={styles.installCard}>
              <div className={styles.installHeader}>
                <span className={styles.installPlatform}>Windows</span>
                <span className={styles.installBadge}>PowerShell</span>
              </div>
              <div className={styles.installCode}>
                <code>
                  <span className={styles.installCmd}>irm</span>
                  {' '}
                  <span className={styles.installUrl}>https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.ps1</span>
                  {' '}
                  <span className={styles.installPipe}>|</span>
                  {' '}
                  <span className={styles.installCmd}>iex</span>
                </code>
              </div>
            </div>
          </AnimatedSection>

          <AnimatedSection delay={200}>
            <div className={styles.installCard}>
              <div className={styles.installHeader}>
                <span className={styles.installPlatform}>macOS / Linux</span>
                <span className={styles.installBadge}>Bash</span>
              </div>
              <div className={styles.installCode}>
                <code>
                  <span className={styles.installCmd}>curl</span>
                  {' '}
                  <span className={styles.installArg}>-sSL</span>
                  {' '}
                  <span className={styles.installUrl}>https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh</span>
                  {' '}
                  <span className={styles.installPipe}>|</span>
                  {' '}
                  <span className={styles.installCmd}>bash</span>
                </code>
              </div>
            </div>
          </AnimatedSection>
        </div>

        <AnimatedSection delay={300}>
          <div className={styles.installFooter}>
            <Link className={styles.installLink} to="/docs/installation">
              View full installation guide
              <Icons.ArrowRight />
            </Link>
          </div>
        </AnimatedSection>
      </div>
    </section>
  );
}

function CTASection() {
  return (
    <section className={styles.cta}>
      <div className="container">
        <AnimatedSection>
          <div className={styles.ctaCard}>
            <div className={styles.ctaContent}>
              <h2 className={styles.ctaTitle}>Ready to simplify your C++ workflow?</h2>
              <p className={styles.ctaSubtitle}>
                Join developers who've switched from complex CMake to clean, readable TOML configuration
              </p>
              <div className={styles.ctaActions}>
                <Link className={styles.ctaPrimaryBtn} to="/docs/quick-start">
                  Get Started Now
                </Link>
                <Link className={styles.ctaSecondaryBtn} to="/docs/intro">
                  Read the Docs
                </Link>
              </div>
            </div>
          </div>
        </AnimatedSection>
      </div>
    </section>
  );
}

export default function Home() {
  return (
    <Layout
      title="Modern C/C++ Build System"
      description="A modern TOML-based build system for C/C++ with CMake integration. Cargo-style output, dependency management, and developer tools.">
      <div className={styles.page}>
        <HomepageHeader />
        <main className={styles.main}>
          <InteractiveDemo />
          <FeaturesSection />
          <ToolsSection />
          <InstallSection />
          <CTASection />
        </main>
      </div>
    </Layout>
  );
}
