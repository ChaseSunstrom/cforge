using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    public partial class OutputToolWindowControl : UserControl
    {
        private static OutputToolWindowControl? _instance;
        private bool _wordWrap = false;
        private FlowDocument _document;

        // ANSI color mappings (standard 16 colors)
        private static readonly Dictionary<int, Color> AnsiColors = new Dictionary<int, Color>
        {
            { 30, Color.FromRgb(0, 0, 0) },         // Black
            { 31, Color.FromRgb(205, 49, 49) },     // Red
            { 32, Color.FromRgb(13, 188, 121) },    // Green
            { 33, Color.FromRgb(229, 229, 16) },    // Yellow
            { 34, Color.FromRgb(36, 114, 200) },    // Blue
            { 35, Color.FromRgb(188, 63, 188) },    // Magenta
            { 36, Color.FromRgb(17, 168, 205) },    // Cyan
            { 37, Color.FromRgb(229, 229, 229) },   // White
            { 90, Color.FromRgb(102, 102, 102) },   // Bright Black (Gray)
            { 91, Color.FromRgb(241, 76, 76) },     // Bright Red
            { 92, Color.FromRgb(35, 209, 139) },    // Bright Green
            { 93, Color.FromRgb(245, 245, 67) },    // Bright Yellow
            { 94, Color.FromRgb(59, 142, 234) },    // Bright Blue
            { 95, Color.FromRgb(214, 112, 214) },   // Bright Magenta
            { 96, Color.FromRgb(41, 184, 219) },    // Bright Cyan
            { 97, Color.FromRgb(255, 255, 255) },   // Bright White
        };

        public OutputToolWindowControl()
        {
            InitializeComponent();
            _instance = this;
            _document = new FlowDocument();
            OutputBox.Document = _document;
        }

        public static OutputToolWindowControl? Instance => _instance;

        public void Clear()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            _document.Blocks.Clear();
        }

        public void AppendLine(string text)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            var paragraph = new Paragraph { Margin = new Thickness(0) };
            ParseAnsiAndAddRuns(paragraph, text);
            _document.Blocks.Add(paragraph);

            // Auto-scroll to bottom
            OutputScroller.ScrollToEnd();
        }

        public void AppendText(string text)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            if (_document.Blocks.Count == 0)
            {
                _document.Blocks.Add(new Paragraph { Margin = new Thickness(0) });
            }

            var lastParagraph = _document.Blocks.LastBlock as Paragraph;
            if (lastParagraph != null)
            {
                ParseAnsiAndAddRuns(lastParagraph, text);
            }

            OutputScroller.ScrollToEnd();
        }

        private void ParseAnsiAndAddRuns(Paragraph paragraph, string text)
        {
            // Regex to match ANSI escape sequences
            var ansiPattern = new Regex(@"\x1b\[([0-9;]*)m");

            int lastIndex = 0;
            Color currentForeground = Color.FromRgb(220, 220, 220); // Default light gray
            bool isBold = false;

            foreach (Match match in ansiPattern.Matches(text))
            {
                // Add text before this escape sequence
                if (match.Index > lastIndex)
                {
                    string beforeText = text.Substring(lastIndex, match.Index - lastIndex);
                    AddRun(paragraph, beforeText, currentForeground, isBold);
                }

                // Parse the ANSI codes
                string codes = match.Groups[1].Value;
                if (string.IsNullOrEmpty(codes) || codes == "0")
                {
                    // Reset
                    currentForeground = Color.FromRgb(220, 220, 220);
                    isBold = false;
                }
                else
                {
                    foreach (var codeStr in codes.Split(';'))
                    {
                        if (int.TryParse(codeStr, out int code))
                        {
                            if (code == 0)
                            {
                                currentForeground = Color.FromRgb(220, 220, 220);
                                isBold = false;
                            }
                            else if (code == 1)
                            {
                                isBold = true;
                            }
                            else if (AnsiColors.TryGetValue(code, out Color color))
                            {
                                currentForeground = color;
                            }
                        }
                    }
                }

                lastIndex = match.Index + match.Length;
            }

            // Add remaining text
            if (lastIndex < text.Length)
            {
                string remainingText = text.Substring(lastIndex);
                AddRun(paragraph, remainingText, currentForeground, isBold);
            }
        }

        private void AddRun(Paragraph paragraph, string text, Color foreground, bool isBold)
        {
            if (string.IsNullOrEmpty(text)) return;

            var run = new Run(text)
            {
                Foreground = new SolidColorBrush(foreground)
            };

            if (isBold)
            {
                run.FontWeight = FontWeights.Bold;
            }

            paragraph.Inlines.Add(run);
        }

        private void ClearButton_Click(object sender, RoutedEventArgs e)
        {
            Clear();
        }

        private void WordWrapButton_Click(object sender, RoutedEventArgs e)
        {
            _wordWrap = !_wordWrap;
            OutputBox.Document.PageWidth = _wordWrap ? OutputBox.ActualWidth : double.MaxValue;
            WordWrapText.Text = _wordWrap ? "Word Wrap: On" : "Word Wrap: Off";
        }
    }
}
