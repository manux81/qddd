#include "MiStreamBuffer.h"

#include <QByteArray>
#include <QList>

namespace {

bool equals(const QList<QByteArray>& actual,
            std::initializer_list<QByteArray> expected)
{
	return actual == QList<QByteArray>(expected);
}

} // namespace

int main()
{
	MiStreamBuffer stream;

	if (!equals(stream.append("1^done,value=\"ok\"\n"),
	            {"1^done,value=\"ok\""}) || !stream.isEmpty())
		return 1;

	if (!stream.append("2^do").isEmpty())
		return 2;
	if (!stream.append("ne,value=\"split\"\r").isEmpty())
		return 3;
	if (!equals(stream.append("\n"), {"2^done,value=\"split\""}))
		return 4;

	if (!equals(stream.append("~\"console\\n\"\n=thread-created,id=\"1\"\r\n"
	                          "3^done,value=\"mixed\"\n"),
	            {"~\"console\\n\"", "=thread-created,id=\"1\"",
	             "3^done,value=\"mixed\""}))
		return 5;

	const QByteArray longPayload(128 * 1024, 'x');
	const QByteArray escaped = "4^done,value=\"line\\n\\\"quoted\\\"\\\\path" +
	                           longPayload + "\"";
	const int firstCut = 17;
	const int secondCut = 70000;
	if (!stream.append(escaped.left(firstCut)).isEmpty())
		return 6;
	if (!stream.append(escaped.mid(firstCut, secondCut - firstCut)).isEmpty())
		return 7;
	if (!equals(stream.append(escaped.mid(secondCut) + "\r\n"), {escaped}))
		return 8;

	if (!stream.append("5^done,value=\"unterminated\"").isEmpty())
		return 9;
	if (stream.takeRemainder() != "5^done,value=\"unterminated\"")
		return 10;
	if (!stream.isEmpty() || !stream.takeRemainder().isEmpty())
		return 11;

	return 0;
}
