#include <QApplication>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("xgs-qt-spike");

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(new QLineEdit);
    layout->addWidget(new QPlainTextEdit);
    window.setCentralWidget(central);
    window.resize(640, 480);
    window.show();

    return app.exec();
}
