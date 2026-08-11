/*
 * Copyright (c) 2026, Manuele Conti
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Manuele Conti nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "MainWindow.h"
#include "QDDDSplash.h"
#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[]) {
#if QT_VERSION >= 0x050600
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling); // DPI support
	QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps); //HiDPI pixmaps
#endif
	QApplication app(argc, argv);
	QCoreApplication::setOrganizationName("qddd");
	QCoreApplication::setApplicationName("qddd");
	app.setWindowIcon(QIcon(":icons/resources/icons/appicon.png"));
	Q_INIT_RESOURCE(resources);

	QString styleSheet = QStringLiteral(R"(
    QMainWindow {
        background-color: #1e1e1e;
    }
    QDockWidget {
        background: #252526;
        border: 1px solid #3c3c3c;
    }
    QDockWidget::title {
        background: #2d2d2d;
        padding: 4px 8px;
        font-size: 12px;
        color: #cccccc;
    }
    QWidget {
        background-color: #1e1e1e;
        color: #cccccc;
    }
    
    QTabBar::tab {
        background: #2d2d2d;
        padding: 4px 12px;
        margin: 0px;
        border: 1px solid #3c3c3c;
        color: #cccccc;
        font-size: 12px;
    }
    QTabBar::tab:selected {
        background: #1e1e1e;
        border-bottom: 2px solid #007acc;
        color: white;
    }
    QTabWidget::pane {
        border: 0px;
        top: -1px;
    }

    QScrollBar:vertical {
        background: transparent;
        width: 10px;
        margin: 10px 4px 10px 4px;
    }
    QScrollBar::handle:vertical {
        background: #CBD5E1;
        border-radius: 5px;
        min-height: 32px;
    }
    QScrollBar::handle:vertical:hover {
        background: #94A3B8;
    }
    QScrollBar::add-line:vertical,
    QScrollBar::sub-line:vertical {
        height: 0px;
    }
    QScrollBar::add-page:vertical,
    QScrollBar::sub-page:vertical {
        background: transparent;
    }

    QScrollBar:horizontal {
        background: transparent;
        height: 10px;
        margin: 4px 10px 4px 10px;
    }
    QScrollBar::handle:horizontal {
        background: #CBD5E1;
        border-radius: 5px;
        min-width: 32px;
    }
    QScrollBar::handle:horizontal:hover {
        background: #94A3B8;
    }
    QScrollBar::add-line:horizontal,
    QScrollBar::sub-line:horizontal {
        width: 0px;
    }
    QScrollBar::add-page:horizontal,
    QScrollBar::sub-page:horizontal {
        background: transparent;
    }
)");

#ifdef Q_OS_WIN
	styleSheet += QStringLiteral("\nQWidget { font-family: \"Segoe UI\", sans-serif; }\n");
#endif
	app.setStyleSheet(styleSheet);

	QDDDSplash splash(":icons/resources/images/qddd_splash.png");
	splash.show();
	splash.startTremble();
	app.processEvents();

	QString programPath;
	if (argc > 1)
		programPath = QString::fromLocal8Bit(argv[1]);

	// attesa simulata
	QTimer::singleShot(1200, [&]() {
		splash.stopTremble();
		splash.hide();

		MainWindow *w = new MainWindow(programPath);
		w->resize(1200, 800);
		w->show();
	});

	return app.exec();
}
