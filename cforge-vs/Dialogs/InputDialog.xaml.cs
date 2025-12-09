using System.Windows;

namespace CforgeVS
{
    public partial class InputDialog : Window
    {
        public string? Result { get; private set; }

        public InputDialog(string title, string prompt)
        {
            InitializeComponent();
            Title = title;
            PromptText.Text = prompt;
            InputBox.Focus();
        }

        private void OkButton_Click(object sender, RoutedEventArgs e)
        {
            Result = InputBox.Text;
            DialogResult = true;
            Close();
        }

        private void CancelButton_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = false;
            Close();
        }

        public static string? Show(string title, string prompt)
        {
            var dialog = new InputDialog(title, prompt);
            if (dialog.ShowDialog() == true)
            {
                return dialog.Result;
            }
            return null;
        }
    }
}
