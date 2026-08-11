/*
 * Copyright (c) 2026, Manuele Conti
 * All rights reserved.
 */

#pragma once

#include <QByteArray>
#include <QList>

class MiStreamBuffer
{
public:
	QList<QByteArray> append(const QByteArray& data);
	QByteArray takeRemainder();
	void clear();
	[[nodiscard]] bool isEmpty() const;

private:
	QByteArray m_buffer;
};
