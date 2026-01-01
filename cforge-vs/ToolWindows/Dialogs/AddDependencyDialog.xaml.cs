using System.Windows;

namespace CforgeVS
{
    public partial class AddDependencyDialog : Window
    {
        public string PackageName => PackageNameBox.Text.Trim();
        public string PackageVersion => PackageVersionBox.Text.Trim();

        public AddDependencyDialog()
        {
            InitializeComponent();
            PackageNameBox.Focus();
        }

        private void AddButton_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrWhiteSpace(PackageName))
            {
                MessageBox.Show("Please enter a package name.", "Add Dependency", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            DialogResult = true;
            Close();
        }
    }
}
