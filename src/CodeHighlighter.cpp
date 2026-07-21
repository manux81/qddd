/*
 * Copyright (c) [2026], Manuele Conti
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

#include "CodeHighlighter.h"

CodeHighlighter::CodeHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
	// --- Keyword format ---
	// Xcode uses a distinctive purple/blue for keywords
	m_keywordFormat.setForeground(QColor("#c678dd")); // purple like Xcode/One Dark

	// --- Type format ---
	m_typeFormat.setForeground(QColor("#e5c07b")); // warm yellow for types

	// --- Literal format (true, false, nullptr, NULL) ---
	m_literalFormat.setForeground(QColor("#56b6c2")); // cyan

	// --- String/Char literal format ---
	m_stringFormat.setForeground(QColor("#98c379")); // green

	// --- Comment format ---
	m_commentFormat.setForeground(QColor("#5c6370")); // muted gray
	m_commentFormat.setFontItalic(true);

	// --- Preprocessor format ---
	m_preprocessorFormat.setForeground(QColor("#61afef")); // blue

	// --- Number format ---
	m_numberFormat.setForeground(QColor("#d19a66")); // orange

	// --- Operator format ---
	m_operatorFormat.setForeground(QColor("#56b6c2")); // cyan

	// -- C/C++ Keywords --
	const QStringList keywords = {
		"\\bauto\\b", "\\bbreak\\b", "\\bcase\\b", "\\bcatch\\b",
		"\\bclass\\b", "\\bconst\\b", "\\bconstexpr\\b", "\\bcontinue\\b",
		"\\bdecltype\\b", "\\bdefault\\b", "\\bdelete\\b", "\\bdo\\b",
		"\\belse\\b", "\\benum\\b", "\\bexplicit\\b", "\\bexport\\b",
		"\\bextern\\b", "\\bfor\\b", "\\bfriend\\b", "\\bgoto\\b",
		"\\bif\\b", "\\binline\\b", "\\bmutable\\b", "\\bnamespace\\b",
		"\\bnew\\b", "\\bnoexcept\\b", "\\boperator\\b", "\\boverride\\b",
		"\\bprivate\\b", "\\bprotected\\b", "\\bpublic\\b", "\\bregister\\b",
		"\\breturn\\b", "\\bsizeof\\b", "\\bstatic\\b", "\\bstruct\\b",
		"\\bswitch\\b", "\\btemplate\\b", "\\bthis\\b", "\\bthrow\\b",
		"\\btry\\b", "\\btypedef\\b", "\\btypename\\b", "\\bunion\\b",
		"\\busing\\b", "\\bvirtual\\b", "\\bvolatile\\b", "\\bwhile\\b",
		"\\bifdef\\b", "\\bendif\\b", "\\bdefine\\b", "\\binclude\\b",
		"\\bpragma\\b", "\\bifndef\\b", "\\bdefined\\b",
		"\\bconst_cast\\b", "\\bdynamic_cast\\b", "\\breinterpret_cast\\b",
		"\\bstatic_cast\\b", "\\btrue\\b", "\\bfalse\\b", "\\bnullptr\\b",
		"\\bNULL\\b", "\\bnull\\b",
	};

	for (const QString &pattern : keywords) {
		HighlightingRule rule;
		rule.pattern = QRegularExpression(pattern);
		if (pattern.contains("nullptr") || pattern.contains("NULL") ||
		    pattern.contains("null\\b") || pattern.contains("true") ||
		    pattern.contains("false")) {
			rule.format = m_literalFormat;
		} else {
			rule.format = m_keywordFormat;
		}
		m_rules.append(rule);
	}

	// -- C/C++ Types --
	const QStringList types = {
		"\\bbool\\b", "\\bchar\\b", "\\bdouble\\b", "\\bfloat\\b",
		"\\bint\\b", "\\blong\\b", "\\bshort\\b", "\\bsigned\\b",
		"\\bunsigned\\b", "\\bvoid\\b", "\\bwchar_t\\b",
		"\\bint8_t\\b", "\\buint8_t\\b", "\\bint16_t\\b", "\\buint16_t\\b",
		"\\bint32_t\\b", "\\buint32_t\\b", "\\bint64_t\\b", "\\buint64_t\\b",
		"\\bsize_t\\b", "\\bptrdiff_t\\b", "\\bintptr_t\\b", "\\buintptr_t\\b",
		"\\bssize_t\\b", "\\bqintptr_t\\b", "\\bquintptr_t\\b",
		"\\bqint8\\b", "\\bqint16\\b", "\\bqint32\\b", "\\bqint64\\b",
		"\\bquint8\\b", "\\bquint16\\b", "\\bquint32\\b", "\\bquint64\\b",
		"\\bqreal\\b", "\\buint\\b", "\\bushort\\b", "\\buchar\\b",
		"\\bQString\\b", "\\bQStringList\\b", "\\bQByteArray\\b",
		"\\bQVariant\\b", "\\bQMap\\b", "\\bQHash\\b", "\\bQList\\b",
		"\\bQVector\\b", "\\bQSet\\b", "\\bQPair\\b", "\\bQPointer\\b",
		"\\bQSharedPointer\\b", "\\bQWeakPointer\\b", "\\bQScopedPointer\\b",
		"\\bQObject\\b", "\\bQWidget\\b", "\\bQFrame\\b", "\\bQLabel\\b",
		"\\bQPushButton\\b", "\\bQPlainTextEdit\\b", "\\bQTextEdit\\b",
		"\\bQDialog\\b", "\\bQMainWindow\\b", "\\bQDockWidget\\b",
		"\\bQTimer\\b", "\\bQFile\\b", "\\bQDir\\b", "\\bQFileInfo\\b",
		"\\bQColor\\b", "\\bQPen\\b", "\\bQBrush\\b", "\\bQPainter\\b",
		"\\bQImage\\b", "\\bQPixmap\\b", "\\bQFont\\b", "\\bQPoint\\b",
		"\\bQRect\\b", "\\bQSize\\b", "\\bQMargins\\b",
		"\\bQTextCursor\\b", "\\bQTextBlock\\b", "\\bQTextDocument\\b",
		"\\bQTextCharFormat\\b", "\\bQTextOption\\b",
		"\\bQScrollBar\\b", "\\bQEvent\\b", "\\bQMouseEvent\\b",
		"\\bQResizeEvent\\b", "\\bQPaintEvent\\b", "\\bQKeyEvent\\b",
		"\\bQGuiApplication\\b", "\\bQScreen\\b",
		"\\bQVBoxLayout\\b", "\\bQHBoxLayout\\b", "\\bQGridLayout\\b",
		"\\bQSpacerItem\\b", "\\bQSplitter\\b", "\\bQTabWidget\\b",
		"\\bQMenu\\b", "\\bQMenuBar\\b", "\\bQAction\\b",
		"\\bQToolBar\\b", "\\bQStatusBar\\b", "\\bQComboBox\\b",
		"\\bQLineEdit\\b", "\\bQCheckBox\\b", "\\bQRadioButton\\b",
		"\\bQGroupBox\\b", "\\bQProgressBar\\b", "\\bQSlider\\b",
		"\\bQSpinBox\\b", "\\bQDoubleSpinBox\\b",
		"\\bQTableWidget\\b", "\\bQTreeWidget\\b", "\\bQListWidget\\b",
		"\\bQStandardItem\\b", "\\bQAbstractItemView\\b",
		"\\bQHeaderView\\b", "\\bQItemSelectionModel\\b",
		"\\bQGraphicsView\\b", "\\bQGraphicsScene\\b",
		"\\bQGraphicsDropShadowEffect\\b",
		"\\bQThread\\b", "\\bQMutex\\b", "\\bQWaitCondition\\b",
		"\\bQProcess\\b", "\\bQTcpSocket\\b", "\\bQTcpServer\\b",
		"\\bstd::\\w+\\b",
	};

	for (const QString &pattern : types) {
		HighlightingRule rule;
		rule.pattern = QRegularExpression(pattern);
		rule.format = m_typeFormat;
		m_rules.append(rule);
	}

	// -- Numbers (integer and floating-point) --
	HighlightingRule numberRule;
	numberRule.pattern = QRegularExpression(
		"\\b(0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO]?[0-7]+|"
		"[0-9]+\\.[0-9]*([eE][+-]?[0-9]+)?[fFlL]?|"
		"\\.[0-9]+([eE][+-]?[0-9]+)?[fFlL]?|"
		"[0-9]+[eE][+-]?[0-9]+[fFlL]?|"
		"[0-9]+[fFlLuU]*)\\b"
	);
	numberRule.format = m_numberFormat;
	m_rules.append(numberRule);

	// -- Strings and characters --
	// Double-quoted string literals (with escape sequences)
	HighlightingRule stringRule;
	stringRule.pattern = QRegularExpression(
		"\"(?:[^\"\\\\]|\\\\.)*\""
	);
	stringRule.format = m_stringFormat;
	m_rules.append(stringRule);

	// Single-quoted character literals (with escape sequences)
	HighlightingRule charRule;
	charRule.pattern = QRegularExpression(
		"'(?:[^'\\\\]|\\\\.)'"
	);
	charRule.format = m_stringFormat;
	m_rules.append(charRule);

	// Raw string literals R"(...)"
	HighlightingRule rawStringRule;
	rawStringRule.pattern = QRegularExpression(
		"R\"\\([^)]*\\)\""
	);
	rawStringRule.format = m_stringFormat;
	m_rules.append(rawStringRule);

	// -- Single-line comments --
	HighlightingRule singleLineComment;
	singleLineComment.pattern = QRegularExpression("//[^\n]*");
	singleLineComment.format = m_commentFormat;
	m_rules.append(singleLineComment);

	// -- Multi-line comment delimiters --
	m_commentStartExpr = QRegularExpression("/\\*");
	m_commentEndExpr = QRegularExpression("\\*/");

	// -- Preprocessor directives --
	HighlightingRule preprocessorRule;
	preprocessorRule.pattern = QRegularExpression("^\\s*#[^\\n]*");
	preprocessorRule.format = m_preprocessorFormat;
	m_rules.append(preprocessorRule);

	// -- Operators (after strings/comments so they don't interfere) --
	HighlightingRule operatorRule;
	operatorRule.pattern = QRegularExpression(
		"[{}()\\[\\];,.:!?<>~%^&|*/=+\\-]+"
	);
	operatorRule.format = m_operatorFormat;
	m_rules.append(operatorRule);
}

void CodeHighlighter::highlightBlock(const QString &text)
{
	// Apply normal rules first
	for (const HighlightingRule &rule : m_rules) {
		QRegularExpressionMatchIterator matchIterator =
		    rule.pattern.globalMatch(text);
		while (matchIterator.hasNext()) {
			QRegularExpressionMatch match = matchIterator.next();
			setFormat(match.capturedStart(), match.capturedLength(),
			          rule.format);
		}
	}

	// Handle multi-line comments
	setCurrentBlockState(0);

	int startIndex = 0;
	if (previousBlockState() != 1)
		startIndex = text.indexOf(m_commentStartExpr);

	while (startIndex >= 0) {
		QRegularExpressionMatch endMatch;
		int endIndex = text.indexOf(m_commentEndExpr, startIndex, &endMatch);

		int commentLength;
		if (endIndex == -1) {
			setCurrentBlockState(1);
			commentLength = text.length() - startIndex;
		} else {
			commentLength = endIndex - startIndex + endMatch.capturedLength();
		}

		setFormat(startIndex, commentLength, m_commentFormat);
		startIndex = text.indexOf(m_commentStartExpr, startIndex + commentLength);
	}
}