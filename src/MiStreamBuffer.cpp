/*
 * Copyright (c) 2026, Manuele Conti
 * All rights reserved.
 */

#include "MiStreamBuffer.h"

namespace {

void stripCarriageReturn(QByteArray& line)
{
	if (line.endsWith('\r'))
		line.chop(1);
}

} // namespace

QList<QByteArray> MiStreamBuffer::append(const QByteArray& data)
{
	m_buffer += data;
	QList<QByteArray> lines;

	int newline = -1;
	while ((newline = m_buffer.indexOf('\n')) >= 0) {
		QByteArray line = m_buffer.left(newline);
		m_buffer.remove(0, newline + 1);
		stripCarriageReturn(line);
		lines.push_back(std::move(line));
	}

	return lines;
}

QByteArray MiStreamBuffer::takeRemainder()
{
	QByteArray remainder = std::move(m_buffer);
	m_buffer.clear();
	stripCarriageReturn(remainder);
	return remainder;
}

void MiStreamBuffer::clear()
{
	m_buffer.clear();
}

bool MiStreamBuffer::isEmpty() const
{
	return m_buffer.isEmpty();
}
