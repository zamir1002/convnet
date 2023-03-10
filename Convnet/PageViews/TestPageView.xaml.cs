using Convnet.Common;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace Convnet.PageViews
{
    public partial class TestPageView : UserControl
    {
        public TestPageView()
        {
            InitializeComponent();
        }

        private void TextBlockConfusionMatrix_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (e.ClickCount == 1)
               ApplicationHelper.OpenBrowser("https://en.wikipedia.org/wiki/Confusion_matrix"); 
    
            e.Handled = true;
        }

        private void Matrix_AutoGeneratedColumns(object sender, System.EventArgs e)
        {
            Matrix.Columns[0].Visibility = Visibility.Hidden;
        }

        private void TextBlockConfusionMatrix_MouseEnter(object sender, System.Windows.Input.MouseEventArgs e)
        {
            Mouse.OverrideCursor = Cursors.Help;
        }

        private void TextBlockConfusionMatrix_MouseLeave(object sender, System.Windows.Input.MouseEventArgs e)
        {
            Mouse.OverrideCursor = null;
        }
    }
}
