#include <QApplication>

#include "dataplotwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    DataPlotWindow window;
    window.show();

    return app.exec();
}
