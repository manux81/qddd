#include "SourceEditor.h"
#include <QApplication>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QtTest/QTest>

class SourceEditorTests : public QObject {
	Q_OBJECT
private slots:
	void sourceLinesIgnoreWrapping_data()
	{
		QTest::addColumn<bool>("wrap");
		QTest::addColumn<QByteArray>("newline");
		QTest::newRow("wrapped-lf") << true << QByteArray("\n");
		QTest::newRow("wrapped-crlf") << true << QByteArray("\r\n");
		QTest::newRow("unwrapped-lf") << false << QByteArray("\n");
	}

	void sourceLinesIgnoreWrapping()
	{
		QFETCH(bool, wrap);
		QFETCH(QByteArray, newline);
		QTemporaryFile file;
		QVERIFY(file.open());
		const QByteArray source = QByteArray("// ") + QByteArray(400, 'x')
			+ newline + "int first;" + newline + "int second;" + newline;
		QCOMPARE(file.write(source), qint64(source.size()));
		QVERIFY(file.flush());

		SourceEditor editor;
		editor.resize(320, 240);
		editor.setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
		editor.show();
		QCoreApplication::processEvents();
		for (int line : {2, 3, 1}) {
			editor.showLocation(file.fileName(), line);
			QString currentFile;
			int currentLine = 0;
			editor.currentLocation(currentFile, currentLine);
			QCOMPARE(currentFile, file.fileName());
			QCOMPARE(currentLine, line);
			QCOMPARE(editor.textCursor().block().text(),
				editor.document()->findBlockByNumber(line - 1).text());
		}
	}
};

QTEST_MAIN(SourceEditorTests)
#include "SourceEditorTests.moc"