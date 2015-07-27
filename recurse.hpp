#ifndef RECURSE_HPP
#define RECURSE_HPP

#include <QCoreApplication>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QHash>
#include <QVector>

#include <functional>
using std::function;
using std::bind;

struct Request {
    // tcp request data
    QString data;

    // higher level data
    QHash<QString, QString> headers;
    QString body, method, proto, url;
};

typedef function<void(Request &request, QString &response, function<void()> next)> next_f;

//!
//! \brief The Recurse class
//! main class of the app
//!
class Recurse : public QObject
{
public:

    Recurse(int & argc, char ** argv, QObject *parent = NULL);
    ~Recurse();

    bool listen(quint64 port, QHostAddress address = QHostAddress::Any);
    void use(next_f next);

private:
    QCoreApplication app;
    QTcpServer m_tcp_server;
    quint64 m_port;
    QVector<next_f> m_middleware;
    int current_middleware = 0;
    void m_next(Request &request, QString &response);
    void parse_http(Request &request);
};

Recurse::Recurse(int & argc, char ** argv, QObject *parent) : app(argc, argv)
{
    Q_UNUSED(parent);
};

Recurse::~Recurse()
{

};

//!
//! \brief Recurse::listen
//! listen for tcp requests
//!
//! \param port tcp server port
//! \param address tcp server listening address
//!
//! \return true on success
//!
bool Recurse::listen(quint64 port, QHostAddress address)
{
    m_port = port;
    int bound = m_tcp_server.listen(address, port);
    if (!bound)
        return false;

    connect(&m_tcp_server, &QTcpServer::newConnection, [this] {
        qDebug() << "client connected";
        QTcpSocket *client = m_tcp_server.nextPendingConnection();

        connect(client, &QTcpSocket::readyRead, [this, client] {
            Request request;
            QString response;

            request.data = client->readAll();
            QRegExp httpRx("^(?=[A-Z]).* \\/.* HTTP\\/[0-9]\\.[0-9]\\r\\n");
            bool isHttp = request.data.contains(httpRx);

            if (isHttp)
                parse_http(request);

            qDebug() << "client request: " << request.data;

            if (m_middleware.count() > 0)
                m_middleware[current_middleware](request, response, bind(&Recurse::m_next, this, std::ref(request), std::ref(response)));

            qDebug() << "middleware end; resp:" << response;
            current_middleware = 0;

            // send response to the client
            client->write(response.toStdString().c_str(), response.size());
        });
    });

    return app.exec();
};

//!
//! \brief Recurse::m_next
//! call next middleware
//!
void Recurse::m_next(Request &request, QString &response)
{
    qDebug() << "calling next:" << current_middleware << " num:" << m_middleware.size();

    if (++current_middleware >= m_middleware.size()) {
        return;
    };

    m_middleware[current_middleware](request, response, bind(&Recurse::m_next, this, std::ref(request), std::ref(response)));

};

//!
//! \brief Recurse::use
//! add new middleware
//!
//! \param f middleware function that will be called later
//!
void Recurse::use(next_f f)
{
    m_middleware.push_back(f);
};

//!
//! \brief Recurse::parse_http
//! parse http data
//!
//! \param data reference to data received from the tcp connection
//!
void Recurse::parse_http(Request &request)
{
    QStringList data_list = request.data.split("\r\n");
    bool is_body = false;

    for (int i = 0; i < data_list.size(); ++i) {
        QStringList item_list = data_list.at(i).split(":");

        if (item_list.length() < 2 && item_list.at(0).size() < 1 && !is_body) {
            is_body = true;
            continue;
        }
        else if (i == 0 && item_list.length() < 2) {
            QStringList first_line = item_list.at(0).split(" ");
            request.method = first_line.at(0);
            request.url = first_line.at(1).trimmed();
            request.proto = first_line.at(2).trimmed();
        }
        else if (!is_body) {
            request.headers[item_list.at(0).toLower()] = item_list.at(1).trimmed();
        }
        else {
            request.body.append(item_list.at(0));
        }
    }

    qDebug() << "request ctx ready: " << request.method << request.url << request.headers << request.proto << request.body;
};

#endif // RECURSE_HPP
