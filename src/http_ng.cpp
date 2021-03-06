#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDateTime>
#include <QTextCodec>
#include "../include/http_ng_p.h"
#include "../include/socket_ng.h"


Request::Request()
    :maxBodySize(1024 * 1024 * 8), maxRedirects(8)
{

}

Request::~Request()
{

}

void HeaderOperationMixin::setContentLength(qint64 contentLength)
{
    headers.insert(QString::fromUtf8("Content-Length"), QString::number(contentLength).toLatin1());
}

qint64 HeaderOperationMixin::getContentLength() const
{
    bool ok;
    QByteArray s = headers.value(QString::fromUtf8("Content-Length"));
    qint64 l = s.toULongLong(&ok);
    if(ok) {
        return l;
    } else {
        return -1;
    }
}

void HeaderOperationMixin::setContentType(const QString &contentType)
{
    headers.insert(QString::fromUtf8("Content-Type"), contentType.toUtf8());
}

QString HeaderOperationMixin::getContentType() const
{
    return QString::fromUtf8(headers.value(QString::fromUtf8("Content-Type"), "text/plain"));
}

QUrl HeaderOperationMixin::getLocation() const
{
    if(!headers.contains(QString::fromUtf8("Location"))) {
        return QUrl();
    }
    const QByteArray &value = headers.value(QString::fromUtf8("Location"));
    QUrl result = QUrl::fromEncoded(value, QUrl::StrictMode);
    if (result.isValid()) {
        return result;
    } else {
        return QUrl();
    }
}

void HeaderOperationMixin::setLocation(const QUrl &url)
{
    headers.insert(QString::fromUtf8("Location"), url.toEncoded(QUrl::FullyEncoded));
}

// Fast month string to int conversion. This code
// assumes that the Month name is correct and that
// the string is at least three chars long.
static int name_to_month(const char* month_str)
{
    switch (month_str[0]) {
    case 'J':
        switch (month_str[1]) {
        case 'a':
            return 1;
        case 'u':
            switch (month_str[2] ) {
            case 'n':
                return 6;
            case 'l':
                return 7;
            }
        }
        break;
    case 'F':
        return 2;
    case 'M':
        switch (month_str[2] ) {
        case 'r':
            return 3;
        case 'y':
            return 5;
        }
        break;
    case 'A':
        switch (month_str[1]) {
        case 'p':
            return 4;
        case 'u':
            return 8;
        }
        break;
    case 'O':
        return 10;
    case 'S':
        return 9;
    case 'N':
        return 11;
    case 'D':
        return 12;
    }

    return 0;
}

QDateTime HeaderOperationMixin::fromHttpDate(const QByteArray &value)
{
    // HTTP dates have three possible formats:
    //  RFC 1123/822      -   ddd, dd MMM yyyy hh:mm:ss "GMT"
    //  RFC 850           -   dddd, dd-MMM-yy hh:mm:ss "GMT"
    //  ANSI C's asctime  -   ddd MMM d hh:mm:ss yyyy
    // We only handle them exactly. If they deviate, we bail out.

    int pos = value.indexOf(',');
    QDateTime dt;
#ifndef QT_NO_DATESTRING
    if (pos == -1) {
        // no comma -> asctime(3) format
        dt = QDateTime::fromString(QString::fromLatin1(value), Qt::TextDate);
    } else {
        // Use sscanf over QLocal/QDateTimeParser for speed reasons. See the
        // Qt WebKit performance benchmarks to get an idea.
        if (pos == 3) {
            char month_name[4];
            int day, year, hour, minute, second;
#ifdef Q_CC_MSVC
            // Use secure version to avoid compiler warning
            if (sscanf_s(value.constData(), "%*3s, %d %3s %d %d:%d:%d 'GMT'", &day, month_name, 4, &year, &hour, &minute, &second) == 6)
#else
            // The POSIX secure mode is %ms (which allocates memory), too bleeding edge for now
            // In any case this is already safe as field width is specified.
            if (sscanf(value.constData(), "%*3s, %d %3s %d %d:%d:%d 'GMT'", &day, month_name, &year, &hour, &minute, &second) == 6)
#endif
                dt = QDateTime(QDate(year, name_to_month(month_name), day), QTime(hour, minute, second));
        } else {
            QLocale c = QLocale::c();
            // eat the weekday, the comma and the space following it
            QString sansWeekday = QString::fromLatin1(value.constData() + pos + 2);
            // must be RFC 850 date
            dt = c.toDateTime(sansWeekday, QLatin1String("dd-MMM-yy hh:mm:ss 'GMT'"));
        }
    }
#endif // QT_NO_DATESTRING

    if (dt.isValid())
        dt.setTimeSpec(Qt::UTC);
    return dt;
}

QByteArray HeaderOperationMixin::toHttpDate(const QDateTime &dt)
{
    return QLocale::c().toString(dt, QLatin1String("ddd, dd MMM yyyy hh:mm:ss 'GMT'"))
        .toLatin1();
}

QDateTime HeaderOperationMixin::getLastModified() const
{
    if(!headers.contains(QString::fromUtf8("Last-Modified"))) {
        return QDateTime();
    }
    const QByteArray &value = headers.value(QString::fromUtf8("Last-Modified"));
    return fromHttpDate(value);
}

void HeaderOperationMixin::setLastModified(const QDateTime &lastModified)
{
    headers.insert(QString::fromUtf8("Last-Modified"), toHttpDate(lastModified));
}


void HeaderOperationMixin::setModifiedSince(const QDateTime &modifiedSince)
{
    headers.insert(QString::fromUtf8("Modified-Since"), toHttpDate(modifiedSince));
}

QDateTime HeaderOperationMixin::getModifedSince() const
{
    if(!headers.contains(QString::fromUtf8("Modified-Since"))) {
        return QDateTime();
    }
    const QByteArray &value = headers.value(QString::fromUtf8("Modified-Since"));
    return fromHttpDate(value);
}


static QStringList knownHeaders = {
    QString::fromUtf8("Content-Type"),
    QString::fromUtf8("Content-Length"),
    QString::fromUtf8("Location"),
    QString::fromUtf8("Last-Modified"),
    QString::fromUtf8("Cookie"),
    QString::fromUtf8("Set-Cookie"),
    QString::fromUtf8("Content-Disposition"),
    QString::fromUtf8("Server"),
    QString::fromUtf8("User-Agent"),
    QString::fromUtf8("Accept"),
    QString::fromUtf8("Accept-Language"),
    QString::fromUtf8("Accept-Encoding"),
    QString::fromUtf8("DNT"),
    QString::fromUtf8("Connection"),
    QString::fromUtf8("Pragma"),
    QString::fromUtf8("Cache-Control"),
    QString::fromUtf8("Date"),
    QString::fromUtf8("Allow"),
    QString::fromUtf8("Vary"),
    QString::fromUtf8("X-Frame-Options"),
};

QString normalizeHeaderName(const QString &headerName) {
    foreach(const QString &goodName, knownHeaders) {
        if(headerName.compare(goodName, Qt::CaseInsensitive) == 0) {
            return goodName;
        }
    }
    return headerName;
}

void HeaderOperationMixin::setHeader(const QString &name, const QByteArray &value)
{
    headers.insert(normalizeHeaderName(name), value);
}

void HeaderOperationMixin::addHeader(const QString &name, const QByteArray &value)
{
    headers.insertMulti(normalizeHeaderName(name), value);
}

QByteArray HeaderOperationMixin::getHeader(const QString &name) const
{
    return headers.value(normalizeHeaderName(name));
}

QByteArrayList HeaderOperationMixin::getMultiHeader(const QString &name) const
{
    return headers.values(normalizeHeaderName(name));
}

FormData::FormData()
{
    const QByteArray possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    const int randomPartLength = 16;

    QByteArray randomPart;
    for(int i=0; i<randomPartLength; ++i) {
       int index = qrand() % possibleCharacters.length();
       char nextChar = possibleCharacters.at(index);
       randomPart.append(nextChar);
    }

    boundary = QByteArray("----WebKitFormBoundary") + randomPart;
}

QByteArray formatHeaderParam(const QString &name, const QString &value)
{
    QTextCodec *asciiCodec = QTextCodec::codecForName("latin1");
    if(!asciiCodec) {
        asciiCodec = QTextCodec::codecForName("ascii");
    }
    QByteArray data;
    if(asciiCodec && asciiCodec->canEncode(value)) {
        data.append(name.toUtf8());
        data.append("=\"");
        data.append(value.toUtf8());
        data.append("\"");
        return data;
    } else {
        data.append(name.toUtf8());
        data.append("*=UTF8''");
        data.append(QUrl::toPercentEncoding(value));
    }
    return data;
}

QByteArray FormData::toByteArray() const
{
    QByteArray body;
    for(QMap<QString, QString>::const_iterator itor = query.constBegin(); itor != query.constEnd(); ++itor) {
        body.append("--");
        body.append(boundary);
        body.append("\r\n");
        body.append("Content-Disposition: form-data;");
        body.append(formatHeaderParam(QString::fromUtf8("name"), itor.key()));
        body.append("\r\n\r\n");
        body.append(itor.value().toUtf8());
        body.append("\r\n");
    }
    for(QMap<QString, FormDataFile>::const_iterator itor = files.constBegin(); itor != files.constEnd(); ++itor) {
        body.append("--");
        body.append(boundary);
        body.append("\r\n");
        body.append("Content-Disposition: form-data;");
        body.append(formatHeaderParam(QString::fromUtf8("name"), itor.key()));
        body.append("; ");
        body.append(formatHeaderParam(QString::fromUtf8("filename"), itor.value().filename));
        body.append("\r\n");
        body.append("Content-Type: ");
        body.append(itor.value().contentType);
        body.append("\r\n\r\n");
        body.append(itor.value().data);
    }
    body.append("--");
    body.append(boundary);
    body.append("--");
    return body;
}

void Request::setFormData(FormData &formData, const QString &method)
{
    this->method = method;
    QString contentType = QString::fromLatin1("multipart/form-data; boundary=%1").arg(QString::fromLatin1(formData.boundary));
    this->headers.insert(QString::fromUtf8("Content-Type"), contentType.toLatin1());
    this->body = formData.toByteArray();
}

Request Request::fromFormData(const FormData &formData)
{
    Request request;
    request.method = "POST";
    request.body = formData.toByteArray();
    QString contentType = QString::fromLatin1("multipart/form-data; boundary=%1").arg(QString::fromLatin1(formData.boundary));
    request.setContentType(contentType);
    return request;
}

Request Request::fromForm(const QUrlQuery &data)
{
    Request request;
    request.setContentType(QString::fromUtf8("application/x-www-form-urlencoded"));
    request.body = data.toString(QUrl::FullyEncoded).toUtf8();
    request.method = "POST";
    return request;
}

Request Request::fromForm(const QMap<QString, QString> &query)
{
    QUrlQuery data;
    for(QMap<QString, QString>::const_iterator itor = query.constBegin(); itor != query.constEnd(); ++itor) {
        data.addQueryItem(itor.key(), itor.value());
    }
    return fromForm(data);
}

Request Request::fromJson(const QJsonDocument &json)
{
    Request request;
    request.setContentType("application/json");
    request.body = json.toJson();
    request.method = "POST";
    return request;
}

QString Response::text()
{
    return QString::fromUtf8(body);
}

QJsonDocument Response::json()
{
    QJsonParseError error;
    QJsonDocument jsonDocument = QJsonDocument::fromJson(body, &error);
    if(error.error != QJsonParseError::NoError) {
        return QJsonDocument();
    } else {
        return jsonDocument;
    }
}

QString Response::html()
{
    // TODO detect encoding;
    return QString::fromUtf8(body);
}


SessionPrivate::SessionPrivate(Session *q_ptr)
    :q_ptr(q_ptr), maxConnectionsPerServer(10), debugLevel(0)
{
    defaultUserAgent = QString::fromUtf8("Mozilla/5.0 (X11; Linux x86_64; rv:52.0) Gecko/20100101 Firefox/52.0");
}

SessionPrivate::~SessionPrivate()
{

}

void SessionPrivate::setDefaultUserAgent(const QString &userAgent)
{
    defaultUserAgent = userAgent;
}


struct HeaderSplitter
{
    QSocketNg *connection;
    QByteArray buf;

    HeaderSplitter(QSocketNg *connection)
        :connection(connection) {}

    QByteArray nextLine()
    {
        const int MaxLineLength = 1024;
        QByteArray line;
        bool expectingLineBreak = false;

        for(int i = 0; i < MaxLineLength; ++i) {
            if(buf.isEmpty()) {
                buf = connection->recv(1024);
                if(buf.isEmpty()) {
                    return QByteArray();
                }
            }
            int j = 0;
            for(; j < buf.size() && j < MaxLineLength; ++j) {
                char c = buf.at(j);
                if(c == '\n') {
                    if(!expectingLineBreak) {
                        throw InvalidHeader();
                    }
                    buf.remove(0, j + 1);
                    return line;
                } else if(c == '\r') {
                    if(expectingLineBreak) {
                        throw InvalidHeader();
                    }
                    expectingLineBreak = true;
                } else {
                    line.append(c);
                }
            }
            buf.remove(0, j + 1);
        }
        qDebug() << "exhaused max lines.";
        throw InvalidHeader();
    }
};

QList<QByteArray> splitBytes(const QByteArray &bs, char sep, int maxSplit = -1)
{
    QList<QByteArray> tokens;
    QByteArray token;
    for(int i = 0; i < bs.size(); ++i) {
        char c = bs.at(i);
        if(c == sep && (maxSplit < 0 || tokens.size() < maxSplit)) {
            tokens.append(token);
            token.clear();
        } else {
            token.append(c);
        }
    }
    if(!token.isEmpty()) {
        tokens.append(token);
    }
    return tokens;
}


Response SessionPrivate::send(Request &request)
{
    QUrl &url = request.url;
    if(url.scheme() != QString::fromUtf8("http")) {
        qDebug() << "invalid scheme";
        throw ConnectionError();
    }
    if(!request.query.isEmpty()) {
        QUrlQuery query(url);
        for(QMap<QString, QString>::const_iterator itor = request.query.constBegin(); itor != request.query.constEnd(); ++itor) {
            query.addQueryItem(itor.key(), itor.value());
        }
        url.setQuery(query);
        request.url = url.toString();
    }

    mergeCookies(request, url);
    QMap<QString, QByteArray> allHeaders = makeHeaders(request, url);

    if(!connectionSemaphores.contains(url.host())) {
        connectionSemaphores.insert(url.host(), QSharedPointer<Semaphore>(new Semaphore(maxConnectionsPerServer)));
    }
    ScopedLock<Semaphore> lock(*connectionSemaphores[url.host()]);Q_UNUSED(lock);


    QSocketNg connection;
    connection.setDnsCache(dnsCache);
    if(!connection.connect(url.host(), url.port(80))) {
        qDebug() << "can not connect to host: " << url.host() << connection.errorString();
        throw ConnectionError();
    }

    QByteArrayList lines;
    QByteArray resourcePath = url.toEncoded(QUrl::RemoveAuthority | QUrl::RemoveFragment | QUrl::RemoveScheme);
    lines.append(request.method.toUpper().toUtf8() + QByteArray(" ") + resourcePath + QByteArray(" HTTP/1.0\r\n"));
    for(QMap<QString, QByteArray>::const_iterator itor = allHeaders.constBegin(); itor != allHeaders.constEnd(); ++itor) {
        lines.append(itor.key().toUtf8() + QByteArray(": ") + itor.value() + QByteArray("\r\n"));
    }
    lines.append(QByteArray("\r\n"));
    if(debugLevel > 0) {
        qDebug() << "sending headers:" << lines.join();
    }
    connection.sendall(lines.join());

    if(!request.body.isEmpty()) {
        if(debugLevel > 1) {
            qDebug() << "sending body:" << request.body;
        }
        connection.sendall(request.body);
    }

    Response response;
    response.request = request;
    response.url = request.url;  // redirect?

    HeaderSplitter splitter(&connection);

    QByteArray firstLine = splitter.nextLine();

    QList<QByteArray> commands = splitBytes(firstLine, ' ', 2);
    if(commands.size() != 3) {
        throw InvalidHeader();
    }
    if(commands.at(0) != QByteArray("HTTP/1.0") && commands.at(0) != QByteArray("HTTP/1.1")) {
        throw InvalidHeader();
    }
    bool ok;
    response.statusCode = commands.at(1).toInt(&ok);
    if(!ok) {
        throw InvalidHeader();
    }
    response.statusText = QString::fromLatin1(commands.at(2));

    const int MaxHeaders = 64;
    for(int i = 0; i < MaxHeaders; ++i) {
        QByteArray line = splitter.nextLine();
        if(line.isEmpty()) {
            break;
        }
        QByteArrayList headerParts = splitBytes(line, ':', 1);
        if(headerParts.size() != 2) {
            throw InvalidHeader();
        }
        QString headerName = QString::fromUtf8(headerParts[0]).trimmed();
        QByteArray headerValue = headerParts[1].trimmed();
        response.headers.insertMulti(normalizeHeaderName(headerName), headerValue);
        if(debugLevel > 0)  {
            qDebug() << "receiving header: " << headerName << headerValue;
        }
    }
    if(response.headers.contains(QString::fromUtf8("Set-Cookie"))) {
        foreach(const QByteArray &value, response.headers.values("Set-Cookie")) {
            const QList<QNetworkCookie> &cookies = QNetworkCookie::parseCookies(value);
            if(debugLevel > 0 && !cookies.isEmpty()) {
                qDebug() << "receiving cookie:" << cookies[0].toRawForm();
            }
            response.cookies.append(cookies);
        }
        cookieJar.setCookiesFromUrl(response.cookies, response.url);
    }

    if(!splitter.buf.isEmpty()) {
        response.body = splitter.buf;
    }

    qint64 contentLength = response.getContentLength();
    if(contentLength > 0) {
        if(contentLength > request.maxBodySize) {
            throw UnrewindableBodyError();
        } else {
            while(response.body.size() < contentLength) {
                qint64 leftBytes = qMin((qint64) 1024 * 8, contentLength - response.body.size());
                QByteArray t = connection.recv(leftBytes);
                if(t.isEmpty()) {
                    qDebug() << "no content!";
                    throw ConnectionError();
                }
                response.body.append(t);
            }
        }
    } else if(response.getContentLength() < 0){
        while(response.body.size() < request.maxBodySize) {
            QByteArray t = connection.recv(1024 * 8);
            if(t.isEmpty()) {
                break;
            }
            response.body.append(t);
        }
    } else {
        if(!response.body.isEmpty()) {
            // warning!
        }
    }
    if(debugLevel > 1 && !response.body.isEmpty()) {
        qDebug() << "receiving body:" << response.body;
    }
    return response;
}


QMap<QString, QByteArray> SessionPrivate::makeHeaders(Request &request, const QUrl &url)
{
    QMap<QString, QByteArray> allHeaders = request.headers;
    if(!allHeaders.contains(QString::fromUtf8("Host"))) {
        QString httpHost = url.host();
        if(url.port() != -1) {
            httpHost += QString::fromUtf8(":") + QString::number(url.port());
        }
        allHeaders.insert(QString::fromUtf8("Host"), httpHost.toUtf8());
    }

    if(!allHeaders.contains(QString::fromUtf8("User-Agent"))) {
        allHeaders.insert(QString::fromUtf8("User-Agent"), defaultUserAgent.toUtf8());
    }

    if(!allHeaders.contains(QString::fromUtf8("Accept"))) {
        allHeaders.insert(QString::fromUtf8("Accept"), QByteArray("*/*"));
    }

    if(!allHeaders.contains(QString::fromUtf8("Content-Length")) && !request.body.isEmpty()) {
        allHeaders.insert(QString::fromUtf8("Content-Length"), QString::number(request.body.size()).toUtf8());
    }
    if(!allHeaders.contains(QString::fromUtf8("Accept-Language"))) {
        allHeaders.insert(QString::fromUtf8("Accept-Language"), QByteArray("en-US,en;q=0.5"));
    }
    if(!request.cookies.isEmpty() && !allHeaders.contains(QString::fromUtf8("Cookies"))) {
        QByteArray result;
        bool first = true;
        foreach (const QNetworkCookie &cookie, request.cookies) {
            if (!first)
                result += "; ";
            first = false;
            result += cookie.toRawForm(QNetworkCookie::NameAndValueOnly);
        }
        allHeaders.insert(QString::fromUtf8("Cookie"), result);
    }
    return allHeaders;
}

void SessionPrivate::mergeCookies(Request &request, const QUrl &url)
{
    QList<QNetworkCookie> cookies = cookieJar.cookiesForUrl(url);
    if(cookies.isEmpty()) {
        return;
    }
    cookies.append(request.cookies);
    request.cookies = cookies;
}


Session::Session()
    :d_ptr(new SessionPrivate(this)) {}


Session::~Session()
{
    delete d_ptr;
}

#define COMMON_PARAMETERS_WITHOUT_DEFAULT \
    const QMap<QString, QString> &query,\
    const QMap<QString, QByteArray> &headers, \
    bool allowRedirects, \
    bool verify \

Response Session::get(const QUrl &url, COMMON_PARAMETERS_WITHOUT_DEFAULT)
{
    Q_UNUSED(allowRedirects);
    Q_UNUSED(verify);
    Request request;
    request.method = QString::fromLatin1("GET");
    request.url = url;
    request.headers = headers;
    request.query = query;
    return send(request);
}

Response Session::head(const QUrl &url, COMMON_PARAMETERS_WITHOUT_DEFAULT)
{
    Q_UNUSED(allowRedirects);
    Q_UNUSED(verify);
    Request request;
    request.method = QString::fromLatin1("HEAD");
    request.url = url;
    request.headers = headers;
    request.query = query;
    return send(request);
}

Response Session::options(const QUrl &url, COMMON_PARAMETERS_WITHOUT_DEFAULT)
{
    Q_UNUSED(allowRedirects);
    Q_UNUSED(verify);
    Request request;
    request.method = QString::fromLatin1("OPTIONS");
    request.url = url;
    request.headers = headers;
    request.query = query;
    return send(request);
}

Response Session::delete_(const QUrl &url, COMMON_PARAMETERS_WITHOUT_DEFAULT)
{
    Q_UNUSED(allowRedirects);
    Q_UNUSED(verify);
    Request request;
    request.method = QString::fromLatin1("DELETE");
    request.url = url;
    request.headers = headers;
    request.query = query;
    return send(request);
}

Response Session::post(const QUrl &url, const QByteArray &body, COMMON_PARAMETERS_WITHOUT_DEFAULT)
{
    Q_UNUSED(allowRedirects);
    Q_UNUSED(verify);
    Request request;
    request.method = QString::fromLatin1("POST");
    request.url = url;
    request.headers = headers;
    request.query = query;
    request.body = body;
    return send(request);
}

Response Session::put(const QUrl &url, const QByteArray &body, COMMON_PARAMETERS_WITHOUT_DEFAULT)
{
    Q_UNUSED(allowRedirects);
    Q_UNUSED(verify);
    Request request;
    request.method = QString::fromLatin1("PUT");
    request.url = url;
    request.headers = headers;
    request.query = query;
    request.body = body;
    return send(request);
}

Response Session::patch(const QUrl &url, const QByteArray &body, COMMON_PARAMETERS_WITHOUT_DEFAULT)
{
    Q_UNUSED(allowRedirects);
    Q_UNUSED(verify);
    Request request;
    request.method = QString::fromLatin1("PATCH");
    request.url = url;
    request.headers = headers;
    request.query = query;
    request.body = body;
    return send(request);
}


Response Session::post(const QUrl &url, const QJsonDocument &json, COMMON_PARAMETERS_WITHOUT_DEFAULT)
{
    QByteArray data = json.toJson();
    QMap<QString, QByteArray> newHeaders(headers);
    newHeaders.insert("Content-Type", "application/json");
    return post(url, data, query, newHeaders, allowRedirects, verify);
}


Response Session::put(const QUrl &url, const QJsonDocument &json, COMMON_PARAMETERS_WITHOUT_DEFAULT)
{
    QByteArray data = json.toJson();
    QMap<QString, QByteArray> newHeaders(headers);
    newHeaders.insert("Content-Type", "application/json");
    return put(url, data, query, newHeaders, allowRedirects, verify);
}


Response Session::patch(const QUrl &url, const QJsonDocument &json, COMMON_PARAMETERS_WITHOUT_DEFAULT)
{
    QByteArray data = json.toJson();
    QMap<QString, QByteArray> newHeaders(headers);
    newHeaders.insert("Content-Type", "application/json");
    return patch(url, data, query, newHeaders, allowRedirects, verify);
}


Response Session::send(Request &request)
{
    Q_D(Session);
    Response response = d->send(request);
    QList<Response> history;

    if(request.maxRedirects > 0) {
        int tries = 0;
        while(response.statusCode == 301 || response.statusCode == 302 || response.statusCode == 303 || response.statusCode == 307) {
            if(tries > request.maxRedirects) {
                throw TooManyRedirects();
            }
            Request newRequest;
            if(response.statusCode == 303 || response.statusCode == 307) {
                newRequest = request;
            } else {
                newRequest.method = "GET"; // not rfc behavior, but many browser do this.
            }
            newRequest.url = request.url.resolved(response.getLocation());
            if(!newRequest.url.isValid()) {
                throw InvalidURL();
            }
            Response newResponse = d->send(newRequest);
            history.append(response);
            response = newResponse;
            ++tries;
        }
    }
    response.history = history;
    return response;
}

QNetworkCookieJar &Session::getCookieJar()
{
    Q_D(Session);
    return d->getCookieJar();
}


QNetworkCookie Session::getCookie(const QUrl &url, const QString &name)
{
    Q_D(Session);
    const QNetworkCookieJar &jar = d->getCookieJar();
    QList<QNetworkCookie> cookies = jar.cookiesForUrl(url);
    for(int i = 0; i < cookies.size(); ++i) {
        const QNetworkCookie &cookie = cookies.at(i);
        if(cookie.name() == name) {
            return cookie;
        }
    }
    return QNetworkCookie();
}


void Session::setMaxConnectionsPerServer(int maxConnectionsPerServer)
{
    Q_D(Session);
    if(maxConnectionsPerServer <= 0) {
        maxConnectionsPerServer = INT_MAX;
    }
    d->maxConnectionsPerServer = maxConnectionsPerServer;
    //TODO update semphores
}

int Session::getMaxConnectionsPerServer()
{
    Q_D(Session);
    return d->maxConnectionsPerServer;
}


void Session::setDebugLevel(int level)
{
    Q_D(Session);
    d->debugLevel = level;
}

void Session::disableDebug()
{
    Q_D(Session);
    d->debugLevel = 0;
}

RequestException::~RequestException()
{}


QString RequestException::what() const throw()
{
    return QString::fromUtf8("An HTTP error occurred.");
}


QString HTTPError::what() const throw()
{
    return QString::fromUtf8("server respond error.");
}


QString ConnectionError::what() const throw()
{
    return QString::fromUtf8("A Connection error occurred.");
}


QString ProxyError::what() const throw()
{
    return QString::fromUtf8("A proxy error occurred.");
}


QString SSLError::what() const throw()
{
    return QString::fromUtf8("A SSL error occurred.");
}


QString RequestTimeout::what() const throw()
{
    return QString::fromUtf8("The request timed out.");
}


QString ConnectTimeout::what() const throw()
{
    return QString::fromUtf8("The request timed out while trying to connect to the remote server.");
}


QString ReadTimeout::what() const throw()
{
    return QString::fromUtf8("The server did not send any data in the allotted amount of time.");
}


QString URLRequired::what() const throw()
{
    return QString::fromUtf8("A valid URL is required to make a request.");
}


QString TooManyRedirects::what() const throw()
{
    return QString::fromUtf8("Too many redirects.");
}


QString MissingSchema::what() const throw()
{
    return QString::fromUtf8("The URL schema (e.g. http or https) is missing.");
}


QString InvalidSchema::what() const throw()
{
    return QString::fromUtf8("The URL schema can not be handled.");
}


QString InvalidURL::what() const throw()
{
    return QString::fromUtf8("The URL provided was somehow invalid.");
}


QString InvalidHeader::what() const throw()
{
    return QString::fromUtf8("Can not parse the http header.");
}

QString ChunkedEncodingError::what() const throw()
{
    return QString::fromUtf8("The server declared chunked encoding but sent an invalid chunk.");
}


QString ContentDecodingError::what() const throw()
{
    return QString::fromUtf8("Failed to decode response content");
}


QString StreamConsumedError::what() const throw()
{
    return QString::fromUtf8("The content for this response was already consumed");
}


QString RetryError::what() const throw()
{
    return QString::fromUtf8("Custom retries logic failed");
}


QString UnrewindableBodyError::what() const throw()
{
    return QString::fromUtf8("Requests encountered an error when trying to rewind a body");
}

